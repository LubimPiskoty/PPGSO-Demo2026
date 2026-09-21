#include "shader_loader.hpp"

#include <cmrc/cmrc.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

CMRC_DECLARE(shaders);

std::string loadShaderSource(const std::string &filepath) {
    auto fs = cmrc::shaders::get_filesystem();
    if (!fs.exists(filepath)) {
        std::cerr << "Embedded shader not found: " << filepath << std::endl;
        return "";
    }
    auto file = fs.open(filepath);
    return std::string(file.begin(), file.end());
}

GLuint compileShader(GLenum type, const std::string &source) {
    GLuint shader = glCreateShader(type);
    const char *src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
        std::cerr << "Shader compile error:\n" << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createShaderProgram(const std::string &vertexPath,
                           const std::string &fragmentPath) {
    std::string vertexSrc = loadShaderSource(vertexPath);
    std::string fragmentSrc = loadShaderSource(fragmentPath);

    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(program, 1024, nullptr, infoLog);
        std::cerr << "Program link error:\n" << infoLog << std::endl;
    }

    // Shaders are linked into the program now; the standalone objects aren't
    // needed
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

static GLint uniLoc(const char *name) {
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    return glGetUniformLocation(prog, name);
}

void uInt(const char *name, int v) { glUniform1i(uniLoc(name), v); }
void uFloat(const char *name, float v) { glUniform1f(uniLoc(name), v); }
void uDouble(const char *name, double v) { glUniform1d(uniLoc(name), v); }
void uVec2(const char *name, const glm::vec2 &v) {
    glUniform2fv(uniLoc(name), 1, glm::value_ptr(v));
}
void uVec3(const char *name, const glm::vec3 &v) {
    glUniform3fv(uniLoc(name), 1, glm::value_ptr(v));
}
void uVec4(const char *name, const glm::vec4 &v) {
    glUniform4fv(uniLoc(name), 1, glm::value_ptr(v));
}
void uMat4(const char *name, const glm::mat4 &v) {
    glUniformMatrix4fv(uniLoc(name), 1, GL_FALSE, glm::value_ptr(v));
}
void uTexture(const char *name, GLuint tex, int unit, GLenum target) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, tex);
    glUniform1i(uniLoc(name), unit);
}
