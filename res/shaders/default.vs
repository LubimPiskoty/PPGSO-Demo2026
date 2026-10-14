#version 330 core
layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
// Location 7, since the instance transform takes 3-6
layout(location = 7) in vec3 Tangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec2 FragTexCoord;
out vec3 FragNormal;
out vec3 FragPos;
out vec3 FragTangent;

void main() {
    FragTexCoord = TexCoord;
    FragPos      = vec3(uModel * vec4(Position, 1.0));
    FragNormal = mat3(transpose(inverse(uModel))) * Normal;
    // Tangents lie in the surface, so they transform like positions
    FragTangent = mat3(uModel) * Tangent;

    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
