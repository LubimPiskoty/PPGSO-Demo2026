#include "dof.hpp"

#include <algorithm>
#include <cmath>

namespace render {

Dof::Dof(PostProcessing &post) : post(post), pass(post.find("dof.fs")) {}

void Dof::update(float dt, float tiltShift) {
    if (pass == post.passCount() || !post.isEnabled(pass))
        return;
    // Five points averaged, so one blade or leaf there doesn't throw it
    constexpr float R = 0.02f;
    float sum = 0.f;
    for (const glm::vec2 offset :
         {glm::vec2(0.f), glm::vec2(-R, 0.f), glm::vec2(R, 0.f),
          glm::vec2(0.f, -R), glm::vec2(0.f, R)})
        sum += post.distanceAt(FOCUS_POINT + offset);
    const float target = std::clamp(sum / 5.f, FOCUS_MIN, FOCUS_MAX);
    // Eased toward it: the same speed whatever the frame rate
    focus = focus < 0.f ? target
                        : focus + (target - focus) *
                                      (1.f - std::exp(-std::max(dt, 0.f) / EASE));

    const GLuint program = post.program(pass);
    glUseProgram(program);
    glUniform1f(glGetUniformLocation(program, "uFocus"), focus);
    glUniform1f(glGetUniformLocation(program, "uTiltShift"),
                std::clamp(tiltShift, 0.f, 1.f));
    glUseProgram(0);
}

} // namespace render
