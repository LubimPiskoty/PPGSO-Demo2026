#include "model.hpp"
#include <glm/matrix.hpp>

namespace render {

void Mesh::draw() const {
    // Meshes without their own texture keep the one the material bound
    if (textureId != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureId);
    }
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::draw_instanced(GLsizei instance_count) const {

    // Meshes without their own texture keep the one the material bound
    if (textureId != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureId);
    }
    glBindVertexArray(vao);
    glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr,
                            instance_count);
    glBindVertexArray(0);
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
