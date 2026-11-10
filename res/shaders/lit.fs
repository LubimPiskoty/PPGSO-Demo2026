#version 330 core

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

in vec2 FragTexCoord;
in vec3 FragNormal;
in vec3 FragPos;
in vec3 FragTangent;

out vec4 FragColor;

// ---------------------------------------------------------------------------
// Material
// ---------------------------------------------------------------------------

struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
    float opacity;
};
uniform Material uMaterial;

// Texture units match render::TextureSlot
uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalTex;
uniform sampler2D uRoughnessTex;
uniform sampler2D uSpecularTex;
uniform sampler2D uEmissiveTex; // unit 6, units 4 and 5 are taken

uniform bool uHasNormalMap;
uniform bool uHasRoughnessMap;
uniform bool uHasSpecularMap;
uniform bool uHasEmissiveMap;
uniform float uEmissiveStrength; // multiplies the emissive map
uniform sampler2DArrayShadow uShadowMaps; // depth array, slot 5
uniform samplerCube uSkybox; // cubemap of skybox slot 4
uniform float uReflectivity; // 0 no reflect, 1 mirror
uniform vec3 uSkyTint; // the skybox's tint: dark at night, so is its reflection
uniform float uRoughness; // when mat doesnt have roughmap

// ---------------------------------------------------------------------------
// Lights (layout matches render::LightBlock)
// ---------------------------------------------------------------------------

// Must match MAX_LIGHTS in light_buffer.hpp, or the block layouts disagree
#define MAX_LIGHTS 64

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT        2

// One light as uploaded, matches render::GpuLight
struct GpuLight {
    vec4 position; // xyz = position, w = type
    vec4 direction; // xyz = direction
    vec4 color; // rgb = color, w = ambient strength
    vec4 attenuation; // x = constant, y = linear, z = quadratic
    vec4 shadow; // x = shadow map layer / ShadowBlock index, -1 = none
    // y = directional: close-up map layer, -1 = none
    vec4 cutOff; // x = inner cutoff, y = outer cutoff
};

// Must match ShadowMaps::MAX_SHADOW_LIGHTS
#define MAX_SHADOW_LIGHTS 12
// Must match 1 / ShadowMaps::POINT_RESOLUTION_DIV: point faces only fill the
// lower left part of their layer
#define POINT_SHADOW_SCALE 0.25

layout(std140) uniform ShadowBlock {
    mat4 uLightSpace[MAX_SHADOW_LIGHTS]; // world -> light clip space per layer
};

layout(std140) uniform Lights {
    GpuLight uLights[MAX_LIGHTS];
    vec4 uViewPos;
    ivec4 uLightCount; // x = number of lights in use
};

// Unpacked light, with the vec4 channels given proper names
struct Light {
    int type;
    vec3 position;
    vec3 direction;
    vec3 color;
    float ambient;
    vec3 attenuation;
    int shadowIndex; // layer in uShadowMaps, -1 = casts no shadow
    int closeUpIndex; // directional: layer of the close-up (houses) map, -1 = none
    float cutOff;
    float outerCutOff;
};

Light getLight(int i) {
    GpuLight gpu = uLights[i];
    Light light;
    light.type = int(gpu.position.w + 0.5);
    light.position = gpu.position.xyz;
    light.direction = gpu.direction.xyz;
    light.color = gpu.color.rgb;
    light.ambient = gpu.color.w;
    light.attenuation = gpu.attenuation.xyz;
    light.shadowIndex = int(floor(gpu.shadow.x + 0.5)); // rounds -1 to -1
    light.closeUpIndex = int(floor(gpu.shadow.y + 0.5));
    light.cutOff = gpu.cutOff.x; // inner cutoff cos
    light.outerCutOff = gpu.cutOff.y; // outer cutoff cos
    return light;
}

// ---------------------------------------------------------------------------
// Surface: everything about this fragment, after sampling the material's maps
// ---------------------------------------------------------------------------

struct Surface {
    vec3 position;
    vec3 normal;
    vec3 albedo;
    vec3 specular;
    float shininess;
    float roughness;
};

