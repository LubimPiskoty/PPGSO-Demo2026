#pragma once
#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

namespace render {

// One light as the shaders read it (lit.fs, forward_plus.comp)
struct GpuLight {
    glm::vec4 position;    // xyz = position, w = type -> 0 sun/1 lightbulb
    glm::vec4 direction;   // xyz = direction
    glm::vec4 color;       // rgb = color, w = ambient
    glm::vec4 attenuation; // x = constant, y = linear, z = quadratic,
                           // w = reach (Forward+)
    glm::vec4 shadow;      // x = shadow map layer / ShadowBlock index, -1 = none
                           // z = 0: in the suns' array, 1: the small one
    glm::vec4 cutOff;      // x = inner cutoff, y = outer cutoff
                           // z = shadow texel size, w = 1: per metre away
};

// Start of lit.fs's Lights storage buffer, the GpuLight array follows it
// (std430: all vec4s, so no padding anywhere)
struct LightHeader {
    glm::vec4 viewPos;
    glm::ivec4 count; // x = number of lights
};

// Its binding point; the particles use SSBOs 0-3
constexpr unsigned int LIGHT_SSBO_BINDING = 4;

// lit.fs's Lights storage buffer, grows to fit any number of lights
class LightBuffer {
  public:
    LightBuffer() = default;
    // Owns its SSBO: move-only, freed with it
    ~LightBuffer();
    LightBuffer(LightBuffer &&other) noexcept;
    LightBuffer &operator=(LightBuffer &&other) noexcept;
    LightBuffer(const LightBuffer &) = delete;
    LightBuffer &operator=(const LightBuffer &) = delete;

    void upload(const std::vector<GpuLight> &lights, const glm::vec3 &viewPos);

  private:
    unsigned int ssbo = 0;
    size_t capacity = 0; // bytes allocated
};

} // namespace render
