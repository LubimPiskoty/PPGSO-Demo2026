#version 330 core

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

in vec2 FragTexCoord;
in vec3 FragNormal;
in vec3 FragPos;
in vec3 FragTangent;

// To the color texture; in the prepass (prepass.fs) output 1 is the normals
layout(location = 0) out vec4 FragColor;

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
// BlendMode::Transparent (render::Material): blended over what's behind,
// alpha the albedo's times uMaterial.opacity, not cut out at 0.5
uniform bool uTransparent;
// Water: the deeper it is under the surface, the less see-through. Per m of
// water down to what's opaque behind it (uSceneDepth, the opaque pass's
// depth, PostProcessing::resolveDepth); 0 off, the rest of the materials.
uniform float uDepthFade;
uniform sampler2D uSceneDepth; // unit 8
uniform mat4 uProjection; // the vertex shader's, for the distances

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
uniform sampler2DArrayShadow uShadowMaps; // the suns' depth maps, slot 5
uniform sampler2DArrayShadow uSmallShadowMaps; // spot / point maps, slot 7
uniform samplerCube uSkybox; // cubemap of skybox slot 4
uniform float uReflectivity; // 0 no reflect, 1 mirror
uniform vec3 uSkyTint; // the skybox's tint: dark at night, so is its reflection
uniform float uRoughness; // when mat doesnt have roughmap

// ---------------------------------------------------------------------------
// Lights (layout matches render::LightBlock)
// ---------------------------------------------------------------------------

// Must match MAX_LIGHTS in light_buffer.hpp, or the block layouts disagree
#define MAX_LIGHTS 160

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT        2

// One light as uploaded, matches render::GpuLight
struct GpuLight {
    vec4 position; // xyz = position, w = type
    vec4 direction; // xyz = direction
    vec4 color; // rgb = color, w = ambient strength
    vec4 attenuation; // x = constant, y = linear, z = quadratic
    vec4 shadow; // x = shadow map layer in its array, -1 = none
    // y = directional: close-up map layer, -1 = none
    vec4 cutOff; // x = inner cutoff, y = outer cutoff, z = shadow texel
    // size, w = 1: z is per metre away from the light
};

// Normal offset of the shadow lookups, in map texels: always, and more on
// surfaces the light grazes
#define SHADOW_NORMAL_OFFSET 1.0
#define SHADOW_GRAZING_OFFSET 1.5
// The water's surface (MC_Water, its node 1 m down, demo_scene.cpp; the same
// in ssao.fs and tools/drape_ground.py)
#define WATER_LEVEL -5.29
// No shadows below this: under the water's lowest troughs (water.vs's waves
// sum to about 0.36 m), so nothing a trough uncovers loses its shadow
#define NO_SHADOW_BELOW (WATER_LEVEL - 0.5)

// Must match ShadowMaps::MAX_SUN_LAYERS / MAX_SMALL_LAYERS
#define MAX_SUN_SHADOWS 4
#define MAX_SMALL_SHADOWS 120

layout(std140) uniform ShadowBlock {
    // world -> light clip space per layer, of uShadowMaps / uSmallShadowMaps
    mat4 uLightSpace[MAX_SUN_SHADOWS];
    mat4 uSmallLightSpace[MAX_SMALL_SHADOWS];
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
    int shadowIndex; // first layer of its map(s), -1 = casts no shadow
    bool smallMap; // in uSmallShadowMaps, else uShadowMaps
    int closeUpIndex; // directional: layer of the close-up (houses) map, -1 = none
    float cutOff;
    float outerCutOff;
    float shadowTexel; // metres (per metre away if texelPerMetre)
    bool texelPerMetre;
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
    light.smallMap = gpu.shadow.z > 0.5;
    light.cutOff = gpu.cutOff.x; // inner cutoff cos
    light.outerCutOff = gpu.cutOff.y; // outer cutoff cos
    light.shadowTexel = gpu.cutOff.z;
    light.texelPerMetre = gpu.cutOff.w > 0.5;
    return light;
}

// ---------------------------------------------------------------------------
// Surface: everything about this fragment, after sampling the material's maps
// ---------------------------------------------------------------------------

