#version 330 core
// Height fog: haze thick near the ground, thinning out upward (its density
// falls by e every FOG_FALLOFF metres over FOG_BASE). The fog along a pixel's
// ray is that density summed from the camera to what's there, so looking
// down from high up (the timelapse) goes through little of it and low views
// along the ground fade far things out. Its colour is the skybox in the
// pixel's direction (raised to the horizon when it looks down, blurred a
// little), tinted like the sky: the far terrain meets the horizon without a
// seam. The sky itself is left as it is.

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;
uniform sampler2D uDepth;
uniform samplerCube uSkybox; // unit 4 (main.cpp)
uniform mat4 uInvProjection; // view space from clip space
uniform mat4 uInvView; // world space from view space
uniform vec3 uSkyTint; // the skybox's tint, 0: no skybox, no fog

const float FOG_DISTANCE = 450.0; // m along the ground to about 63 % fogged
const float FOG_BASE = 0.0; // m, height of the full density (the city)
const float FOG_FALLOFF = 30.0; // m up for the density to fall by e
const float FOG_MAX = 0.9; // the most it covers
const float SKY_BLUR = 4.0; // mip level the fog's colour is read at

void main() {
    vec3 color = texture(uScreen, vUV).rgb;
    float depth = texture(uDepth, vUV).r;
    if (depth >= 1.0 || uSkyTint == vec3(0.0)) {
        FragColor = vec4(color, 1.0);
        return;
    }
    vec4 p = uInvProjection * vec4(vec3(vUV, depth) * 2.0 - 1.0, 1.0);
    vec3 view = p.xyz / p.w;
    vec3 camera = uInvView[3].xyz;
    vec3 ray = mat3(uInvView) * view; // camera to the pixel, world space
    float len = length(ray);

    // Density at the camera, then integrated along the ray: the exponential
    // falloff gives (1 - e^-k) / k for the climb k (FOG_FALLOFFs) along it
    float atCamera = exp(-(camera.y - FOG_BASE) / FOG_FALLOFF);
    float k = ray.y / FOG_FALLOFF;
    float along = abs(k) > 1e-4 ? (1.0 - exp(-k)) / k : 1.0;
    float amount = atCamera * along * len / FOG_DISTANCE;
    // Squared: next to nothing close by (5 % at a third of FOG_DISTANCE),
    // then it closes in
    float fog = (1.0 - exp(-amount * amount)) * FOG_MAX;

    vec3 dir = normalize(ray);
    dir = normalize(vec3(dir.x, max(dir.y, 0.02), dir.z));
    vec3 sky = textureLod(uSkybox, dir, SKY_BLUR).rgb * uSkyTint;
    FragColor = vec4(mix(color, sky, fog), 1.0);
}
