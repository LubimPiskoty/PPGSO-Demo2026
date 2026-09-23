#pragma once

#include "ecs.hpp"
#include <glm/glm.hpp>

namespace ecs {

class UserMovement : public ComponentBase<ecs::UserMovement> {
  public:
    // Keys are GLFW key codes; letters are their uppercase ASCII ('W').
    UserMovement(float speed, float sensitivity)
        : UserMovement(speed, sensitivity, 'W', 'A', 'S', 'D', ' ', 'C') {}
    UserMovement(float speed, float sensitivity, int forward_key, int left_key,
                 int backward_key, int right_key, int up_key, int down_key)
        : speed(speed), sensitivity(sensitivity), up_key(up_key),
          down_key(down_key), forward_key(forward_key),
          backward_key(backward_key), left_key(left_key), right_key(right_key) {
    }

    void update(double dt) override;
    void handleInput(int key, bool is_pressed);
    // Right mouse button toggles mouse look, cursor position drives rotation.
    void handleMouseButton(int button, bool is_pressed);
    void handleMouseMove(double x, double y);
    const char *type_name() const override { return "UserMovement"; }

    glm::vec3 input_vector{0.f};
    float speed;
    float sensitivity; // degrees per pixel
    bool looking = false;
    int up_key, down_key, forward_key, backward_key, left_key, right_key;

  private:
    // Held state per key, so releasing one key doesn't cancel the other
    // direction on the same axis.
    bool forward_held = false, backward_held = false, left_held = false,
         right_held = false, up_held = false, down_held = false;

    // Radians. Rotation is rebuilt as yaw (world Y) * pitch (local X), so the
    // camera never rolls.
    float yaw = 0.f, pitch = 0.f;
    double last_x = 0.0, last_y = 0.0;
    bool first_move = true;
};
} // namespace ecs
