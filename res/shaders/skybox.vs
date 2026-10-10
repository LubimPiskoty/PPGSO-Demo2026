#version 330 core
layout(location = 0) in vec3 Position;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 TexDir;

void main() {
    TexDir = Position;   // direction to cubemap

    // sky moving with camera
    mat4 view = mat4(mat3(uView));
    vec4 pos = uProjection * view * vec4(Position, 1.0);

    // put w in z, to make w/w = 1 so sky is always far
    gl_Position = pos.xyww;
}