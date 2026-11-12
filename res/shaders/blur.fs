#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uScreen, 0));
    float spread = 3.0;

    // offsets to around pixels
    vec2 offsets[9] = vec2[](
        vec2(-1,  1), vec2(0,  1), vec2(1,  1),
        vec2(-1,  0), vec2(0,  0), vec2(1,  0),
        vec2(-1, -1), vec2(0, -1), vec2(1, -1)
    );

    // gauss center of weights
    float kernel[9] = float[](
        1.0, 2.0, 1.0,
        2.0, 4.0, 2.0,
        1.0, 2.0, 1.0
    );

    vec3 color = vec3(0.0);
    // compute new color for pixel
    for (int i = 0; i < 9; i++) {
        color += texture(uScreen, vUV + offsets[i] * texel*spread).rgb * kernel[i];
    }

    FragColor = vec4(color / 16.0, 1.0);
}