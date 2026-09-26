#pragma once

#include "../render/material.hpp"
#include <GL/glew.h>
#include <memory>
#include <string>

namespace loader {
// Uploads RGBA8 pixels as a repeating, mipmapped 2D texture
GLuint createTexture(const unsigned char *pixels, int width, int height);

// filename is the embedded resource path, e.g. "grid_dark.png"
std::shared_ptr<render::Texture> loadTexture(const std::string &filename);
} // namespace loader
