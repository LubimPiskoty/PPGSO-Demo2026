#pragma once
#include <glm/glm.hpp>
#define MAX_LIGHTS 160
#include <vector>

namespace render {

// one lioght def
struct GpuLight {
    glm::vec4 position;    // xyz = position, w = type -> 0 sun/1 lightbulb
    glm::vec4 direction;   // xyz = direction
    glm::vec4 color;       // rgb = color, w = ambient
    glm::vec4 attenuation; // x = constant, y = linear, z = quadratic
    glm::vec4 shadow;      // x = shadow map layer / ShadowBlock index, -1 = none
                           // y = directional: close-up map layer, -1 = none
                           // z = 0: in the suns' array, 1: the small one
    glm::vec4 cutOff;      // x = inner cutoff, y = outer cutoff
                           // z = shadow texel size, w = 1: per metre away
};

struct LightBlock {
    GpuLight lights[MAX_LIGHTS];
    glm::vec4 viewPos;
    glm::ivec4 count;
};

class LightBuffer {
  public:
    LightBuffer() = default;
    // Owns its UBO: move-only, freed with it
    ~LightBuffer();
    LightBuffer(LightBuffer &&other) noexcept;
    LightBuffer &operator=(LightBuffer &&other) noexcept;
    LightBuffer(const LightBuffer &) = delete;
    LightBuffer &operator=(const LightBuffer &) = delete;

    void upload(const std::vector<GpuLight> &lights, const glm::vec3 &viewPos);

  private:
    unsigned int ubo = 0;
};

} // namespace render