// Surface normal, perturbed by the normal map when there is one
vec3 getNormal() {
    // Double sided materials: the back side faces the other way
    vec3 N = normalize(gl_FrontFacing ? FragNormal : -FragNormal);

    // Meshes without UVs have no tangent, so the map can't be oriented
    bool hasTangent = dot(FragTangent, FragTangent) > 1e-8;
    if (!uHasNormalMap || !hasTangent)
        return N;

    // Gram-Schmidt: make T perpendicular to N again after interpolation
    vec3 T = normalize(FragTangent - dot(FragTangent, N) * N);
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    // [0, 1] texture values -> [-1, 1] tangent space direction
    vec3 tangentNormal = texture(uNormalTex, FragTexCoord).rgb * 2.0 - 1.0;
    return normalize(TBN * tangentNormal);
}

Surface getSurface() {
    Surface surf;
    surf.position = FragPos;
    surf.normal = getNormal();
    vec4 albedo = texture(uAlbedoTex, FragTexCoord);
    // Alpha-masked cards (tree leaves)
    if (albedo.a < 0.5)
        discard;
    surf.albedo = albedo.rgb;
    surf.specular = uMaterial.specular;
    surf.shininess = uMaterial.shininess;
    surf.roughness = uRoughness;

    if (uHasSpecularMap)
        surf.specular *= texture(uSpecularTex, FragTexCoord).rgb;

    if (uHasRoughnessMap) {
        // G channel: glTF packs roughness there, and a grayscale roughness
        // map has it in every channel anyway
        float roughness = texture(uRoughnessTex, FragTexCoord).g;

        // Rough -> wide highlight (exponent 2), smooth -> tight (2048)
        surf.shininess = exp2(1.0 + 10.0 * (1.0 - roughness));
        // Fade the highlight out as the surface gets rougher, otherwise
        // rough surfaces get a big bright smear
        surf.specular *= 1.0 - roughness;
    }

    return surf;
}

// ---------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------

// clip space -> [0, 1] texture space of a layer
vec3 shadowCoords(int layer, vec3 position) {
    vec4 p = uLightSpace[layer] * vec4(position, 1.0);
    return p.xyz / p.w * 0.5 + 0.5;
}

// 3x3 PCF on one layer of a sun / spot shadow map, one texel apart
float shadowPcf(int layer, vec3 c) {
    if (c.z > 1.0)
        return 1.0; // behind the far plane
    vec2 texel = 1.0 / vec2(textureSize(uShadowMaps, 0).xy);
    float lit = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            lit += texture(uShadowMaps, vec4(c.xy + vec2(x, y) * texel,
                        float(layer), c.z));
    return lit / 9.0;
}

// 1 = lit, 0 = in shadow
float shadowFactor(Light light, Surface surf, vec3 lightDir) {
    if (light.shadowIndex < 0)
        return 1.0;

    if (light.type == LIGHT_POINT) {
        // One layer per cube face (+X -X +Y -Y +Z -Z)
        vec3 d = surf.position - light.position;
        vec3 a = abs(d);
        int face = (a.x >= a.y && a.x >= a.z) ? (d.x > 0.0 ? 0 : 1) : (a.y >= a.z) ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5);
        int layer = light.shadowIndex + face;
        vec3 c = shadowCoords(layer, surf.position);
        if (c.z > 1.0)
            return 1.0;
        // Into the used square, kept half a texel inside so the filter never
        // reads the unused part of the layer
        float res = float(textureSize(uShadowMaps, 0).x) * POINT_SHADOW_SCALE;
        vec2 uv = clamp(c.xy, 0.5 / res, 1.0 - 0.5 / res) * POINT_SHADOW_SCALE;
        return texture(uShadowMaps, vec4(uv, float(layer), c.z));
    }

    float lit = shadowPcf(light.shadowIndex,
            shadowCoords(light.shadowIndex, surf.position));

    // Sun: the houses are only in the close-up map, so both shadows apply.
    // Outside its box the close-up map reads as lit.
    if (light.closeUpIndex >= 0)
        lit *= shadowPcf(light.closeUpIndex,
                shadowCoords(light.closeUpIndex, surf.position));
    return lit;
}

