#pragma once

#include "shape.hpp"

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

    // World position: translation column of the transform
    glm::vec3 pos() const {
        return glm::vec3(tf[3]);
    }
};

struct Contact {
    Body a; // first body (the plane or box comes first in mixed pairs)
    Body b; // second body
    glm::vec3 normal; // unit length, points from a toward b
    glm::vec3 point;  // world-space contact point
    float depth;      // penetration depth, > 0
};

struct Context {
    glm::vec3 gravity{0, -9.8f, 0};
    glm::vec3 windVel{0}; // TODO: make into vec field
    float drag = 0.3f;

    // Fixed timestep: real frame time (scaled by timeScale) is accumulated
    // and consumed in fixedDt sized steps, so the simulation is the same at
    // any frame rate or timescale. timeScale 0 pauses.
    float timeScale = 1.f;
    float fixedDt = 1 / 120.f;
    float accumulator = 0.f;

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
