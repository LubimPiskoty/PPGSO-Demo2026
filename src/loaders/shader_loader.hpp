#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>

namespace loader {

std::string loadShaderSource(const std::string &filepath);

GLuint compileShader(GLenum type, const std::string &source);

GLuint createShaderProgram(const std::string &vertexPath,
                           const std::string &fragmentPath);
} // namespace loader
