#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uScreen, 0));

    float threshold = 0.8;   // od akeho jasu ma pixel ziarit
    float intensity = 1.0;   // sila ziaru
    int radius = 6;          // velkost ziary px *2

    vec3 glow = vec3(0.0);
    float weightSum = 0.0;

    // prejdeme okolie pixelu
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            vec3 c = texture(uScreen, vUV + vec2(x, y) * texel * 2.0).rgb;

            // 1) BRIGHT PASS
            float brightness = dot(c, vec3(0.2126, 0.7152, 0.0722));
            if (brightness < threshold) c = vec3(0.0);

            // 2) BLUR
            float w = exp(-float(x*x + y*y) / (2.0 * float(radius)));
            glow += c * w;
            weightSum += w;
        }
    }
    glow /= weightSum;

    // 3) COMBINE
    vec3 scene = texture(uScreen, vUV).rgb;
    FragColor = vec4(scene + glow * intensity, 1.0);
    //FragColor = vec4(glow * intensity, 1.0); // for debug

}