#pragma once

#include "postprocessing.hpp"
#include <glm/vec2.hpp>

namespace render {

// The depth of field layer's (dof.fs) uniforms, every frame: its focus
// distance, eased toward what's at FOCUS_POINT on the screen so it doesn't
// jump when something thin crosses that point, and how much tilt-shift
// replaces the lens (the scene's tiltShift).
class Dof {
  public:
    // Where on the screen the focus is taken: across the middle, 60 % down
    // (texture coordinates count up from the bottom)
    static constexpr glm::vec2 FOCUS_POINT{0.5f, 0.4f};
    // Closest and farthest the focus goes, m
    static constexpr float FOCUS_MIN = 1.f, FOCUS_MAX = 400.f;
    // Seconds the focus takes to get most (63 %) of the way to a new
    // distance
    static constexpr float EASE = 0.4f;

    explicit Dof(PostProcessing &post);

    // Before post.end() (it measures the last frame's depth, eased that's
    // the same). dt in real seconds; tiltShift 0..1.
    void update(float dt, float tiltShift);

  private:
    PostProcessing &post;
    size_t pass;
    float focus = -1.f; // m; below 0: not measured yet, take the first one
};

} // namespace render
