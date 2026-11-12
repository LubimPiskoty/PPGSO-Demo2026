#version 330 core
// default_instanced.vs with wind: the plant bends away from the wind, more
// the higher up, so the base stays in the ground. Every copy sways a little
// out of step with its neighbours, and gusts roll across in the wind's
// direction.
layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
// Location 7, since the instance transform takes 3-6
layout(location = 7) in vec3 Tangent;
// Per-instance transform, takes locations 3-6 (one vec4 column each)
layout(location = 3) in mat4 aModel;

uniform mat4 uModel; // node transform, moves the whole batch
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uTime;         // seconds, render::Material::time
uniform vec3 uWindDir;       // xz, where the wind blows to
uniform float uWindStrength; // m the top bends in an average gust
uniform float uPlantHeight;  // m, model height that bends the full amount

out vec2 FragTexCoord;
out vec3 FragPos;
out vec3 FragTangent;
out vec3 FragNormal;
// The same depth in the prepass (prepass.fs) and the lit pass, compiled
// into two programs: the lit pass draws with GL_LEQUAL against it
invariant gl_Position;

void main() {
    mat4 model = uModel * aModel;
    vec3 world = vec3(model * vec4(Position, 1.0));
    vec3 root = vec3(model[3]); // where this copy stands

    // 0 at the base, 1 at the top, squared so the trunk stays stiff
    float bend = clamp(Position.y / uPlantHeight, 0.0, 1.0);
    bend *= bend;

    vec2 dir = normalize(uWindDir.xz);
    // Gusts travel with the wind (~8 m/s, 40 m apart), the sway is offset per
    // copy by where it stands
    float gust = 0.6 + 0.4 * sin(dot(root.xz, dir) * 0.15 - uTime * 1.2);
    float phase = dot(root.xz, vec2(0.37, 0.71));
    float sway = gust * (1.0 + 0.3 * sin(uTime * 1.7 + phase)) +
                 0.15 * sin(uTime * 3.1 + phase * 1.3);
    world.xz += dir * (uWindStrength * bend * sway);

    // Leaves flutter: quick and small, different for every spot on the tree
    float flutter = sin(uTime * 7.0 + dot(world, vec3(1.3, 2.1, 1.7)));
    world += vec3(0.04, 0.03, 0.04) * flutter * bend * gust * uWindStrength;

    FragTexCoord = TexCoord;
    FragPos = world;
    FragNormal = mat3(transpose(inverse(model))) * Normal;
    FragTangent = mat3(model) * Tangent;
    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
