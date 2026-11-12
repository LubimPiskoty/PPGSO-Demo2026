#pragma once

#include "material.hpp"
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
    // Points along +u in the texture, for normal mapping
    glm::vec3 tangent;
};

// Owns its VAO and buffers, freed with it: move-only. The textures are
// shared (the loaders' caches) and stay.
struct Mesh {
    Mesh() = default;
    ~Mesh();
    Mesh(Mesh &&other) noexcept;
    Mesh &operator=(Mesh &&other) noexcept;
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
    // Textures the model file brought along, indexed by TextureSlot.
    // 0 means none, so the material's texture for that slot stays bound.
    GLuint textures[(int)TextureSlot::Count] = {};
    // The file's material is double sided (glTF doubleSided): drawn without
    // back-face culling whatever the Material says
    bool doubleSided = false;

    void draw() const;
    void draw_instanced(GLsizei instance_count) const;

  private:
    // Material state a mesh's own textures replaced during its draw
    struct Overridden {
        GLint program = 0;
        GLint texture[(int)TextureSlot::Count] = {};
        GLint hasMap[(int)TextureSlot::Count] = {};
    };
    Overridden bindTextures() const;
    void restoreTextures(const Overridden &saved) const;
};

// Move-only, as its meshes; frees its instance buffer
struct Model {
    Model() = default;
    ~Model();
    Model(Model &&other) noexcept;
    Model &operator=(Model &&other) noexcept;
    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;

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
