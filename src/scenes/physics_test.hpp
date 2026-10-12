#pragma once

#include "../scene/scene.hpp"

// Small physics demo scenes, one scenario each, with a fixed camera framing
// it. Each is a floor enclosed by four walls so nothing can leave. All need
// a current GL context. Cameras are created 1x1, the SceneManager sizes
// them.
namespace scenes {

// Identical balls dropped from the same height, restitution 0.2 -> 0.95
scn::Scene physicsBounce();
// Balls rolling down ramps (finite planes) of 15, 30 and 45 degrees
scn::Scene physicsSlide();
// Head-on ball collisions: equal mass, and heavy vs light
scn::Scene physicsImpact();
// Pool style break: a cue ball rolled into a triangle of 10 balls
scn::Scene physicsBreak();

} // namespace scenes
