#include <GL/glew.h>
#include "light_buffer.hpp"

namespace render {

    void LightBuffer::upload(const LightBlock &block) {
        // create light buffer
        if (ubo == 0) {
            glGenBuffers(1, &ubo);
            glBindBuffer(GL_UNIFORM_BUFFER, ubo);
            glBufferData(GL_UNIFORM_BUFFER, sizeof(LightBlock), nullptr, GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo);
        }

        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightBlock), &block);
    }

} // namespace render