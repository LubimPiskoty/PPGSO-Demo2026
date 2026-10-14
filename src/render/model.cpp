#include "model.hpp"
#include <glm/matrix.hpp>

namespace render {

// Uniform telling the shader whether a slot holds a map, for slots that are
// optional (albedo always has one)
static const char *hasMapNames[(int)TextureSlot::Count] = {
    nullptr,
    "uHasNormalMap",
    "uHasRoughnessMap",
    "uHasSpecularMap",
};

// Binds the mesh's own textures over the material's and returns what was
// there, so restoreTextures() can put it back for the next mesh
Mesh::Overridden Mesh::bindTextures() const {
    Overridden saved;
    glGetIntegerv(GL_CURRENT_PROGRAM, &saved.program);

    // Slots the mesh has no texture for keep the one the material bound
    for (int i = 0; i < (int)TextureSlot::Count; i++) {
        if (textures[i] == 0)
            continue;
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved.texture[i]);
        glBindTexture(GL_TEXTURE_2D, textures[i]);

        if (!hasMapNames[i] || saved.program == 0)
            continue;
        GLint loc = glGetUniformLocation(saved.program, hasMapNames[i]);
        if (loc < 0)
            continue;
        glGetUniformiv(saved.program, loc, &saved.hasMap[i]);
        glUniform1i(loc, 1);
    }
    glActiveTexture(GL_TEXTURE0);
    return saved;
}

void Mesh::restoreTextures(const Overridden &saved) const {
    for (int i = 0; i < (int)TextureSlot::Count; i++) {
        if (textures[i] == 0)
            continue;
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, saved.texture[i]);

        if (!hasMapNames[i] || saved.program == 0)
            continue;
        GLint loc = glGetUniformLocation(saved.program, hasMapNames[i]);
        if (loc >= 0)
            glUniform1i(loc, saved.hasMap[i]);
    }
    glActiveTexture(GL_TEXTURE0);
}

void Mesh::draw() const {
    Overridden saved = bindTextures();
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    restoreTextures(saved);
}

void Mesh::draw_instanced(GLsizei instance_count) const {
    Overridden saved = bindTextures();
    glBindVertexArray(vao);
    glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr,
                            instance_count);
    glBindVertexArray(0);
    restoreTextures(saved);
}

void Model::draw() const {
    if (isInstanced())
        for (const auto &mesh : meshes)
            mesh.draw_instanced(instance_count);
    else
        for (const auto &mesh : meshes)
            mesh.draw();
}

void Model::makeInstanced(const std::vector<glm::mat4> &instance_transforms) {
    instance_count = instance_transforms.size();

    if (!isInstanced()) // Generate if uninitialized
        glGenBuffers(1, &instanceVBO);

    // Copy transform data
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, instance_count * sizeof(glm::mat4),
                 instance_transforms.data(), GL_STATIC_DRAW);

    // Describe to each mesh vao
    for (const auto &mesh : meshes) {
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        for (int i = 0; i < 4; i++) {
            glEnableVertexAttribArray(3 + i);
            glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE,
                                  sizeof(glm::mat4),
                                  (void *)(sizeof(glm::vec4) * i));
            glVertexAttribDivisor(3 + i, 1);
        }
    }
    glBindVertexArray(0);
}

void Model::disableInstanced() {
    instance_count = 0;
    glDeleteBuffers(1, &instanceVBO);
    instanceVBO = 0;
}

} // namespace render
