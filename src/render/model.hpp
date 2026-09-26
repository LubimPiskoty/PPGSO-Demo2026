#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace render {
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
    GLuint textureId = 0;

    void draw() const;
};

struct Model {
    std::string filename;
    std::vector<Mesh> meshes;

    void draw() const;
};
} // namespace render
