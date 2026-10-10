#pragma once
#include <glm/glm.hpp>

namespace render {

    struct LightBlock {
        glm::vec4 direction;   // xyz
        glm::vec4 color;       // rgb
        glm::vec4 viewPos;     // xyz -> for camera
    };

    class LightBuffer {
    public:
        void upload(const LightBlock &block);
    private:
        unsigned int ubo = 0;
    };

} // namespace render