#pragma once

#include "../loaders/shader_loader.hpp"
#include <GL/glew.h>
#include <cstdio>
#include <string>

namespace render {

// https://www.geeks3d.com/20140704/tutorial-introduction-to-opengl-4-3-shader-storage-buffers-objects-ssbo-demo/
struct Buffer {
    GLuint bufferID = 0;
    GLenum target;
    GLenum usage;
    size_t size = 0;

    explicit Buffer(GLenum target = GL_SHADER_STORAGE_BUFFER, size_t size = 0,
                    const void *data = nullptr, GLenum usage = GL_DYNAMIC_COPY)
        : target(target), usage(usage) {
        glGenBuffers(1, &bufferID);
        if (size > 0)
            setData(size, data);
    }

    ~Buffer() {
        if (bufferID)
            glDeleteBuffers(1, &bufferID);
    }

    Buffer(Buffer &&other) noexcept
        : bufferID(other.bufferID), target(other.target), usage(other.usage),
          size(other.size) {
        other.bufferID = 0;
    }

    void bind() const {
        glBindBuffer(target, bufferID);
    }

    // Bind to a non-default target, e.g. GL_DRAW_INDIRECT_BUFFER
    void bind(GLenum type) const {
        glBindBuffer(type, bufferID);
    }

    // Binds to `layout(std430, binding = index)` in the shader
    void bindSSBO(GLuint index) const {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, index, bufferID);
    }

    // (Re)allocates storage; data may be nullptr to just reserve space
    void setData(size_t newSize, const void *data) {
        size = newSize;
        glBindBuffer(target, bufferID);
        glBufferData(target, newSize, data, usage);
        glBindBuffer(target, 0);
    }
};

class ComputeShader {
  public:
    GLuint program;

    ComputeShader(std::string name) {
        const auto shader = loader::compileShader(
            GL_COMPUTE_SHADER, loader::loadShaderSource(name), name);

        program = glCreateProgram();
        glAttachShader(program, shader);
        glLinkProgram(program);

        glGetProgramiv(program, GL_COMPUTE_WORK_GROUP_SIZE, work_group_sizes);
    }

    void bind() const {
        glUseProgram(program);
    }

    void run(GLuint numX, GLuint numY = 1, GLuint numZ = 1) const {
        glDispatchCompute(numX, numY, numZ);
        glMemoryBarrier(GL_ALL_BARRIER_BITS); // Maybe optimalize later
    }

    int QueryLinearWorkGroupSize() {
        return work_group_sizes[0]; // Return the x-dimension of the work group
                                    // size
    }

  private:
    GLint work_group_sizes[3];
};
} // namespace render
