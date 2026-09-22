#include "camera.hpp"

#include "../scene/scene.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace ecs {

void Camera::lookAt(glm::vec3 target, glm::vec3 up) {
    auto owner = node.lock();
    if (!owner)
        return;

    glm::vec3 eye = glm::vec3(owner->globalPosition());
    // glm::lookAt builds a world->view (eye) matrix; the node's transform is
    // view->world (camera-to-world), so we need its inverse.
    owner->localTransform = glm::inverse(glm::lookAt(eye, target, up));
}

} // namespace ecs
