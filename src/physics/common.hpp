#pragma once

#include "shape.hpp"

#include <cstddef>
#include <cstdint>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <utility>
#include <vector>

// physicsbody.hpp includes this header (it needs Context), so PhysicsBody is
// only forward declared here, a pointer doesn't need more.
namespace component {
class PhysicsBody;
}

// This is a graphics project so i wont waste time with proper physics engine
// just simple single threaded physics with accumulated fixed timestep
namespace physics {
typedef component::PhysicsBody *Entity;

// Per step snapshot of one body: its global transform and world AABB are
// computed once after integration and reused by the broad and narrow phase
struct Body {
    Entity e;
    glm::mat4 tf; // global transform
    AABB aabb;
    std::size_t index; // position in ctx.cache
    // Positional correction summed over all contacts of this step, applied
    // to the node once after solving
    glm::vec3 correction{0.f};

    // World position: translation column of the transform
    glm::vec3 pos() const {
        return glm::vec3(tf[3]);
    }
};

struct Contact {
    Body a; // first body (the plane comes first in plane-sphere pairs)
    Body b; // second body
    glm::vec3 normal; // unit length, points from a toward b
    glm::vec3 point;  // world-space contact point
    float depth;      // penetration depth, > 0
};

struct Context {
    glm::vec3 gravity{0, -9.8f, 0};
    glm::vec3 windVel{0}; // TODO: make into vec field
    float drag = 0.1f;
    // Angular velocity lost per second, independent of mass and size, so
    // small spins left over from contacts die out instead of lingering
    float angularDrag = 2.f;

    // Every frame advances the simulation by fixedDt * timeScale, split into
    // `substeps` equal steps. Not tied to real frame time, so physics runs
    // at real speed only at 60 fps (vsync). timeScale 0 pauses.
    float timeScale = 1.f;
    float fixedDt = 1 / 60.f;
    int substeps = 6;

    std::vector<Entity> bodies;
    // Rebuilt every step from `bodies`. `pairs` point into it, so they are
    // only valid until the next rebuild.
    std::vector<Body> cache;
    std::vector<std::pair<const Body *, const Body *>> pairs;
    std::vector<Contact> contacts;
};

// Advances all ctx.bodies by dt. Defined in solver.cpp
void step(Context &ctx, float dt);

} // namespace physics
