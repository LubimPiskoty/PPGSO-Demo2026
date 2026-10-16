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

GLuint loadCubemapCross(const std::string &filename) {
    auto fs = cmrc::textures::get_filesystem();
    if (!fs.exists(filename)) {
        std::cerr << "Cubemap not found: " << filename << std::endl;
        return 0;
    }
    auto file = fs.open(filename);

    int width, height, channels;
    unsigned char *data = stbi_load_from_memory(
        reinterpret_cast<const unsigned char *>(file.begin()),
        static_cast<int>(file.size()), &width, &height, &channels, 3);
    if (!data) {
        std::cerr << "Cubemap failed to decode: " << filename << std::endl;
        return 0;
    }

    int face = width / 4;   // cross is 4x width a 3x height

    // column and row for cross
    const int cells[6][2] = {
        {2, 1},   // +X right
        {0, 1},   // -X left
        {1, 0},   // +Y up
        {1, 2},   // -Y down
        {1, 1},   // +Z frotn
        {3, 1},   // -Z back
    };

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, width);

    for (int i = 0; i < 6; i++) {
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, cells[i][0] * face);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,   cells[i][1] * face);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB,
                     face, face, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    stbi_image_free(data);

    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

} // namespace loader
