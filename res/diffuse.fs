#version 330 core
in vec3 FragPos;
in vec3 FragNormal;
in vec2 FragTexCoord;

out vec4 FragColor;

uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform vec3 uObjectColor;
uniform vec3 uAmbientColor;
uniform sampler2D uTexture;

void main() {
    vec3 normal = normalize(FragNormal);
    vec3 lightDir = normalize(uLightPos - FragPos);

    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;

    vec3 texColor = texture(uTexture, FragTexCoord).rgb;
    vec3 result = (uAmbientColor + diffuse) * uObjectColor * texColor;
    FragColor = vec4(result, 1.0);
}
