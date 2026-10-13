#include <GL/glew.h>
#include "light_buffer.hpp"
#include <cstdio>

namespace render {

    void LightBuffer::upload(const std::vector<GpuLight> &lights,
                             const glm::vec3 &viewPos) {
        // create light buffer
        if (ubo == 0) {
            glGenBuffers(1, &ubo);
            glBindBuffer(GL_UNIFORM_BUFFER, ubo);
            glBufferData(GL_UNIFORM_BUFFER, sizeof(LightBlock), nullptr, GL_DYNAMIC_DRAW);
        }

        LightBlock block{};
        for (int i = 0; i < (int)lights.size() && i < MAX_LIGHTS; i++) {
            block.position[i]    = lights[i].position;
            block.direction[i]   = lights[i].direction;
            block.color[i]       = lights[i].color;
            block.attenuation[i] = lights[i].attenuation;
            block.count.x++;
        }
        block.viewPos = glm::vec4(viewPos, 1.f);

        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightBlock), &block);

        // every frame attach THIS scene's buffer to slot 1
        // (each scene has its own buffer, switching scenes would leave another one attached)
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo);

    }

} // namespace render