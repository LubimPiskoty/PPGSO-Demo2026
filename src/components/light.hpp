#pragma once
#include "ecs.hpp"
#include "../render/light_buffer.hpp"
#include <glm/glm.hpp>
#include <string>

namespace component {

    // type of light
    enum class LightType { Directional = 0, Point = 1 };

    class Light : public ComponentBase<Light> {
    public:
        LightType type = LightType::Directional;
        glm::vec3 color{1.f};
        float intensity = 0.8f;
        float ambient = 0.25f;

        // params for point
        float constant = 1.f;
        float linear = 0.09f;
        float quadratic = 0.032f;

        // no type -> is always sun
        explicit Light(LightType type = LightType::Directional) : type(type) {}

        const char *type_name() const override { return "Light"; }

        glm::vec3 position() const;          // get light pos
        glm::vec3 direction() const;    // get light dir
        render::GpuLight toGpu() const; // put lights to gpu
    };

} // namespace component