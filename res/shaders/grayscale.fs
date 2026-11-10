#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;

void main() {
    vec3 color = texture(uScreen, vUV).rgb;
    // Rec. 709 luminance weights
    float gray = dot(color, vec3(0.2126, 0.7152, 0.0722));
    FragColor = vec4(vec3(gray), 1.0);
}
