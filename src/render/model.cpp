#include "model.hpp"

namespace render {

void Mesh::draw() const {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Model::draw() const {
    for (const auto &mesh : meshes) {
        mesh.draw();
    }
}

} // namespace render
