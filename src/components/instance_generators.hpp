#pragma once

// Procedural instance placement, used by the scene loader. Typical use:
//   generateGrid(center, cellSize, cellCount, up, count)
//   randomizePosition(min, max)
//   randomizeRotation(min, max)

#include "instances.hpp"
#include <glm/glm.hpp>
#include <random>
#include <vector>

namespace component::instgen {

// count instances evenly spaced on a circle in the plane perpendicular to
// up. Each instance is turned so its local +X points away from the center
// and local Z runs along the circle.
std::vector<Instance> generateCircle(glm::vec3 center, float radius,
                                     glm::vec3 up, int count);

// count instances spread over the ring between innerRadius and outerRadius,
// in the plane perpendicular to up. innerRadius 0 fills the whole disc.
// Turned like generateCircle.
std::vector<Instance> generateDisc(glm::vec3 center, float innerRadius,
                                   float outerRadius, glm::vec3 up, int count);

// count instances spread over a cellCount.x * cellCount.y grid of cellSize
// cells, centered at center, in the plane perpendicular to up
std::vector<Instance> generateGrid(glm::vec3 center, float cellSize,
                                   glm::ivec2 cellCount, glm::vec3 up,
                                   int count);

// count instances spread over a size.x * size.y rectangle, centered at
// center, in the plane perpendicular to up (with up = +Y, size.x runs along
// X and size.y along Z)
std::vector<Instance> generatePlane(glm::vec3 center, glm::vec2 size,
                                    glm::vec3 up, int count);

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

// Runs the steps in order with an rng seeded from recipe.seed
std::vector<Instance> runRecipe(const InstanceRecipe &recipe);

} // namespace component::instgen
