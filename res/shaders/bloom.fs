#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uScreen, 0));

    float threshold = 0.7; // od akeho jasu ma pixel ziarit
    float intensity = 1.5; // sila ziaru
    int radius = 12; // velkost ziary px *2

    vec3 glow = vec3(0.0);
    float weightSum = 0.0;

    // prejdeme okolie pixelu
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            // Round, the weight down to 0 at the rim: cut off by a square
            // window, round a very bright point (a bulb) the square showed.
            // A tight core and a faint wide tail, so the glow falls off
            // the way light scattered in a lens does, not as an even disc
            float d2 = float(x * x + y * y) / float(radius * radius);
            if (d2 >= 1.0)
                continue;
            float rim = (1.0 - d2) * (1.0 - d2);
            float w = (exp(-16.0 * d2) + 0.15 * exp(-3.0 * d2)) * rim;

            // Every 2 px, half a texel off: each sample sits between 4
            // texels and the filtering averages them, so the samples cover
            // every pixel (3 px apart, a small bright lamp fell between
            // them and its glow came out as a grid)
            vec3 c = texture(uScreen, vUV + (vec2(x, y) * 2.0 + 0.5) * texel).rgb;

            // 1) BRIGHT PASS (only the part above threshold glows, so it
            // fades in instead of popping on)
            float brightness = dot(c, vec3(0.2126, 0.7152, 0.0722));
            c *= max(brightness - threshold, 0.0) / max(brightness, 1e-4);

            // 2) BLUR
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

