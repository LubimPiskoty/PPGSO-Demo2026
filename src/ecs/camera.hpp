#pragma once

#include "ecs.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>

namespace ecs {

class Camera : public ComponentBase<Camera> {
  public:
    Camera(float fovDegrees, float width, float height);
    Camera(float fovDegrees, float width, float height, float near, float far);

    glm::mat4 projection;

    const char *type_name() const override { return "Camera"; }

    // Helper functions
    // Orients (and repositions to `eye`, if given) the owning node so the
    // camera faces `target`. Defined in camera.cpp: needs the full scn::Node
    // definition, which scene.hpp can't provide here without a circular
    // include (scene.hpp includes this header before declaring Node).
    void lookAt(glm::vec3 target, glm::vec3 up = glm::vec3(0.f, 1.f, 0.f));

    float getFov();
    float getNear();
    float getFar();
    float getWidth();
    float getHeight();
    void setNearFar(float near, float far);
    void setSize(float width, float height);

  private:
    float fov, near, far, width, height;

    void calculateProjection();
};
} // namespace ecs
