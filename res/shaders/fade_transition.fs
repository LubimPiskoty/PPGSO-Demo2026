#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;
uniform float transition; // Darkens whole screen [1;0]

void main() {
    vec3 color = texture(uScreen, vUV).rgb;
    FragColor = vec4(color * clamp(transition, 0, 1), 1.0);
}
