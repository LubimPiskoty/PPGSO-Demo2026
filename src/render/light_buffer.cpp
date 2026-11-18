#include "light_buffer.hpp"
#include <GL/glew.h>
#include <algorithm>

namespace render {

LightBuffer::~LightBuffer() { glDeleteBuffers(1, &ssbo); }

LightBuffer::LightBuffer(LightBuffer &&other) noexcept
    : ssbo(other.ssbo), capacity(other.capacity) {
    other.ssbo = 0;
    other.capacity = 0;
}

LightBuffer &LightBuffer::operator=(LightBuffer &&other) noexcept {
    if (this != &other) {
        glDeleteBuffers(1, &ssbo);
        ssbo = other.ssbo;
        capacity = other.capacity;
        other.ssbo = 0;
        other.capacity = 0;
    }
    return *this;
}

void LightBuffer::upload(const std::vector<GpuLight> &lights,
                         const glm::vec3 &viewPos) {
    if (ssbo == 0)
        glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);

    // Reallocate only when the lights outgrow it, doubling so a slowly
    // growing count doesn't reallocate every frame
    size_t lightBytes = lights.size() * sizeof(GpuLight);
    size_t size = sizeof(LightHeader) + lightBytes;
    if (size > capacity) {
        capacity = std::max(size, capacity * 2);
        glBufferData(GL_SHADER_STORAGE_BUFFER, capacity, nullptr,
                     GL_DYNAMIC_DRAW);
    }

    LightHeader header{glm::vec4(viewPos, 1.f),
                       glm::ivec4((int)lights.size(), 0, 0, 0)};
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(header), &header);
    if (lightBytes)
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(header), lightBytes,
                        lights.data());

    // every frame attach THIS scene's buffer
    // (each scene has its own buffer, switching scenes would leave another one
    // attached)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, LIGHT_SSBO_BINDING, ssbo);
}

} // namespace render
