#version 330 core
// Water surface: a few Gerstner waves running with the wind, then lit.fs
// lights it like any other surface. A Gerstner wave also pulls the surface
// toward its crest, so crests are sharp and troughs wide, unlike a sine's.
// The mesh must be dense (MC_Water.glb, ~2 m triangles): waves shorter than
// a few triangles can't show.
layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
// Location 7, since the instance transform takes 3-6
layout(location = 7) in vec3 Tangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uTime; // seconds, render::Material::time

out vec2 FragTexCoord;
out vec3 FragNormal;
out vec3 FragPos;
out vec3 FragTangent;

// The waves: direction (radians from +x, around the wind), wavelength and
// height, metres. Must match WAVES in demo_scene.cpp, the boats ride them.
const int WAVES = 4;
const vec3 waves[WAVES] = vec3[](
    vec3(0.25, 37.0, 0.16),
    vec3(0.80, 23.0, 0.10),
    vec3(-0.20, 14.0, 0.06),
    vec3(1.30, 9.0, 0.035));
// How far a crest pulls the surface in, as a part of the wave's height
const float PINCH = 0.7;

void main() {
    vec3 world = vec3(uModel * vec4(Position, 1.0));

    vec3 offset = vec3(0.0);
    vec2 slope = vec2(0.0); // dh/dx, dh/dz
    float squeeze = 0.0;    // how much the surface is pulled together here
    for (int i = 0; i < WAVES; i++) {
        vec2 d = vec2(cos(waves[i].x), sin(waves[i].x));
        float k = 6.2831853 / waves[i].y;
        float a = waves[i].z;
        // Deep water: omega = sqrt(g k), long waves travel faster
        float phase = k * dot(d, world.xz) - sqrt(9.81 * k) * uTime;
        offset.y += a * sin(phase);
        offset.xz += PINCH * a * cos(phase) * d;
        slope += a * k * cos(phase) * d;
        squeeze += PINCH * a * k * sin(phase);
    }

    FragPos = world + offset;
    // Gerstner normal: the squeeze stands the crests up
    FragNormal = normalize(vec3(-slope.x, 1.0 - squeeze, -slope.y));
    // Along +x the surface rises by dh/dx
    FragTangent = normalize(vec3(1.0 - squeeze, slope.x, 0.0));
    FragTexCoord = TexCoord;

    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
