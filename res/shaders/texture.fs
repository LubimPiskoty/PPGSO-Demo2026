#version 330 core

out vec4 FragColor;

in vec2 FragTexCoord;
in vec3 FragNormal;
in vec3 FragPos;

struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
    float opacity;
};

uniform sampler2D uTexture;
uniform Material uMaterial;

// ligt buffer UBO slot 1
layout(std140) uniform Lights {
    vec4 uLightDirection;   // xyz light dir
    vec4 uLightColor;       // rgb color
    vec4 uViewPos;          // xyz camera pos
};

vec3 CalcDirLight(vec3 normal, vec3 viewDir, vec3 texColor);

void main() {
    vec3 texColor = texture(uTexture, FragTexCoord).rgb;

    vec3 norm    = normalize(FragNormal);
    vec3 viewDir = normalize(uViewPos.xyz - FragPos);

    vec3 result = CalcDirLight(norm, viewDir, texColor);

    FragColor = vec4(result, 1.0);

}

vec3 CalcDirLight(vec3 normal, vec3 viewDir, vec3 texColor)
{
    vec3 lightDir = normalize(-uLightDirection.xyz);
    vec3 color    = uLightColor.rgb;

    // diffuse
    float diff = max(dot(normal, lightDir), 0.0);

    // specular
    float spec = 0.0;
    if (diff > 0.0) {
        vec3 reflectDir = reflect(-lightDir, normal);
        spec = pow(max(dot(viewDir, reflectDir), 0.0), max(uMaterial.shininess, 1.0));
    }

    vec3 ambient  = color * uLightColor.w * texColor;
    vec3 diffuse  = color * diff * texColor;
    vec3 specular = color * spec * uMaterial.specular;
    return ambient + diffuse + specular;
}