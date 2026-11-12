#version 330 core
// Shows the thin G-buffer (render::PostProcessing), the whole view in each
// quarter of the screen:
//
//   lit scene (for reference) | world-space normals
//   --------------------------+---------------------------
//   world positions as colour | distance from the camera
//
// Normals: x red, y green, z blue, -1..1 as 0..1. Positions: world space,
// each axis repeating every TILE metres as 0..1, x red, y green, z blue; a
// spot keeps its colour as the camera moves if the rebuilding is right. Distance: black at the camera, white at FAR,
// on a log scale so the near metres don't all look the same. The sky (no
// depth) shows the lit scene in every quarter. Layer in main.cpp's LAYERS.

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform mat4 uInvProjection; // view space from clip space
uniform mat4 uInvView; // world from view space (the camera's)

// The positions' colours repeat every this many metres
const float TILE = 100.0;
// Distance shown white, m
const float FAR = 500.0;

vec3 viewPos(vec2 uv) {
    float depth = texture(uDepth, uv).r;
    vec4 p = uInvProjection * vec4(vec3(uv, depth) * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

vec3 worldPos(vec2 uv) {
    return (uInvView * vec4(viewPos(uv), 1.0)).xyz;
}

void main() {
    // Which quarter (0 or 1 across, 0 or 1 up), and where in the view
    ivec2 quarter = ivec2(vUV * 2.0);
    vec2 uv = fract(vUV * 2.0);

    vec3 scene = texture(uScreen, uv).rgb;
    bool top = quarter.y == 1, left = quarter.x == 0;
    // Nothing drawn here (the sky), or the reference quarter
    if ((top && left) || texture(uDepth, uv).r >= 1.0) {
        FragColor = vec4(scene, 1.0);
        return;
    }
    if (top) {
        vec3 n = texture(uNormal, uv).xyz;
        n = dot(n, n) > 0.0 ? normalize(n) : n;
        FragColor = vec4(n * 0.5 + 0.5, 1.0);
    } else if (left) {
        FragColor = vec4(fract(worldPos(uv) / TILE), 1.0);
    } else {
        float distance = length(viewPos(uv));
        FragColor = vec4(vec3(clamp(log(1.0 + distance) / log(1.0 + FAR),
                        0.0, 1.0)),
                1.0);
    }
}
