#pragma once

#include "../loaders/shader_loader.hpp"
#include "../util/guid.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace render {

// Which texture a slot holds. The slot's index doubles as the texture unit
// and each slot maps to a fixed sampler name in the shader.
enum class TextureSlot {
    Albedo,   // uTexture / uAlbedo
    Normal,   // uNormalMap
    Specular, // uSpecularMap
    Count
};

enum class BlendMode {
    Opaque,
    Transparent, // drawn after opaque objects, sorted back to front
};

// Shared shader program + the source paths it was built from, so several
// materials can reuse one program and the serializer can write the paths.
struct Shader {
    GLuint program = 0;
    std::string vertexPath;
    std::string fragmentPath;

    Shader(std::string vertexPath, std::string fragmentPath)
        : vertexPath(vertexPath), fragmentPath(fragmentPath) {
        program = loader::createShaderProgram(vertexPath, fragmentPath);
    }
};

struct Texture {
    GLuint id = 0;
    std::string path;
};

class Material {
  public:
    util::Guid guid;
    Material(const std::string &name, std::shared_ptr<Shader> shader);

    // Binds the program, textures and material uniforms.
    // Per-object uniforms (uModel, uView, uProjection) stay with the caller.
    void bind() const;

    // Textures
    void setTexture(TextureSlot slot, std::shared_ptr<Texture> texture);
    std::shared_ptr<Texture> getTexture(TextureSlot slot) const;
    bool hasTexture(TextureSlot slot) const;

    // Phong / Blinn-Phong parameters
    glm::vec3 ambient{0.1f};
    glm::vec3 diffuse{1.f};
    glm::vec3 specular{0.5f};
    float shininess = 32.f;
    float opacity = 1.f;

    BlendMode blend = BlendMode::Opaque;

    // Extra per-material uniforms that don't fit the fields above,
    // uploaded in bind()
    void setFloat(const std::string &name, float v);
    void setVec3(const std::string &name, const glm::vec3 &v);
    void setVec4(const std::string &name, const glm::vec4 &v);
    void setMat4(const std::string &name, const glm::mat4 &v);

    std::shared_ptr<Shader> getShader() const;
    void setShader(std::shared_ptr<Shader> shader);

    std::string name;

  private:
    std::shared_ptr<Shader> shader;
    std::shared_ptr<Texture> textures[(int)TextureSlot::Count];

    std::unordered_map<std::string, float> uFloats;
    std::unordered_map<std::string, glm::vec3> uVec3s;
    std::unordered_map<std::string, glm::vec4> uVec4s;
    std::unordered_map<std::string, glm::mat4> uMat4s;

    GLint uniLoc(const std::string &name) const;
};

} // namespace render
