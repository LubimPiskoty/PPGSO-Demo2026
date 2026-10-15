#include "material.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <cstdio>

namespace render {

// Sampler name for each TextureSlot, indexed by the slot
static const char *samplerNames[(int)TextureSlot::Count] = {
    "uAlbedoTex",
    "uNormalTex",
    "uRoughnessTex",
    "uSpecularTex",
};

// 1x1 magenta, bound in place of a missing albedo texture
static GLuint missingTexture() {
    static GLuint id = 0;
    if (id == 0) {
        const unsigned char magenta[4] = {255, 0, 255, 255};
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, magenta);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    return id;
}

Material::Material(const std::string &name, std::shared_ptr<Shader> shader)
    : name(name), shader(shader) {
    guid = util::Guid::generate();
}

void Material::bind() const {
    if (!shader)
        return;

    // Every glUniform* call below targets this program
    glUseProgram(shader->program);

    // Textures: slot index == texture unit
    for (int i = 0; i < (int)TextureSlot::Count; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        GLuint id = textures[i] ? textures[i]->id : 0;
        // Missing albedo shows up magenta instead of silently black
        if (!id && i == (int)TextureSlot::Albedo)
            id = missingTexture();
        glBindTexture(GL_TEXTURE_2D, id);
        glUniform1i(uniLoc(samplerNames[i]), i);
    }
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(uniLoc("uHasNormalMap"), hasTexture(TextureSlot::Normal));
    glUniform1i(uniLoc("uHasRoughnessMap"),
                hasTexture(TextureSlot::Roughness));
    glUniform1i(uniLoc("uHasSpecularMap"), hasTexture(TextureSlot::Specular));

    // Material parameters
    glUniform3fv(uniLoc("uMaterial.ambient"), 1, glm::value_ptr(ambient));
    glUniform3fv(uniLoc("uMaterial.diffuse"), 1, glm::value_ptr(diffuse));
    glUniform3fv(uniLoc("uMaterial.specular"), 1, glm::value_ptr(specular));
    glUniform1f(uniLoc("uMaterial.shininess"), shininess);
    glUniform1f(uniLoc("uMaterial.opacity"), opacity);


    // Extra uniforms
    for (auto &[n, v] : uFloats)
        glUniform1f(uniLoc(n), v);
    for (auto &[n, v] : uVec3s)
        glUniform3fv(uniLoc(n), 1, glm::value_ptr(v));
    for (auto &[n, v] : uVec4s)
        glUniform4fv(uniLoc(n), 1, glm::value_ptr(v));
    for (auto &[n, v] : uMat4s)
        glUniformMatrix4fv(uniLoc(n), 1, GL_FALSE, glm::value_ptr(v));

    // Render state
    if (blend == BlendMode::Transparent) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // Test against opaque depth, but don't hide transparent objects
        // behind each other
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
}

void Material::setTexture(TextureSlot slot, std::shared_ptr<Texture> texture) {
    this->textures[(int)slot] = texture;
}

std::shared_ptr<Texture> Material::getTexture(TextureSlot slot) const {
    return textures[(int)slot];
}

bool Material::hasTexture(TextureSlot slot) const {
    return this->textures[(int)slot] != nullptr;
}

// Setters only store the value; bind() uploads it, so the right program is
// guaranteed to be current and materials sharing a Shader don't clash.
void Material::setFloat(const std::string &name, float v) {
    uFloats[name] = v;
}

void Material::setVec3(const std::string &name, const glm::vec3 &v) {
    uVec3s[name] = v;
}

void Material::setVec4(const std::string &name, const glm::vec4 &v) {
    uVec4s[name] = v;
}

void Material::setMat4(const std::string &name, const glm::mat4 &v) {
    uMat4s[name] = v;
}

std::shared_ptr<Shader> Material::getShader() const {
    return shader;
}

void Material::setShader(std::shared_ptr<Shader> shader) {
    this->shader = shader;
}

GLint Material::uniLoc(const std::string &name) const {
    return glGetUniformLocation(this->shader->program, name.c_str());
}

} // namespace render

// void uTexture(const char *name, GLuint tex, int unit, GLenum target) {
//     glActiveTexture(GL_TEXTURE0 + unit);
//     glBindTexture(target, tex);
//     glUniform1i(uniLoc(name), unit);
// }
