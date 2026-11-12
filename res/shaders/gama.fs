#version 330 core
// Gamma encoding for the screen, the last layer: the frame is linear light
// up to here (colour textures are sRGB and decoded when sampled), the
// window shows what's encoded

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;

void main() {
    vec3 color = texture(uScreen, vUV).rgb;
    color = pow(color, vec3(1.0/2.2));
    FragColor = vec4(color, 1.0);
}
