#include "model_loader.hpp"
#include "texture_loader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cmrc/cmrc.hpp>
#include <functional>
#include <initializer_list>
#include <glm/gtc/constants.hpp>
#include <iostream>
#include <string_view>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

CMRC_DECLARE(models);

namespace loader {

using render::Mesh;
using render::Model;
using render::Vertex;

// Used for meshes with no (or an unsupported) texture, so the shader always
// has something valid bound to sample.
GLuint pinkFallbackTexture() {
    static GLuint texture = 0;
    if (texture == 0) {
        unsigned char pink[4] = {255, 0, 255, 255};
        texture = createTexture(pink, 1, 1);
    }
    return texture;
}

// Decodes the first texture of the given types found on the mesh's material.
// Returns 0 when there is none (or it can't be loaded).
GLuint loadMeshTexture(const aiScene *scene, const aiMesh *mesh,
                       std::initializer_list<aiTextureType> types) {
    if (mesh->mMaterialIndex >= scene->mNumMaterials) {
        return 0;
    }
    const aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];

    aiString path;
    bool hasTexture = false;
    for (aiTextureType type : types) {
        if (material->GetTexture(type, 0, &path) == AI_SUCCESS) {
            hasTexture = true;
            break;
        }
    }
    if (!hasTexture) {
        return 0;
    }

    // glTF/GLB textures are embedded in the file itself, referenced as "*N";
    // Assimp resolves that back to the raw image bytes for us.
    const aiTexture *embedded = scene->GetEmbeddedTexture(path.C_Str());
    if (!embedded || embedded->mHeight != 0) {
        std::cerr << "Unsupported or missing embedded texture: " << path.C_Str()
                  << std::endl;
        return 0;
    }

    // Many models embed copies of the same texture atlas, so decode each
    // distinct image only once and share the GL texture. Keyed by a hash of
    // the compressed bytes; a 64-bit collision is not a practical concern.
    static std::unordered_map<std::size_t, GLuint> cache;
    std::string_view bytes(reinterpret_cast<const char *>(embedded->pcData),
                           embedded->mWidth);
    auto cached = cache.find(std::hash<std::string_view>{}(bytes));
    if (cached != cache.end())
        return cached->second;

    int width, height, channels;
    unsigned char *pixels = stbi_load_from_memory(
        reinterpret_cast<const unsigned char *>(bytes.data()),
        static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) {
        std::cerr << "Failed to decode embedded texture: " << path.C_Str()
                  << std::endl;
        return 0;
    }

    GLuint texture = createTexture(pixels, width, height);
    stbi_image_free(pixels);
    cache[std::hash<std::string_view>{}(bytes)] = texture;
    return texture;
}

void loadMeshTextures(const aiScene *scene, const aiMesh *mesh, Mesh &result) {
    using render::TextureSlot;
    auto slot = [&](TextureSlot s) -> GLuint & {
        return result.textures[(int)s];
    };

    slot(TextureSlot::Albedo) = loadMeshTexture(
        scene, mesh, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE});
    // Albedo always gets something, so the shader has a valid texture to
    // sample even for untextured meshes
    if (slot(TextureSlot::Albedo) == 0)
        slot(TextureSlot::Albedo) = pinkFallbackTexture();

    slot(TextureSlot::Normal) = loadMeshTexture(
        scene, mesh, {aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA});
    // glTF packs roughness into the G channel of its metallicRoughness
    // texture, which older Assimp versions report as UNKNOWN
    slot(TextureSlot::Roughness) = loadMeshTexture(
        scene, mesh, {aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN});
    slot(TextureSlot::Specular) =
        loadMeshTexture(scene, mesh, {aiTextureType_SPECULAR});
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
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
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

    // Location 7, since the instance transform takes 3-6
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, tangent));

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
        vertex.tangent = mesh->HasTangentsAndBitangents()
                             ? glm::vec3{mesh->mTangents[i].x,
                                         mesh->mTangents[i].y,
                                         mesh->mTangents[i].z}
                             : glm::vec3{0.0f, 0.0f, 0.0f};
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
    loadMeshTextures(scene, mesh, result);
    return result;
}

std::string extensionOf(const std::string &filename) {
    auto dot = filename.find_last_of('.');
    return dot == std::string::npos ? "" : filename.substr(dot + 1);
}

