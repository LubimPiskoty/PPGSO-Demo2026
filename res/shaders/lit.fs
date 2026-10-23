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

uniform bool uHasNormalMap;
uniform bool uHasRoughnessMap;
uniform bool uHasSpecularMap;
uniform sampler2DArrayShadow uShadowMaps; // depth array, slot 5
uniform samplerCube uSkybox;     // cubemap of skybox slot 4
uniform float uReflectivity;     // 0 no reflect, 1 mirror
uniform float uRoughness; // when mat doesnt have roughmap

// ---------------------------------------------------------------------------
// Lights (layout matches render::LightBlock)
// ---------------------------------------------------------------------------

// Must match MAX_LIGHTS in light_buffer.hpp, or the block layouts disagree
#define MAX_LIGHTS 64

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT        2
#define LIGHT_SPOT        2

// One light as uploaded, matches render::GpuLight
struct GpuLight {
    vec4 position; // xyz = position, w = type
    vec4 direction; // xyz = direction
    vec4 color; // rgb = color, w = ambient strength
    vec4 attenuation; // x = constant, y = linear, z = quadratic
    vec4 shadow; // x = shadow map layer / ShadowBlock index, -1 = none
};

// Must match ShadowMaps::MAX_SHADOW_LIGHTS
#define MAX_SHADOW_LIGHTS 12

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
    vec3 N = normalize(FragNormal);

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
    surf.albedo = texture(uAlbedoTex, FragTexCoord).rgb;
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

// 1 = lit, 0 = in shadow (hardware filtered between the two at the edges)
float shadowFactor(Light light, Surface surf, vec3 lightDir) {
    if (light.shadowIndex < 0)
        return 1.0;

    int layer = light.shadowIndex;
    if (light.type == 1) { // point: layer + cube face (+X -X +Y -Y +Z -Z)
        vec3 d = surf.position - light.position;
        vec3 a = abs(d);
        int face = (a.x >= a.y && a.x >= a.z) ? (d.x > 0.0 ? 0 : 1)
                 : (a.y >= a.z)               ? (d.y > 0.0 ? 2 : 3)
                 :                              (d.z > 0.0 ? 4 : 5);
        layer += face;
    }

    vec4 p = uLightSpace[layer] * vec4(surf.position, 1.0);
    vec3 c = p.xyz / p.w * 0.5 + 0.5; // clip space -> [0, 1] texture space
    if (c.z > 1.0)
        return 1.0; // behind the far plane

    // Surfaces at a steep angle to the light need more bias
    float bias = max(0.005 * (1.0 - dot(surf.normal, lightDir)), 0.0005);
    return texture(uShadowMaps, vec4(c.xy, float(layer), c.z - bias));
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

vec3 pointLight(Light light, Surface surf, vec3 viewDir) {
    vec3 toLight = light.position - surf.position;
    vec3 lightDir = normalize(toLight);

    // The further away, the weaker the light
    float d = length(toLight);
    float attenuation = 1.0 / (light.attenuation.x +
                light.attenuation.y * d +
                light.attenuation.z * d * d);

    float shadow = shadowFactor(light, surf, lightDir);
    return phong(light, surf, lightDir, viewDir, shadow) * attenuation;
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
        // TODO: LIGHT_SPOT
    }

    // Sky reflection with Fresnel
    if (uReflectivity > 0.0) {
        vec3 reflectDir = reflect(-viewDir, surf.normal); // reflection from camera view
        float lod       = surf.roughness * 8.0; // 0 = sharp sky, 8 = very blurry
        vec3 envColor   = textureLod(uSkybox, reflectDir, lod).rgb; // read from blurred mip level

        // Schlick Fresnel
        float NdotV   = max(dot(surf.normal, viewDir), 0.0); // if its 1 then its 90angles direction => fresnel weak
        float fresnel = uReflectivity + (1.0 - uReflectivity) * pow(1.0 - NdotV, 5.0);
        float amount = fresnel * (1.0 - surf.roughness);   // rough surfaces reflect less
        result = mix(result, envColor, amount);
    }


    FragColor = vec4(result, 1.0);
}
