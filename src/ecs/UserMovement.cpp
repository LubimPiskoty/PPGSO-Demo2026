#include "UserMovement.hpp"

#include "../scene/scene.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>

namespace ecs {

void UserMovement::update(double dt) {
    auto owner = this->node.lock();
    if (!owner)
        return;

    const glm::vec3 input_weights = glm::vec3(1, 0.5, 1);
    glm::vec3 input = input_vector * input_weights;
    // Horizontal input follows the view direction, vertical stays world up.
    glm::vec3 dir = owner->localRot * glm::vec3(input.x, 0.f, input.z) +
                    glm::vec3(0.f, input.y, 0.f);
    owner->localPos += dir * speed * (float)dt;
}

// W = (0, 0, -1), S = (0, 0, +1), A = (-1, 0, 0), D = (+1, 0, 0)
// Space = (0, +1, 0), C = (0, -1, 0)
void UserMovement::handleInput(int key, bool is_pressed) {
    if (key == forward_key)
        forward_held = is_pressed;
    else if (key == backward_key)
        backward_held = is_pressed;
    else if (key == left_key)
        left_held = is_pressed;
    else if (key == right_key)
        right_held = is_pressed;
    else if (key == up_key)
        up_held = is_pressed;
    else if (key == down_key)
        down_held = is_pressed;
    else
        return;

    input_vector = glm::vec3((float)right_held - (float)left_held,
                             (float)up_held - (float)down_held,
                             (float)backward_held - (float)forward_held);
}

void UserMovement::handleMouseButton(int button, bool is_pressed) {
    if (button != GLFW_MOUSE_BUTTON_RIGHT)
        return;

    looking = is_pressed;
    first_move = true;

    // Pick up yaw/pitch from the current rotation (e.g. set by lookAt) so the
    // view doesn't snap on the first drag.
    if (auto owner = node.lock(); owner && is_pressed) {
        glm::vec3 fwd = owner->localRot * glm::vec3(0.f, 0.f, -1.f);
        yaw = glm::atan(-fwd.x, -fwd.z);
        pitch = glm::asin(glm::clamp(fwd.y, -1.f, 1.f));
    }
}

void UserMovement::handleMouseMove(double x, double y) {
    if (!looking)
        return;
    auto owner = node.lock();
    if (!owner)
        return;

    // Skip the first event after starting a drag, otherwise the jump from
    // the old cursor position spins the camera.
    if (first_move) {
        last_x = x;
        last_y = y;
        first_move = false;
        return;
    }

    float dx = (float)(x - last_x);
    float dy = (float)(y - last_y);
    last_x = x;
    last_y = y;

    yaw -= glm::radians(dx * sensitivity);
    pitch -= glm::radians(dy * sensitivity);
    pitch = glm::clamp(pitch, glm::radians(-89.f), glm::radians(89.f));

    owner->localRot = glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f)) *
                      glm::angleAxis(pitch, glm::vec3(1.f, 0.f, 0.f));
}
} // namespace ecs
