#version 330 core

out vec4 FragColor;

in vec2 FragTexCoord;
in vec3 FragNormal;

uniform sampler2D uTexture;

struct Material {
    vec3 diffuse;
};
uniform Material uMaterial;

// The grid texture is dark (~0.2), this scales it up so the fill ends up
// close to uMaterial.diffuse and the grid lines come out lighter than it
uniform float uBrighten;

const vec3 LIGHT_DIR = normalize(vec3(0.4, 1.0, 0.6)); // towards the light

void main() {
    vec3 grid = texture(uTexture, FragTexCoord).rgb;
    vec3 albedo = min(grid * (1.0 + uBrighten) * uMaterial.diffuse, vec3(1.0));

    float lambert = max(dot(normalize(FragNormal), LIGHT_DIR), 0.0);
    FragColor = vec4(albedo * (0.4 + 0.6 * lambert), 1.0);
}
