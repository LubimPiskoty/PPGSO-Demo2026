#pragma once
#include <glm/glm.hpp>
#define MAX_LIGHTS 16
#include <vector>

namespace render {

    // one lioght def
    struct GpuLight {
        glm::vec4 position;     // xyz = position, w = type -> 0 sun/1 lightbulb
        glm::vec4 direction;    // xyz = direciton
        glm::vec4 color;        // rgb = color, w = ambient
        glm::vec4 attenuation;  // x = constant, y = linear, z = quadratic
    };

    struct LightBlock {
        glm::vec4 position[MAX_LIGHTS];      // xyz pos, w type
        glm::vec4 direction[MAX_LIGHTS];     // xyz dir
        glm::vec4 color[MAX_LIGHTS];         // rgb  olor, w ambient
        glm::vec4 attenuation[MAX_LIGHTS];   // x, y, z
        glm::vec4 viewPos;
        glm::ivec4 count;
    };

    class LightBuffer {
    public:
        void upload(const std::vector<GpuLight> &lights, const glm::vec3 &viewPos);
    private:
        unsigned int ubo = 0;
    };

} // namespace render