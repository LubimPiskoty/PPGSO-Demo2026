#version 430 core
// Shows the thin G-buffer (render::PostProcessing), the whole view in each
// quarter of the screen:
//
//   lit scene (for reference) | world-space normals
//   --------------------------+---------------------------
//   Forward+ lights per tile  | distance from the camera
//
// Normals: x red, y green, z blue, -1..1 as 0..1. Lights per tile: the
// opaque list's length of each Forward+ tile (render::LightCulling), blue
// (none) through green to red (HOT and more), the sky's tiles too.
// Distance: black at the camera, white at FAR, on a log scale so the near
// metres don't all look the same. The sky (no depth) shows the lit scene in
// the other quarters. Layer in main.cpp's LAYERS.

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform mat4 uInvProjection; // view space from clip space

// The tiles' lists, as forward_plus.comp left them this frame
layout(std430, binding = 5) readonly buffer TileLights {
    ivec4 uTileInfo; // x = tiles across, y = tile size, z = lights per tile,
                     // w = where the transparent lists start
    int uTileLights[];
};

// Lights in a tile shown red
const float HOT = 48.0;
// Distance shown white, m
const float FAR = 500.0;

vec3 viewPos(vec2 uv) {
    float depth = texture(uDepth, uv).r;
    vec4 p = uInvProjection * vec4(vec3(uv, depth) * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

// Blue (none) through green to red (HOT), the number of lights in the
// tile under uv (0..1 over the whole screen)
vec3 heat(vec2 uv) {
    ivec2 tile = ivec2(uv * vec2(textureSize(uDepth, 0))) / uTileInfo.y;
    int count = uTileLights[(tile.y * uTileInfo.x + tile.x) * (uTileInfo.z + 1)];
    float t = clamp(float(count) / HOT, 0.0, 1.0);
    return t < 0.5 ? mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), t * 2.0)
            : mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), t * 2.0 - 1.0);
}

void main() {
    // Which quarter (0 or 1 across, 0 or 1 up), and where in the view
    ivec2 quarter = ivec2(vUV * 2.0);
    vec2 uv = fract(vUV * 2.0);

    vec3 scene = texture(uScreen, uv).rgb;
    bool top = quarter.y == 1, left = quarter.x == 0;
    if (!top && left) {
        FragColor = vec4(heat(uv), 1.0);
        return;
    }
    // Nothing drawn here (the sky), or the reference quarter
    if ((top && left) || texture(uDepth, uv).r >= 1.0) {
        FragColor = vec4(scene, 1.0);
        return;
    }
    if (top) {
        vec3 n = texture(uNormal, uv).xyz;
        n = dot(n, n) > 0.0 ? normalize(n) : n;
        FragColor = vec4(n * 0.5 + 0.5, 1.0);
    } else {
        float distance = length(viewPos(uv));
        FragColor = vec4(vec3(clamp(log(1.0 + distance) / log(1.0 + FAR),
                        0.0, 1.0)),
                1.0);
    }
}