struct Surface {
    vec3 position;
    vec3 normal;
    vec3 faceNormal; // without the normal map
    vec3 albedo;
    vec3 specular;
    float shininess;
    float roughness;
    float alpha; // 1 unless transparent
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
    surf.faceNormal = normalize(gl_FrontFacing ? FragNormal : -FragNormal);
    vec4 albedo = texture(uAlbedoTex, FragTexCoord);
    // Alpha-masked cards (tree leaves, the corn's tassels); prepass.fs cuts
    // the same. Transparent ones blend instead, fully clear texels dropped.
    surf.alpha = uTransparent ? albedo.a * uMaterial.opacity : 1.0;
    if (uTransparent ? surf.alpha < 0.01 : albedo.a < 0.5)
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
vec3 shadowCoords(mat4 lightSpace, vec3 position) {
    vec4 p = lightSpace * vec4(position, 1.0);
    return p.xyz / p.w * 0.5 + 0.5;
}

// 3x3 PCF on one layer of a shadow map, one texel apart
float shadowPcf(sampler2DArrayShadow maps, int layer, vec3 c) {
    if (c.z > 1.0)
        return 1.0; // behind the far plane
    vec2 texel = 1.0 / vec2(textureSize(maps, 0).xy);
    float lit = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            lit += texture(maps, vec4(c.xy + vec2(x, y) * texel,
                        float(layer), c.z));
    return lit / 9.0;
}

// 1 = lit, 0 = in shadow
float shadowFactor(Light light, Surface surf, vec3 lightDir) {
    // Under the water no shadows: they showed on the sea floor through it
    if (light.shadowIndex < 0 || surf.position.y < NO_SHADOW_BELOW)
        return 1.0;

    // Normal offset: the depth is looked up a little out from the surface,
    // more where the light grazes it, so a surface never meets its own depth
    // in the map (the shadow pass draws front faces)
    float texel = light.shadowTexel;
    if (light.texelPerMetre)
        texel *= length(surf.position - light.position);
    float grazing = 1.0 - max(dot(surf.faceNormal, lightDir), 0.0);
    surf.position += surf.faceNormal * texel * (SHADOW_NORMAL_OFFSET +
                SHADOW_GRAZING_OFFSET * grazing);

    if (light.type == LIGHT_POINT) {
        // One layer per cube face (+X -X +Y -Y +Z -Z), in the small maps
        vec3 d = surf.position - light.position;
        vec3 a = abs(d);
        int face = (a.x >= a.y && a.x >= a.z) ? (d.x > 0.0 ? 0 : 1) : (a.y >= a.z) ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5);
        int layer = light.shadowIndex + face;
        vec3 c = shadowCoords(uSmallLightSpace[layer], surf.position);
        if (c.z > 1.0)
            return 1.0;
        // Kept half a texel inside, so the filter never reads the next face's
        // edge from over the border
        float res = float(textureSize(uSmallShadowMaps, 0).x);
        vec2 uv = clamp(c.xy, 0.5 / res, 1.0 - 0.5 / res);
        return texture(uSmallShadowMaps, vec4(uv, float(layer), c.z));
    }

    if (light.smallMap) // spot
        return shadowPcf(uSmallShadowMaps, light.shadowIndex,
            shadowCoords(uSmallLightSpace[light.shadowIndex], surf.position));

    float lit = shadowPcf(uShadowMaps, light.shadowIndex,
            shadowCoords(uLightSpace[light.shadowIndex], surf.position));

    // Sun: the houses are only in the close-up map, so both shadows apply.
    // Outside its box the close-up map reads as lit.
    if (light.closeUpIndex >= 0)
        lit *= shadowPcf(uShadowMaps, light.closeUpIndex,
                shadowCoords(uLightSpace[light.closeUpIndex], surf.position));
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

// Distance in front of the camera, m, of a depth buffer value (0..1)
float eyeDepth(float depth) {
    return uProjection[3][2] / (depth * 2.0 - 1.0 + uProjection[2][2]);
}

// ---------------------------------------------------------------------------

void main() {
    Surface surf = getSurface();
    vec3 viewDir = normalize(uViewPos.xyz - surf.position);

    vec3 result = vec3(0.0);
    for (int i = 0; i < uLightCount.x; i++) {
        Light light = getLight(i);
        // Off (a lamp by day): nothing to add, skip its shadow lookups
        if (light.color == vec3(0.0))
            continue;
        if (light.type == LIGHT_DIRECTIONAL)
            result += directionalLight(light, surf, viewDir);
        else if (light.type == LIGHT_POINT)
            result += pointLight(light, surf, viewDir);
        else if (light.type == LIGHT_SPOT)
            result += spotLight(light, surf, viewDir);
    }

    // Water: by the stretch of the view ray between the surface and what's
    // behind it, from its opacity in the shallows to opaque where it's deep,
    // and its own colour darker (light gets lost on the way down and back).
    // Before the sky's reflection, so the waves keep theirs.
    if (uTransparent && uDepthFade > 0.0) {
        const float DEEPEST = 0.15; // the water's own colour when deep
        float behind = texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r;
        // Clamped: with MSAA a pixel the surface only partly covers is
        // shaded at its middle, off the triangle, and at grazing angles the
        // depth there can run past the far plane: a negative distance, the
        // alpha went hugely negative and the blend left black specks
        float here = max(eyeDepth(clamp(gl_FragCoord.z, 0.0, 1.0)), 0.01);
        float through = max(eyeDepth(behind) - here, 0.0) *
                length(uViewPos.xyz - surf.position) / here;
        float clear = clamp(exp(-through * uDepthFade), 0.0, 1.0);
        surf.alpha = clamp(mix(1.0, surf.alpha, clear), 0.0, 1.0);
        result *= mix(DEEPEST, 1.0, clear);
    }

    // Sky reflection with Fresnel
    if (uReflectivity > 0.0) {
        vec3 reflectDir = reflect(-viewDir, surf.normal); // reflection from camera view
        float lod = surf.roughness * 8.0; // 0 = sharp sky, 8 = very blurry
        vec3 envColor = textureLod(uSkybox, reflectDir, lod).rgb * uSkyTint; // read from blurred mip level

        // Schlick Fresnel
        // Clamped to 1 too: rounding can give 1.0000001, and pow() of a
        // negative 1 - NdotV below would be NaN
        float NdotV = clamp(dot(surf.normal, viewDir), 0.0, 1.0); // if its 1 then its 90angles direction => fresnel weak
        float fresnel = uReflectivity + (1.0 - uReflectivity) * pow(1.0 - NdotV, 5.0);
        float amount = fresnel * (1.0 - surf.roughness); // rough surfaces reflect less
        result = mix(result, envColor, amount);
    }

    // Glow (hot coal, metal, sparks): not lit, so it shows in the dark too
    if (uHasEmissiveMap)
        result += texture(uEmissiveTex, FragTexCoord).rgb * uEmissiveStrength;

    // Well under what the half float frame holds (65504): a sun glint past
    // that turns infinite, and the blurs and the bloom spread it
    result = min(result, vec3(1000.0));

    FragColor = vec4(result, surf.alpha);
}
