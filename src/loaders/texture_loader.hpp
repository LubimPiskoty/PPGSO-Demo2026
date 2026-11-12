#pragma once

#include "../render/material.hpp"
#include <GL/glew.h>
#include <memory>
#include <string>

namespace loader {
// Uploads RGBA8 pixels as a repeating, mipmapped 2D texture. srgb: the
// pixels are colours (albedo, emissive), stored gamma encoded as images
// are; the GPU decodes them to linear when sampled, so lighting adds up
// linear light and gama.fs encodes the frame once at the end. Data
// textures (normal, roughness, specular maps) stay linear: false.
GLuint createTexture(const unsigned char *pixels, int width, int height,
                     bool srgb = false);

// filename is the embedded resource path, e.g. "grid_dark.png"; srgb as
// for createTexture
std::shared_ptr<render::Texture> loadTexture(const std::string &filename,
                                             bool srgb = false);
// skybox from cube map
GLuint loadCubemapCross(const std::string &filename);
} // namespace loader
