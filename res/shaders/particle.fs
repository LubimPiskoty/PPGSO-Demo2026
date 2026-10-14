#version 430

in vec4 vColor;
in vec2 vUV;

out vec4 FragColor;

void main() {
    // Soft round sprite
    float falloff = 1.0 - smoothstep(0.5, 1.0, length(vUV));
    float alpha = vColor.a * falloff;
    if (alpha <= 0.0)
        discard;
    FragColor = vec4(vColor.rgb, alpha);
}
