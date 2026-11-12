#version 330 core
// Depth prepass (Scene::depth_prepass), linked with a lit.fs material's own
// vertex shader (render::Shader::prepassProgram): the depth, and the thin
// G-buffer's normals into the framebuffer's second color texture. Builds
// the normal the same way lit.fs does, keep getNormal() in step with it.
// Alpha-masked cards are cut out where lit.fs cuts them.

in vec2 FragTexCoord;
in vec3 FragNormal;
in vec3 FragTangent;

// World space, unit length; output 0 (the color) is off in the prepass
layout(location = 1) out vec4 Normal;

uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalTex;
uniform bool uHasNormalMap;

// As lit.fs
vec3 getNormal() {
    vec3 N = normalize(gl_FrontFacing ? FragNormal : -FragNormal);
    bool hasTangent = dot(FragTangent, FragTangent) > 1e-8;
    if (!uHasNormalMap || !hasTangent)
        return N;
    vec3 T = normalize(FragTangent - dot(FragTangent, N) * N);
    vec3 B = cross(N, T);
    vec3 tangentNormal = texture(uNormalTex, FragTexCoord).rgb * 2.0 - 1.0;
    return normalize(mat3(T, B, N) * tangentNormal);
}

void main() {
    if (texture(uAlbedoTex, FragTexCoord).a < 0.5)
        discard;
    Normal = vec4(getNormal(), 1.0);
}
