#pragma once

#include "postprocessing.hpp"
#include <GL/glew.h>

namespace render {

// Screen-space ambient occlusion, as https://learnopengl.com/Advanced-Lighting/SSAO
// builds it, for the ssao.fs post layer: the sample kernel (KERNEL_SIZE
// points in a hemisphere, more of them close to the middle) uploaded once,
// and the 4x4 noise texture of random turns about the normal, tiled over
// the screen. Seeded, so every run looks the same. Keep it alive as long as
// the PostProcessing: it owns the noise texture.
class Ssao {
  public:
    // Must match KERNEL_SIZE in ssao.fs
    static constexpr int KERNEL_SIZE = 64;

    // Sets up the pass running ssao.fs, nothing if there is none
    explicit Ssao(PostProcessing &post);
    ~Ssao();
    Ssao(const Ssao &) = delete;
    Ssao &operator=(const Ssao &) = delete;

  private:
    GLuint noise = 0;
};

} // namespace render
