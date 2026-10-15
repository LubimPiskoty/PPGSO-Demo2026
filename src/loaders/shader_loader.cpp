#include "shader_loader.hpp"

#include <cmrc/cmrc.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

CMRC_DECLARE(shaders);

namespace loader {

std::string loadShaderSource(const std::string &filepath) {
    auto fs = cmrc::shaders::get_filesystem();
    if (!fs.exists(filepath)) {
        std::cerr << "Embedded shader not found: " << filepath << std::endl;
        return "";
    }
    auto file = fs.open(filepath);
    return std::string(file.begin(), file.end());
}

GLuint compileShader(GLenum type, const std::string &source,
                     const std::string &name) {
    GLuint shader = glCreateShader(type);
    const char *src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
        std::cerr << "[ERROR]Shader compile error (" << name << "):\n" << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createShaderProgram(const std::string &vertexPath,
                           const std::string &fragmentPath) {
    std::string vertexSrc = loadShaderSource(vertexPath);
    std::string fragmentSrc = loadShaderSource(fragmentPath);

    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSrc, vertexPath);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSrc, fragmentPath);

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

    GLuint blockIndex = glGetUniformBlockIndex(program, "Lights");
    if (blockIndex != GL_INVALID_INDEX) {
        glUniformBlockBinding(program, blockIndex, 1);
    }

    // Shaders are linked into the program now; the standalone objects aren't
    // needed
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}
} // namespace loader
