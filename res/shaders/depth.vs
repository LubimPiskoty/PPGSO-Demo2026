#version 330 core
layout(location = 0) in vec3 Position;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    vec3 FragPos = vec3(uModel * vec4(Position, 1.0));
    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
