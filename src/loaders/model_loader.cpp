#include "model_loader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cmrc/cmrc.hpp>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

CMRC_DECLARE(models);

namespace loader {

GLuint createTexture(const unsigned char *pixels, int width, int height) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

// Used for meshes with no (or an unsupported) texture, so the shader always
// has something valid bound to sample.
// TODO: Change into purple checker texture
GLuint pinkFallbackTexture() {
    static GLuint texture = 0;
    if (texture == 0) {
        unsigned char pink[4] = {255, 0, 255, 255};
        texture = createTexture(pink, 1, 1);
    }
    return texture;
}

GLuint loadMeshTexture(const aiScene *scene, const aiMesh *mesh) {
    if (mesh->mMaterialIndex >= scene->mNumMaterials) {
        return pinkFallbackTexture();
    }
    const aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];

    aiString path;
    bool hasTexture =
        material->GetTexture(aiTextureType_BASE_COLOR, 0, &path) ==
            AI_SUCCESS ||
        material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS;
    if (!hasTexture) {
        return pinkFallbackTexture();
    }

    // glTF/GLB textures are embedded in the file itself, referenced as "*N";
    // Assimp resolves that back to the raw image bytes for us.
    const aiTexture *embedded = scene->GetEmbeddedTexture(path.C_Str());
    if (!embedded || embedded->mHeight != 0) {
        std::cerr << "Unsupported or missing embedded texture: " << path.C_Str()
                  << std::endl;
        return pinkFallbackTexture();
    }

    int width, height, channels;
    unsigned char *pixels = stbi_load_from_memory(
        reinterpret_cast<const unsigned char *>(embedded->pcData),
        static_cast<int>(embedded->mWidth), &width, &height, &channels, 4);
    if (!pixels) {
        std::cerr << "Failed to decode embedded texture: " << path.C_Str()
                  << std::endl;
        return pinkFallbackTexture();
    }

    GLuint texture = createTexture(pixels, width, height);
    stbi_image_free(pixels);
    return texture;
}

Mesh createMesh(const std::vector<Vertex> &vertices,
                const std::vector<unsigned int> &indices) {
    Mesh mesh;
    mesh.indexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                 indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, texCoord));

    glBindVertexArray(0);

    return mesh;
}

Mesh processMesh(const aiScene *scene, const aiMesh *mesh) {
    std::vector<Vertex> vertices;
    vertices.reserve(mesh->mNumVertices);

    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        Vertex vertex;
        vertex.position = {mesh->mVertices[i].x, mesh->mVertices[i].y,
                           mesh->mVertices[i].z};
        vertex.normal = mesh->HasNormals() ? glm::vec3{mesh->mNormals[i].x,
                                                       mesh->mNormals[i].y,
                                                       mesh->mNormals[i].z}
                                           : glm::vec3{0.0f, 0.0f, 0.0f};
        vertex.texCoord = mesh->HasTextureCoords(0)
                              ? glm::vec2{mesh->mTextureCoords[0][i].x,
                                          mesh->mTextureCoords[0][i].y}
                              : glm::vec2{0.0f, 0.0f};
        vertices.push_back(vertex);
    }

    std::vector<unsigned int> indices;
    indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        const aiFace &face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    Mesh result = createMesh(vertices, indices);
    result.textureId = loadMeshTexture(scene, mesh);
    return result;
}

std::string extensionOf(const std::string &filename) {
    auto dot = filename.find_last_of('.');
    return dot == std::string::npos ? "" : filename.substr(dot + 1);
}

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

Model loadModel(const std::string &filename) {
    Model model;

    auto fs = cmrc::models::get_filesystem();
    if (!fs.exists(filename)) {
        std::cerr << "Embedded model not found: " << filename << std::endl;
        return model;
    }
    auto file = fs.open(filename);

    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFileFromMemory(
        file.begin(), file.size(), aiProcess_Triangulate | aiProcess_FlipUVs,
        extensionOf(filename).c_str());

    if (!scene || !scene->mRootNode) {
        std::cerr << "Assimp error loading " << filename << ": "
                  << importer.GetErrorString() << std::endl;
        return model;
    }

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        model.meshes.push_back(processMesh(scene, scene->mMeshes[i]));
    }
    return model;
}
} // namespace loader
