#pragma once

#include "../scene/node.hpp"
#include "ecs.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace component {

// Spins its node around an axis at a constant rate, replacing the node's
// local rotation. Children (a camera, a light) orbit with it.
class Spin : public ComponentBase<Spin> {
  public:
    explicit Spin(float degPerSec, glm::vec3 axis = glm::vec3(0.f, 1.f, 0.f))
        : speed(glm::radians(degPerSec)), axis(glm::normalize(axis)) {}

    const char *type_name() const override { return "Spin"; }

    void update(float dt) override {
        if (auto owner = node.lock()) {
            angle += speed * dt;
            owner->setLocalRot(glm::angleAxis(angle, axis));
        }
    }

    float speed; // radians per second
    glm::vec3 axis;

  private:
    float angle = 0.f;
};

} // namespace component
