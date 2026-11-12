#include "light_buffer.hpp"
#include <GL/glew.h>
#include <cstdio>
#include <cstring>

namespace render {

LightBuffer::~LightBuffer() { glDeleteBuffers(1, &ubo); }

LightBuffer::LightBuffer(LightBuffer &&other) noexcept
    : ubo(other.ubo) {
    other.ubo = 0;
}

LightBuffer &LightBuffer::operator=(LightBuffer &&other) noexcept {
    if (this != &other) {
        glDeleteBuffers(1, &ubo);
        ubo = other.ubo;
        other.ubo = 0;
    }
    return *this;
}

void LightBuffer::upload(const std::vector<GpuLight> &lights,
                         const glm::vec3 &viewPos) {
    // create light buffer
    if (ubo == 0) {
        glGenBuffers(1, &ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(LightBlock), nullptr,
                     GL_DYNAMIC_DRAW);
    }

    // The shaders' block has room for MAX_LIGHTS, the rest would be dropped
    // without a word
    static bool warned = false;
    if (lights.size() > MAX_LIGHTS && !warned) {
        std::fprintf(stderr,
                     "LightBuffer: %zu lights, only the first %d are drawn "
                     "(MAX_LIGHTS in light_buffer.hpp and lit.fs)\n",
                     lights.size(), MAX_LIGHTS);
        warned = true;
    }

    LightBlock block{};
    for (int i = 0; i < (int)lights.size() && i < MAX_LIGHTS; i++) {
        block.lights[i] = lights[i];
        block.count.x++;
    }
    block.viewPos = glm::vec4(viewPos, 1.f);

    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightBlock), &block);

    // every frame attach THIS scene's buffer to slot 1
    // (each scene has its own buffer, switching scenes would leave another one
    // attached)
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo);
}

} // namespace render
