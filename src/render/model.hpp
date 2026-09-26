#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/matrix.hpp>
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
    void draw_instanced(GLsizei instance_count) const;
};

struct Model {
    std::string filename;
    std::vector<Mesh> meshes;

    void draw() const;

    bool isInstanced() const {
        return instanceVBO != 0;
    }
    GLsizei getInstanceCount() const {
        return instance_count;
    }

    void makeInstanced(const std::vector<glm::mat4> &instance_transforms);
    void disableInstanced();

  private:
    GLsizei instance_count = 0;
    GLuint instanceVBO = 0;
};
} // namespace render
