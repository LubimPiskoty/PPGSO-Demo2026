#pragma once

#include <glm/matrix.hpp>
#include <glm/vec3.hpp>

namespace physics {
struct RigidBody {
    glm::vec3 velocity{0}, angularVelocity{0};
    glm::vec3 force{0}, torque{0};
    float mass = 1, invMass = 1, restitution = 0.5f;
    glm::mat3 invInertiaLocal{1}, invInertiaWorld{1};
};
} // namespace physics
