#version 430

// Must match the compute shaders (112 bytes, std430)
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
uniform vec3 uViewPos;
uniform float uStretch; // seconds of motion smeared behind the particle, 0 = off

out vec4 vColor;
out vec2 vUV;

void main() {
    Particle p = particles[drawIndices[gl_InstanceID]];

    // Camera right/up are the rows of the view matrix
    vec3 right = vec3(uView[0][0], uView[1][0], uView[2][0]);
    vec3 up = vec3(uView[0][1], uView[1][1], uView[2][1]);
    float len = p.scale.y;

    // Motion smear: stretch the quad along the on-screen velocity
    // (velocity minus its component towards the camera)
    vec3 toCam = normalize(uViewPos - p.pos);
    vec3 along = p.velocity - dot(p.velocity, toCam) * toCam;
    float speed = length(along);
    if (uStretch > 0.0 && speed > 1e-4) {
        up = along / speed;
        right = normalize(cross(up, toCam));
        len += speed * uStretch;
    }

    // Shift back by half the extra length so the head stays at p.pos and
    // the smear trails behind. Positions are already in world space
    vec3 center = p.pos - up * (len - p.scale.y) * 0.5;
    vec3 world = center + right * Corner.x * p.scale.x + up * Corner.y * len;

    vColor = p.color;
    vUV = Corner * 2.0;
    gl_Position = uProjection * uView * vec4(world, 1.0);
}
