#pragma once

#include <GL/glew.h>
#include <string>

std::string loadShaderSource(const std::string &filepath);

GLuint compileShader(GLenum type, const std::string &source);

GLuint createShaderProgram(const std::string &vertexPath,
                           const std::string &fragmentPath);
