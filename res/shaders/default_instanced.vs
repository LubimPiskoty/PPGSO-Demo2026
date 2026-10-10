#version 330 core
layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
// Per-instance transform, takes locations 3-6 (one vec4 column each),
// filled from the instance VBO set up in Model::makeInstanced()
layout(location = 3) in mat4 aModel;

uniform mat4 uModel; // node transform, moves the whole batch
uniform mat4 uView;
uniform mat4 uProjection;

out vec2 FragTexCoord;
out vec3 FragPos;
out vec3 FragNormal;


void main() {
    mat4 model = uModel * aModel;

    FragTexCoord = TexCoord;
    FragPos      = vec3(model * vec4(Position, 1.0));
    FragNormal   = mat3(transpose(inverse(model))) * Normal;

    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
