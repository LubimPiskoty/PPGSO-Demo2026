#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;

void main() {
    vec3 color = texture(uScreen, vUV).rgb;

    FragColor = vec4(1.0 - color, 1.0);
}