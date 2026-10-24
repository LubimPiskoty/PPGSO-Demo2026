#version 330

in vec2 FragTexCoord;

// Albedo is on unit 0 while the shadow pass draws
uniform sampler2D uAlbedoTex;

void main() {
    // Alpha-masked cards (tree leaves) only block light where they are opaque
    if (texture(uAlbedoTex, FragTexCoord).a < 0.5)
        discard;
}
