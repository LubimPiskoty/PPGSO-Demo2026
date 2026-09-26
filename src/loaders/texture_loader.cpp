#include "texture_loader.hpp"

#include <cmrc/cmrc.hpp>
#include <iostream>

// Implementation lives in model_loader.cpp (STB_IMAGE_IMPLEMENTATION)
#include <stb_image.h>

CMRC_DECLARE(textures);

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

std::shared_ptr<render::Texture> loadTexture(const std::string &filename) {
    auto fs = cmrc::textures::get_filesystem();
    if (!fs.exists(filename)) {
        std::cerr << "Embedded texture not found: " << filename << std::endl;
        return nullptr;
    }
    auto file = fs.open(filename);

    int width, height, channels;
    unsigned char *pixels = stbi_load_from_memory(
        reinterpret_cast<const unsigned char *>(file.begin()),
        static_cast<int>(file.size()), &width, &height, &channels, 4);
    if (!pixels) {
        std::cerr << "Failed to decode texture " << filename << ": "
                  << stbi_failure_reason() << std::endl;
        return nullptr;
    }

    auto texture = std::make_shared<render::Texture>();
    texture->id = createTexture(pixels, width, height);
    texture->path = filename;
    stbi_image_free(pixels);
    return texture;
}

} // namespace loader
