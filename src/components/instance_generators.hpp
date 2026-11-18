#pragma once

// Procedural instance placement (the demo's corn field): a spread of
// instances, then randomized

#include "instances.hpp"
#include <glm/glm.hpp>
#include <random>
#include <vector>

namespace component::instgen {

// count instances spread over the ring between innerRadius and outerRadius,
// in the plane perpendicular to up, on a sunflower spiral. innerRadius 0
// fills the whole disc. Each instance is turned so its local +X points away
// from the center.
std::vector<Instance> generateDisc(glm::vec3 center, float innerRadius,
                                   float outerRadius, glm::vec3 up, int count);

// Multiplies each instance's scale by a random factor in [min, max]
void randomizeScale(std::vector<Instance> &instances, float min, float max,
                    std::mt19937 &rng);

// Adds a random offset in [min, max] (per axis) to each instance's position
void randomizePosition(std::vector<Instance> &instances, glm::vec3 min,
                       glm::vec3 max, std::mt19937 &rng);

// Adds random euler angles in [min, max] (per axis) to each instance's
// rotation
void randomizeRotation(std::vector<Instance> &instances, glm::vec3 min,
                       glm::vec3 max, std::mt19937 &rng);

} // namespace component::instgen
