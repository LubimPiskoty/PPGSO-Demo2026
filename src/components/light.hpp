#pragma once
#include "ecs.hpp"
#include <glm/glm.hpp>
#include <string>

namespace component {

    class Light : public ComponentBase<Light> {
    public:
        glm::vec3 color{1.f};
        float intensity = 0.8f;
        float ambient = 0.25f;

        const char *type_name() const override { return "Light"; }

        glm::vec3 direction() const;
    };

} // namespace component