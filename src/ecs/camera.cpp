#include "camera.hpp"

#include "../scene/scene.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace ecs {

Camera::Camera(float fovDegrees, float width, float height)
    : Camera(fovDegrees, width, height, 1e-3f, 1e+4f) {}

Camera::Camera(float fovDegrees, float width, float height, float near,
               float far) {
    this->fov = glm::radians(fovDegrees);
    this->width = width;
    this->height = height;
    this->near = near;
    this->far = far;
    calculateProjection();
}

void Camera::lookAt(glm::vec3 target, glm::vec3 up) {
    auto owner = node.lock();
    if (!owner)
        return;

    glm::vec3 eye = owner->globalPosition();
    // glm::lookAt builds a world->view (eye) matrix; the node's transform is
    // view->world (camera-to-world), so we need its inverse.
    glm::mat4 transform = glm::inverse(glm::lookAt(eye, target, up));

    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(transform, owner->localScale, owner->localRot,
                   owner->localPos, skew, perspective);
}

float Camera::getFov() {
    return fov;
    calculateProjection();
}
float Camera::getNear() {
    return near;
    calculateProjection();
}
float Camera::getFar() {
    return far;
    calculateProjection();
}
float Camera::getWidth() {
    return width;
    calculateProjection();
}
float Camera::getHeight() {
    return height;
    calculateProjection();
}
void Camera::setNearFar(float near, float far) {
    this->near = near;
    this->far = far;
    calculateProjection();
}
void Camera::setSize(float width, float height) {
    this->width = width;
    this->height = height;
    calculateProjection();
}

void Camera::calculateProjection() {
    projection = glm::perspectiveFov(fov, width, height, near, far);
}

} // namespace ecs
