#version 330 core
out vec4 FragColor;

in vec3 TexDir;

uniform samplerCube uSkybox;
uniform vec3 uTint; // 1 by day, dark blue at night

void main() {
    FragColor = vec4(texture(uSkybox, TexDir).rgb * uTint, 1.0);
}