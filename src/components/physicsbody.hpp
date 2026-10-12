#pragma once

#include "../physics/common.hpp"
#include "../physics/rigidbody.hpp"
#include "../physics/shape.hpp"
#include "../scene/node.hpp"
#include "ecs.hpp"

namespace component {
class PhysicsBody : public ComponentBase<PhysicsBody> {
  public:
    physics::RigidBody body;
    physics::Shape collider;

    void physics_update(const physics::Context &ctx, float dt) {
        auto n = node.lock();
        // Static bodies never move
        if (!n || body.invMass == 0.f)
            return;

        // Apply forces (gravity, wind drag)
        body.force =
            body.mass * ctx.gravity + ctx.drag * (ctx.windVel - body.velocity);

        // Integrate
        // Semi-implicit euler
        // Works in world space and writes back through the parent chain,
        // so bodies don't have to be root children
        body.velocity += body.force * body.invMass * dt;
        n->setGlobalPosition(n->globalPosition() + body.velocity * dt);

        // Angular velocity
        // Inertia tensor rotated into world space: I_w^-1 = R I_l^-1 R^T
        glm::quat rot = n->globalRotation();
        glm::mat3 R = glm::mat3_cast(rot);
        body.invInertiaWorld = R * body.invInertiaLocal * glm::transpose(R);

        body.angularVelocity += body.invInertiaWorld * body.torque * dt;
        // Angular drag, so spinning bodies settle. Applied as damping
        // rather than a torque, so it works the same for any size and mass
        body.angularVelocity *= 1.f / (1.f + ctx.angularDrag * dt);

        // dq/dt = 0.5 * (0, w) * q, then renormalize to stay a rotation
        glm::quat spin(0.f, body.angularVelocity);
        rot += 0.5f * dt * spin * rot;
        rot = glm::normalize(rot);
        n->setGlobalRotation(rot);

        // Reset accumulators
        body.force = glm::vec3(0.f);
        body.torque = glm::vec3(0.f);
    }

    const char *type_name() const override {
        return "PhysicsBody";
    }
};
} // namespace component
