#pragma once
#include "compute_shader.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>

namespace render {

// Forward+ (forward_plus.comp): after the depth prepass, the screen in
// tiles, each tile's list of the lights that reach its frustum (from its
// nearest to its farthest depth), so lit.fs shades a pixel with only those
// instead of every light in the scene. Two lists a tile, for the opaque and
// the transparent pass. gbuffer_debug.fs shows how many lights each tile
// got.
class LightCulling {
  public:
    // Must match LIGHTS_PER_TILE in forward_plus.comp: past it a tile drops
    // lights
    static constexpr int LIGHTS_PER_TILE = 512;
    // Binding of the lists, the lights are on LIGHT_SSBO_BINDING (4)
    static constexpr unsigned int TILE_SSBO_BINDING = 5;

    LightCulling();

    // The lights must be uploaded already (LightBuffer). depthTexture: the
    // prepass depth, resolved, width x height
    void run(GLuint depthTexture, int width, int height,
             const glm::mat4 &view, const glm::mat4 &projection);

  private:
    std::unique_ptr<ComputeShader> shader;
    Buffer tiles;
    int tileCount = 0;
};

} // namespace render
