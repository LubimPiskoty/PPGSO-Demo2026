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
// Uniform helpers: set a uniform on the currently bound program
// (call glUseProgram first). Unknown names are silently ignored by GL.
void uInt(const char *name, int v);
void uFloat(const char *name, float v);
void uDouble(const char *name, double v); // needs GL 4.0 + double uniform
void uVec2(const char *name, const glm::vec2 &v);
void uVec3(const char *name, const glm::vec3 &v);
void uVec4(const char *name, const glm::vec4 &v);
void uMat4(const char *name, const glm::mat4 &v);
// Binds `tex` to texture unit `unit` and points the sampler uniform at it.
void uTexture(const char *name, GLuint tex, int unit = 0,
              GLenum target = GL_TEXTURE_2D);
