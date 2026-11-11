#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;

void main() {
    vec3 color = texture(uScreen, vUV).rgb;
    color = pow(color, vec3(1.0/2.2));
    FragColor = vec4(color, 1.0);
}
