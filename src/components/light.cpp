#include "light.hpp"
#include "../scene/scene.hpp"

namespace component {

    glm::vec3 Light::direction() const {
        auto n = node.lock();
        if (!n)
            return {0.f, -1.f, 0.f};
        return glm::normalize(n->globalPosRotScale().rot * glm::vec3(0.f, 0.f, -1.f));
    }

} // namespace component