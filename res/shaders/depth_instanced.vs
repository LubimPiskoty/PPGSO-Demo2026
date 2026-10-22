#version 330 core
layout(location = 0) in vec3 Position;
layout(location = 3) in mat4 aModel;

uniform mat4 uModel; // node transform, moves the whole batch
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    mat4 model = uModel * aModel;
    vec3 FragPos = vec3(model * vec4(Position, 1.0));
    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
