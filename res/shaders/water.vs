#version 330 core
// Water surface: height = sum of sine waves, then lit.fs lights it like any
// other surface. The mesh must be dense (MC_Water.glb, ~2 m triangles):
// waves shorter than a few triangles can't show.
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

const float PI = 3.14159265;

void main() {
    vec3 world = vec3(uModel * vec4(Position, 1.0));

    // y = sum of sines. Each wave is shorter and lower than the one before
    // and turned 2.4 rad, so they never line up into a repeating pattern.
    float height = 0.0;
    vec2 slope = vec2(0.0); // dh/dx, dh/dz, for the normal
    float wavelength = 200.0, amplitude = 0.25, angle = 0.25;
    for (int i = 0; i < 12; i++) {
        vec2 d = vec2(cos(angle), sin(angle));
        float k = 2.0 * PI / wavelength;
        // Deep water: omega = sqrt(g k), long waves are slow to rise and fall
        float phase = k * dot(d, world.xz) - sqrt(9.81 * k) * uTime;
        height += amplitude * sin(phase);
        slope += amplitude * k * cos(phase) * d;

        wavelength *= 0.8;
        amplitude *= 0.8;
        angle += 2.4;
    }

    FragPos = world + vec3(0.0, height, 0.0);
    // Surface y = h(x, z): the normal is (-dh/dx, 1, -dh/dz)
    FragNormal = normalize(vec3(-slope.x, 1.0, -slope.y));
    // Along +x the surface rises by dh/dx
    FragTangent = normalize(vec3(1.0, slope.x, 0.0));
    FragTexCoord = TexCoord;

    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
