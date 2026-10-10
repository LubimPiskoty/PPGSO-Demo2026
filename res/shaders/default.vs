#version 330 core
layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec2 FragTexCoord;
out vec3 FragNormal;
out vec3 FragPos;

void main() {
    FragTexCoord = TexCoord;
    FragPos      = vec3(uModel * vec4(Position, 1.0));
    FragNormal = mat3(transpose(inverse(uModel))) * Normal;

    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
