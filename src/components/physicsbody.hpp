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
        // Apply forces (gravity, wind drag)
        body.force =
            body.mass * ctx.gravity + ctx.drag * (ctx.windVel - body.velocity);

        // Integrate
        // Semi-implicit euler
        body.velocity += body.force * body.invMass * dt;
        node.lock()->localPos += body.velocity * dt;

        // Angular velocity
    }

    const char *type_name() const override {
        return "PhysicsBody";
    }
};
} // namespace component
