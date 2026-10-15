#version 430

// Must match the compute shaders (96 bytes, std430)
struct Particle {
    vec4 color;
    vec4 startColor;
    vec4 endColor;
    vec3 pos;
    float life;
    vec3 velocity;
    float initialLife;
    vec3 accel;
    float _pad0;
    vec2 scale;
    vec2 _pad1;
};

layout(std430, binding = 0) readonly buffer ParticleBuffer {
    Particle particles[];
};

layout(std430, binding = 3) readonly buffer DrawlistBuffer {
    uint drawIndices[];
};

layout(location = 0) in vec2 Corner; // quad corner in [-0.5, 0.5]

uniform mat4 uView;
uniform mat4 uProjection;

out vec4 vColor;
out vec2 vUV;

void main() {
    Particle p = particles[drawIndices[gl_InstanceID]];

    // Billboard: camera right/up are the rows of the view matrix
    vec3 right = vec3(uView[0][0], uView[1][0], uView[2][0]);
    vec3 up = vec3(uView[0][1], uView[1][1], uView[2][1]);
    // Positions are already in world space (emitter transform applied at spawn)
    vec3 world = p.pos + right * Corner.x * p.scale.x + up * Corner.y * p.scale.y;

    vColor = p.color;
    vUV = Corner * 2.0;
    gl_Position = uProjection * uView * vec4(world, 1.0);
}
