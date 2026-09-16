#version 330 core

out vec4 FragColor;

uniform sampler2D uTexture;

in vec2 FragTexCoord;

void main() {
    vec3 texColor = texture(uTexture, FragTexCoord).rgb;
    FragColor = vec4(texColor, 1.0);
}
