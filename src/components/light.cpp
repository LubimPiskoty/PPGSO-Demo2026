#include "light.hpp"
#include "../scene/scene.hpp"

namespace component {

    glm::vec3 Light::direction() const {
        auto n = node.lock();
        if (!n)
            return {0.f, -1.f, 0.f};
        return glm::normalize(n->globalPosRotScale().rot * glm::vec3(0.f, 0.f, -1.f));
    }

    glm::vec3 Light::position() const {
        auto n = node.lock();
        return n ? n->globalPosition() : glm::vec3(0.f);
    }


    // translate for buffer to gpu
    render::GpuLight Light::toGpu() const {
        render::GpuLight g{};
        g.position    = glm::vec4(position(), (float)type);           // xyz pos, w typ
        g.direction   = glm::vec4(direction(), 0.f);                  // xyz dir
        g.color       = glm::vec4(color * intensity, ambient);        // rgb col, w ambient
        g.attenuation = glm::vec4(constant, linear, quadratic, 0.f);  // point light
        return g;
    }

} // namespace component