Model loadModel(const std::string &filename) {
    Model model;
    // Set even when loading fails, so a missing model keeps its name when the
    // scene is saved again
    model.filename = filename;

    auto fs = cmrc::models::get_filesystem();
    if (!fs.exists(filename)) {
        std::cerr << "Embedded model not found: " << filename << std::endl;
        return model;
    }
    auto file = fs.open(filename);

    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFileFromMemory(
        file.begin(), file.size(), aiProcess_Triangulate | aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace,
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

Model createPlane(float size, float unitsPerTile) {
    return createRect(glm::vec2(size / 2.f), unitsPerTile);
}

Model createRect(glm::vec2 halfSize, float unitsPerTile) {
    const float hx = halfSize.x, hz = halfSize.y;
    glm::vec3 up{0.f, 1.f, 0.f};
    // UVs in world units / tile size, so the texture repeats at a fixed
    // world scale no matter how big the plane is (needs GL_REPEAT)
    auto vertex = [&](float x, float z) {
        return Vertex{{x, 0.f, z}, up, glm::vec2{x, z} / unitsPerTile,
                      {1.f, 0.f, 0.f}};
    };
    std::vector<Vertex> vertices = {
        vertex(-hx, -hz),
        vertex(hx, -hz),
        vertex(hx, hz),
        vertex(-hx, hz),
    };
    // Counter-clockwise when seen from above
    std::vector<unsigned int> indices = {0, 2, 1, 0, 3, 2};

    Model model;
    model.meshes.push_back(createMesh(vertices, indices));
    model.filename = "<plane>";
    return model;
}

Model createBox(glm::vec3 halfExtents, float unitsPerTile) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    // Each face: outward normal plus two tangent axes (u x v == normal, so
    // the corners below wind counter-clockwise seen from outside)
    struct Face {
        glm::vec3 normal, u, v;
    };
    const Face faces[] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };

    for (const Face &f : faces) {
        unsigned int base = static_cast<unsigned int>(vertices.size());
        glm::vec3 center = f.normal * halfExtents;
        glm::vec3 du = f.u * halfExtents;
        glm::vec3 dv = f.v * halfExtents;
        float us = glm::length(du), vs = glm::length(dv);
        for (auto [su, sv] : {std::pair{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) {
            glm::vec3 p = center + du * (float)su + dv * (float)sv;
            glm::vec2 uv{(su * us) / unitsPerTile, (sv * vs) / unitsPerTile};
            vertices.push_back(Vertex{p, f.normal, uv, f.u});
        }
        for (unsigned int i : {0u, 1u, 2u, 0u, 2u, 3u})
            indices.push_back(base + i);
    }

    Model model;
    model.meshes.push_back(createMesh(vertices, indices));
    model.filename = "<box>";
    return model;
}

Model createPyramid(float halfBase, float height) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    const glm::vec3 apex{0.f, height, 0.f};
    // Base corners, counter-clockwise seen from above
    const glm::vec3 c[4] = {{-halfBase, 0.f, halfBase},
                            {halfBase, 0.f, halfBase},
                            {halfBase, 0.f, -halfBase},
                            {-halfBase, 0.f, -halfBase}};

    // Sides: own vertices per face so the shading stays flat
    for (int i = 0; i < 4; i++) {
        glm::vec3 a = c[i], b = c[(i + 1) % 4];
        glm::vec3 edge = glm::normalize(b - a);
        glm::vec3 n = glm::normalize(glm::cross(b - a, apex - a));
        unsigned int base = static_cast<unsigned int>(vertices.size());
        vertices.push_back(Vertex{a, n, {0.f, 0.f}, edge});
        vertices.push_back(Vertex{b, n, {1.f, 0.f}, edge});
        vertices.push_back(Vertex{apex, n, {0.5f, 1.f}, edge});
        for (unsigned int k : {0u, 1u, 2u})
            indices.push_back(base + k);
    }

    // Base, facing down
    unsigned int base = static_cast<unsigned int>(vertices.size());
    for (int i = 0; i < 4; i++)
        vertices.push_back(Vertex{c[i], {0.f, -1.f, 0.f},
                                  {c[i].x > 0.f, c[i].z > 0.f}, {1.f, 0.f, 0.f}});
    for (unsigned int k : {0u, 2u, 1u, 0u, 3u, 2u})
        indices.push_back(base + k);

    Model model;
    model.meshes.push_back(createMesh(vertices, indices));
    model.filename = "<pyramid>";
    return model;
}

Model createSphere(float radius, float unitsPerTile, int segments, int rings,
                   bool invertNormals) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    const float pi = glm::pi<float>();
    float uScale = 2.f * pi * radius / unitsPerTile;
    float vScale = pi * radius / unitsPerTile;

    for (int r = 0; r <= rings; r++) {
        float v = (float)r / rings;
        float phi = v * pi; // 0 at the top pole
        for (int s = 0; s <= segments; s++) {
            float u = (float)s / segments;
            float theta = u * 2.f * pi;
            glm::vec3 n{glm::sin(phi) * glm::cos(theta), glm::cos(phi),
                        glm::sin(phi) * glm::sin(theta)};
            // Direction of increasing u (theta)
            glm::vec3 t{-glm::sin(theta), 0.f, glm::cos(theta)};
            vertices.push_back(Vertex{n * radius, invertNormals ? -n : n,
                                      {u * uScale, v * vScale}, t});
        }
    }

    unsigned int stride = segments + 1;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < segments; s++) {
            unsigned int a = r * stride + s, b = a + stride;
            for (unsigned int i : {a, a + 1, b, a + 1, b + 1, b})
                indices.push_back(i);
        }
    }

    Model model;
    model.meshes.push_back(createMesh(vertices, indices));
    model.filename = "<sphere>";
    return model;
}
} // namespace loader
