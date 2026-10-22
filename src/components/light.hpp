#pragma once
#include "../render/light_buffer.hpp"
#include "ecs.hpp"
#include <glm/glm.hpp>

namespace component {

// type of light, sent to the shader in GpuLight::position.w
enum class LightType { Directional = 0, Point = 1, Spot = 2 };

// Create through the factories and attach with add_component, e.g.
//   node->add_component<Light>(Light::point({1.f, .5f, .2f}, 3.f));
// Position and direction come from the node (direction = node's -Z).
class Light : public ComponentBase<Light> {
  public:
    LightType type = LightType::Directional;
    glm::vec3 color{1.f};
    float intensity = 0.8f;
    float ambient = 0.f; // every light adds its own, so usually only the sun

    // point + spot: 1 / (constant + linear * d + quadratic * d^2)
    float constant = 1.f;
    float linear = 0.09f;
    float quadratic = 0.032f;

    // spot: full brightness inside innerCone, fades out to outerCone
    // (half-angles in degrees)
    //float innerCone = 12.5f;
    //float outerCone = 17.5f;
    float cutOff = 12.5f;

    static Light directional(glm::vec3 color = glm::vec3(1.f),
                             float intensity = 0.8f, float ambient = 0.1f);

    static Light point(glm::vec3 color = glm::vec3(1.f), float intensity = 1.f,
                       float linear = 0.09f, float quadratic = 0.032f);

    // TODO: not rendered yet, lit.fs skips spot lights
    static Light spot(glm::vec3 color = glm::vec3(1.f), float intensity = 1.f,
                      float cutOff = 12.5f,
                      float linear = 0.09f, float quadratic = 0.032f);

    const char *type_name() const override { return "Light"; }

    glm::vec3 position() const;     // get light pos
    glm::vec3 direction() const;    // get light dir
    render::GpuLight toGpu() const; // put lights to gpu

  private:
    // Private so every light comes from a factory with matching settings
    explicit Light(LightType type) : type(type) {}
};

} // namespace component
