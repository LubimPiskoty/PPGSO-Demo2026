#version 330 core
// https://learnopengl.com/Advanced-Lighting/SSAO
// Screen-space ambient occlusion in two runs of this layer (main.cpp's
// LAYERS, repeats 2):
//   uPass 0: how occluded each pixel is, 1 open .. 0 closed in, as grey
//   uPass 1: that blurred over 4x4 pixels (evens out the noise's pattern),
//            the image the layer started from darkened by it
// The positions come from the depth (viewPos), the normals from the thin
// G-buffer (world space, turned into view space here): the same view-space
// position and normal the tutorial reads from its G-buffer. The sample
// kernel and the noise come from render::Ssao. The scene is drawn forward,
// so there's no ambient term apart: the whole colour is darkened.

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen; // run 0: the scene, run 1: run 0's occlusion
uniform sampler2D uLayerInput; // the image before this layer
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform sampler2D uNoise; // 4x4 turns about the normal, repeating
uniform int uPass;

uniform mat4 uProjection; // view space to clip space
uniform mat4 uInvProjection; // view space from clip space
uniform mat4 uInvView; // world from view space (the camera's)

const int KERNEL_SIZE = 64; // must match render::Ssao::KERNEL_SIZE
uniform vec3 uSamples[KERNEL_SIZE]; // the hemisphere, tangent space

const float RADIUS = 1.50; // m around a point that can occlude it
// How much of the occlusion darkens the image: 1 all of it. The tutorial
// darkens only the ambient light; here it's the whole colour (sunlit too),
// so less than 1 keeps sunny dense grass from going dark.
const float STRENGTH = 1.6;
const float BIAS = 0.025; // m, so a surface doesn't occlude itself
// Far away the depth is too coarse to tell creases from noise: the
// occlusion fades out between these distances, m
const float FADE_START = 60.0;
const float FADE_END = 120.0;

vec3 viewPos(vec2 uv) {
    float depth = texture(uDepth, uv).r;
    vec4 p = uInvProjection * vec4(vec3(uv, depth) * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

float occlusion() {
    // Nothing drawn here (the sky), or nothing in the prepass: open
    vec3 normalWorld = texture(uNormal, vUV).xyz;
    if (texture(uDepth, vUV).r >= 1.0 || dot(normalWorld, normalWorld) == 0.0)
        return 1.0;
    vec3 fragPos = viewPos(vUV);
    float distance = length(fragPos);
    if (distance > FADE_END)
        return 1.0;
    // The camera's rotation is orthonormal: its transpose turns world into
    // view space
    vec3 normal = normalize(transpose(mat3(uInvView)) * normalWorld);

    // Tangent space around the normal, turned by the noise
    vec2 noiseScale = vec2(textureSize(uDepth, 0)) / 4.0;
    vec3 randomVec = normalize(texture(uNoise, vUV * noiseScale).xyz);
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occluded = 0.0;
    for (int i = 0; i < KERNEL_SIZE; i++) {
        // The sample in view space, then where it lands on the screen
        vec3 samplePos = fragPos + TBN * uSamples[i] * RADIUS;
        vec4 offset = uProjection * vec4(samplePos, 1.0);
        offset.xy = offset.xy / offset.w * 0.5 + 0.5;
        // What's drawn there: in front of the sample, it occludes; only
        // what's within RADIUS counts (no dark halos round a silhouette)
        float sampleDepth = viewPos(offset.xy).z;
        float rangeCheck =
            smoothstep(0.0, 1.0, RADIUS / abs(fragPos.z - sampleDepth));
        occluded += (sampleDepth >= samplePos.z + BIAS ? 1.0 : 0.0) * rangeCheck;
    }
    float open = 1.0 - occluded / float(KERNEL_SIZE);
    return mix(open, 1.0, smoothstep(FADE_START, FADE_END, distance));
}

void main() {
    if (uPass == 0) {
        FragColor = vec4(vec3(occlusion()), 1.0);
        return;
    }
    // The noise repeats every 4 pixels: averaging 4x4 pixels evens it out
    vec2 texel = 1.0 / vec2(textureSize(uScreen, 0));
    float ao = 0.0;
    for (int x = -2; x < 2; x++)
        for (int y = -2; y < 2; y++)
            ao += texture(uScreen, vUV + vec2(x, y) * texel).r;
    ao = mix(1.0, ao / 16.0, STRENGTH);
    FragColor = vec4(texture(uLayerInput, vUV).rgb * ao, 1.0);
}
