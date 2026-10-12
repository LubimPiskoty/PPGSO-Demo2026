#pragma once

#include "../scene/scene.hpp"

namespace scenes {

// Hand built arena with mini stages for bouncing, sliding, impacts and
// stacking. Needs a current GL context. The camera is created 1x1, the
// SceneManager sizes it.
scn::Scene physicsTest();

} // namespace scenes
