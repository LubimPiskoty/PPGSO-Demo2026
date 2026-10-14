#version 330 core

out vec4 FragColor;

in vec2 FragTexCoord;
in vec3 FragNormal;
in vec3 FragPos;

uniform sampler2D uTexture;

void main() {
    FragColor = texture(uTexture, FragTexCoord);
}
