#pragma once

#include "ecs.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>

namespace ecs {

class Camera : public ComponentBase<Camera> {
  public:
    Camera(float fovDegrees, float width, float height) {
        this->projection = glm::perspectiveFov(
            glm::radians(fovDegrees), width, height, (float)1e-3, (float)1e+4);
    }

    glm::mat4 projection;

    // Helper functions
    // Orients (and repositions to `eye`, if given) the owning node so the
    // camera faces `target`. Defined in camera.cpp: needs the full scn::Node
    // definition, which scene.hpp can't provide here without a circular
    // include (scene.hpp includes this header before declaring Node).
    void lookAt(glm::vec3 target, glm::vec3 up = glm::vec3(0.f, 1.f, 0.f));
};
} // namespace ecs
