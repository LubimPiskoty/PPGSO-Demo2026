#include "camera.hpp"

#include "../scene/scene.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace ecs {

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

} // namespace ecs
