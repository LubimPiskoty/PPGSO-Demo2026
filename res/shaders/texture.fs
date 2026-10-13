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

// same as in cpp file
#define MAX_LIGHTS 16

struct Light {
    vec4 position;            // xyz pos, w type of light
    vec4 direction;           // xyz  dir
    vec4 color;               // rgb = col, w = ambient
    vec4 attenuation;         // x = constant, y = linear, z = quadratic
};

layout(std140) uniform Lights {
    vec4 uLightPosition[MAX_LIGHTS];
    vec4 uLightDirection[MAX_LIGHTS];
    vec4 uLightColor[MAX_LIGHTS];
    vec4 uLightAttenuation[MAX_LIGHTS];
    vec4 uViewPos;
    ivec4 uLightCount;
};

vec3 CalcDirLight(Light light, vec3 normal, vec3 viewDir, vec3 texColor);
vec3 CalcPointLight(Light light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 texColor);

void main() {
    vec3 texColor = texture(uTexture, FragTexCoord).rgb;

    vec3 norm    = normalize(FragNormal);
    vec3 viewDir = normalize(uViewPos.xyz - FragPos);

    // add all lights together
    vec3 result = vec3(0.0);
    for (int i = 0; i < uLightCount.x; i++) {
        Light light = Light(uLightPosition[i], uLightDirection[i],
                            uLightColor[i], uLightAttenuation[i]);
        if (light.position.w < 0.5)
            result += CalcDirLight(light, norm, viewDir, texColor);
        else
            result += CalcPointLight(light, norm, FragPos, viewDir, texColor);
    }
    FragColor = vec4(result, 1.0);
}

vec3 CalcDirLight(Light light, vec3 normal, vec3 viewDir, vec3 texColor)
{
    vec3 lightDir = normalize(-light.direction.xyz);
    vec3 color    = light.color.rgb;

    float diff = max(dot(normal, lightDir), 0.0);

    float spec = 0.0;
    if (diff > 0.0) { // only if its directed to ligbht
        vec3 reflectDir = reflect(-lightDir, normal);
        spec = pow(max(dot(viewDir, reflectDir), 0.0), max(uMaterial.shininess, 1.0));
    }

    vec3 ambient  = color * light.color.w * texColor;
    vec3 diffuse  = color * diff * texColor;
    vec3 specular = color * spec * uMaterial.specular;
    return ambient + diffuse + specular;
}

vec3 CalcPointLight(Light light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 texColor)
{
    vec3 toLight  = light.position.xyz - fragPos;   // od povrchu k žiarovke
    vec3 lightDir = normalize(toLight);
    vec3 color    = light.color.rgb;

    float diff = max(dot(normal, lightDir), 0.0);

    float spec = 0.0;
    if (diff > 0.0) { // only if its directed to ligbht
        vec3 reflectDir = reflect(-lightDir, normal);
        spec = pow(max(dot(viewDir, reflectDir), 0.0), max(uMaterial.shininess, 1.0));
    }

    // the more far away the weaker light is
    float distance    = length(toLight);
    float attenuation = 1.0 / (light.attenuation.x +
                               light.attenuation.y * distance +
                               light.attenuation.z * distance * distance);

    vec3 ambient  = color * light.color.w * texColor;
    vec3 diffuse  = color * diff * texColor;
    vec3 specular = color * spec * uMaterial.specular;
    return (ambient + diffuse + specular) * attenuation;
}