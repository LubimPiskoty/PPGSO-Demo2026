#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;

const float EXPOSURE = 1.5; // brightness before tone mapping

void main() {
    vec3 color = texture(uScreen, vUV).rgb; // HDR color (can be > 1)

    color *= EXPOSURE;

    color = color / (color + vec3(1.0)); // Reinhard: HDR -> [0,1]

    color = pow(color, vec3(1.0 / 2.2)); // gamma correction
    FragColor = vec4(color, 1.0);
}