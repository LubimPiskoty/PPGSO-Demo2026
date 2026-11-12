#version 330 core
// Gaussian blur, separable: the layer runs twice (main.cpp's LAYERS,
// repeats 2), uPass 0 blurs along x, uPass 1 the result along y. Two runs
// of 2 * RADIUS + 1 samples give the same blur as one of (2 * RADIUS + 1)^2.

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;
uniform int uPass; // 0 horizontal, 1 vertical

const float SIGMA = 6.0; // px, how strong the blur is
const int RADIUS = 18;   // px each side, 3 sigma: the weights beyond are ~0

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uScreen, 0));
    vec2 step = uPass == 0 ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);

    vec3 color = vec3(0.0);
    float total = 0.0;
    for (int i = -RADIUS; i <= RADIUS; i++) {
        float weight = exp(-0.5 * float(i * i) / (SIGMA * SIGMA));
        color += texture(uScreen, vUV + float(i) * step).rgb * weight;
        total += weight;
    }
    // The weights sum to 1: the image keeps its brightness
    FragColor = vec4(color / total, 1.0);
}
