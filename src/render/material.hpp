#pragma once

#include "../loaders/shader_loader.hpp"
#include "../util/guid.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace render {

// Which texture a slot holds. Each slot maps to a fixed sampler name in the
// shader and binds to textureUnit(slot).
enum class TextureSlot {
  Albedo,    // uTexture / uAlbedoTex
  Normal,    // uNormalTex
  Roughness, // uRoughnessTex
  Specular,  // uSpecularTex
  Emissive,  // uEmissiveTex, added on top of the lighting
  Count
};

// Texture unit of a slot: the slot's index, except that units 4 and 5 are
// taken by the skybox cubemap and the shadow map array
constexpr int textureUnit(int slot) { return slot < 4 ? slot : slot + 2; }

enum class BlendMode {
  Opaque,
  // Blending on, depth writes off (Material::bind). Not sorted yet, and
  // lit.fs doesn't output the opacity: see TODO.md, transparent objects
  Transparent,
};

// Shared shader program + the source paths it was built from, so several
// materials can reuse one program.
struct Shader {
  GLuint program = 0;
  std::string vertexPath;
  std::string fragmentPath;

  Shader(std::string vertexPath, std::string fragmentPath)
      : Shader(vertexPath, fragmentPath, false) {}

  Shader(std::string vertexPath, std::string fragmentPath, bool instanced)
      : vertexPath(vertexPath), fragmentPath(fragmentPath),
        instanced(instanced) {
    program = loader::createShaderProgram(vertexPath, fragmentPath);
  }

  // Owns its programs: shared through shared_ptr, never copied
  ~Shader() {
    glDeleteProgram(program);
    glDeleteProgram(prepass);
  }
  Shader(const Shader &) = delete;
  Shader &operator=(const Shader &) = delete;

  bool isInstanced() { return instanced; }

  // The depth prepass's program: the same vertex shader (wind, waves,
  // instancing move the vertices the same way) with prepass.fs, which
  // writes the thin G-buffer's normals. Built on first use. Only for
  // lit.fs materials, the vertex shaders prepass.fs knows the outputs of;
  // 0 for the rest, they're drawn in the forward pass only.
  GLuint prepassProgram() {
    if (!prepassBuilt) {
      prepassBuilt = true;
      if (fragmentPath == "lit.fs")
        prepass = loader::createShaderProgram(vertexPath, "prepass.fs");
    }
    return prepass;
  }

private:
  bool instanced = false;
  bool prepassBuilt = false;
  GLuint prepass = 0;
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
  // The same into the shader's prepass program (Shader::prepassProgram).
  // False when it has none.
  bool bindPrepass() const;

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

  // Seconds of scene time, uploaded as uTime for animated shaders (water).
  // Scene::update advances it, so it pauses with the scene.
  static inline float time = 0.f;
  // The sky's tint (component::Skybox::tint), uploaded as uSkyTint: what a
  // surface reflects of the sky gets dark with it. Scene::draw sets it.
  static inline glm::vec3 skyTint{1.f};
  // Both sides drawn (no back-face culling), lit.fs flips the normal on the
  // back side. For thin cards: leaves, grass, cloth.
  bool doubleSided = false;

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

  void bindTo(GLuint program) const;
};

} // namespace render
