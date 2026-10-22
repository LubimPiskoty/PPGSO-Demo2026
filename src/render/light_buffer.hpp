#pragma once
#include <glm/glm.hpp>
#define MAX_LIGHTS 64
#include <vector>

namespace render {

// one lioght def
struct GpuLight {
    glm::vec4 position;    // xyz = position, w = type -> 0 sun/1 lightbulb
    glm::vec4 direction;   // xyz = direciton
    glm::vec4 color;       // rgb = color, w = ambient
    glm::vec4 attenuation; // x = constant, y = linear, z = quadratic
    glm::vec4 shadow;      // x = shadow map layer / ShadowBlock index, -1 = none
};

struct LightBlock {
    GpuLight lights[MAX_LIGHTS];
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