// Phong ambient + diffuse + specular for light arriving from lightDir
// (unit vector pointing from the surface towards the light). shadow scales
// the direct light only, ambient is never shadowed.
vec3 phong(Light light, Surface surf, vec3 lightDir, vec3 viewDir, float shadow) {
    float diff = max(dot(surf.normal, lightDir), 0.0);

    // No highlight on faces turned away from the light
    float spec = 0.0;
    if (diff > 0.0) {
        vec3 reflectDir = reflect(-lightDir, surf.normal);
        spec = pow(max(dot(viewDir, reflectDir), 0.0), max(surf.shininess, 1.0));
    }

    vec3 ambient = light.ambient * surf.albedo;
    vec3 diffuse = diff * surf.albedo;
    vec3 specular = spec * surf.specular;
    return light.color * (ambient + shadow * (diffuse + specular));
}

vec3 directionalLight(Light light, Surface surf, vec3 viewDir) {
    vec3 lightDir = normalize(-light.direction);
    float shadow = shadowFactor(light, surf, lightDir);
    return phong(light, surf, lightDir, viewDir, shadow);
}

// The further away, the weaker the light
float attenuation(Light light, vec3 position) {
    float d = length(light.position - position);
    return 1.0 / (light.attenuation.x + light.attenuation.y * d +
            light.attenuation.z * d * d);
}

vec3 pointLight(Light light, Surface surf, vec3 viewDir) {
    vec3 lightDir = normalize(light.position - surf.position);
    float shadow = shadowFactor(light, surf, lightDir);
    return phong(light, surf, lightDir, viewDir, shadow) *
        attenuation(light, surf.position);
}

vec3 spotLight(Light light, Surface surf, vec3 viewDir) {
    vec3 lightDir = normalize(light.position - surf.position);

    // 1 inside the inner cone, fading to 0 at the outer one
    float theta = dot(lightDir, normalize(-light.direction));
    float cone = clamp((theta - light.outerCutOff) /
                (light.cutOff - light.outerCutOff), 0.0, 1.0);

    // Outside the cone only the ambient part is left
    float shadow = cone * shadowFactor(light, surf, lightDir);
    return phong(light, surf, lightDir, viewDir, shadow) *
        attenuation(light, surf.position);
}

// ---------------------------------------------------------------------------

void main() {
    Surface surf = getSurface();
    vec3 viewDir = normalize(uViewPos.xyz - surf.position);

    vec3 result = vec3(0.0);
    for (int i = 0; i < uLightCount.x; i++) {
        Light light = getLight(i);
        if (light.type == LIGHT_DIRECTIONAL)
            result += directionalLight(light, surf, viewDir);
        else if (light.type == LIGHT_POINT)
            result += pointLight(light, surf, viewDir);
        else if (light.type == LIGHT_SPOT)
            result += spotLight(light, surf, viewDir);
    }

    // Sky reflection with Fresnel
    if (uReflectivity > 0.0) {
        vec3 reflectDir = reflect(-viewDir, surf.normal); // reflection from camera view
        float lod = surf.roughness * 8.0; // 0 = sharp sky, 8 = very blurry
        vec3 envColor = textureLod(uSkybox, reflectDir, lod).rgb * uSkyTint; // read from blurred mip level

        // Schlick Fresnel
        float NdotV = max(dot(surf.normal, viewDir), 0.0); // if its 1 then its 90angles direction => fresnel weak
        float fresnel = uReflectivity + (1.0 - uReflectivity) * pow(1.0 - NdotV, 5.0);
        float amount = fresnel * (1.0 - surf.roughness); // rough surfaces reflect less
        result = mix(result, envColor, amount);
    }

    // Glow (hot coal, metal, sparks): not lit, so it shows in the dark too
    if (uHasEmissiveMap)
        result += texture(uEmissiveTex, FragTexCoord).rgb * uEmissiveStrength;

    FragColor = vec4(result, 1.0);
}
