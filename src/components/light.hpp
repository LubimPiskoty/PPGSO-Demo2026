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

    // spot: half-angle of the cone in degrees (also the shadow map's FOV / 2)
    float cutOff = 12.5f;

    float outerCutOff = 17.5f;

    // Renders a shadow map for this light (point lights use 6 layers, one per
    // cube face)
    bool castsShadow = false;
    // Keep the map instead of rendering it every frame, for static casters
    bool bakedShadow = false;
    // Set it after changing a baked light, its map is rendered once more
    bool shadowDirty = true;
    // Renders a shadow map but adds no light. A directional one is a second
    // sun map of only the casters under its parent node (the other suns
    // leave those out): same rotation, a small box over those casters,
    // multiplied onto the sun's shadow.
    bool shadowOnly = false;
    // directional: half-size of the orthographic box, centered on the node
    float shadowExtent = 20.f;
    float shadowNear = 0.1f;
    float shadowFar = 50.f;

    static Light directional(glm::vec3 color = glm::vec3(1.f),
                             float intensity = 0.8f, float ambient = 0.1f);

    static Light point(glm::vec3 color = glm::vec3(1.f), float intensity = 1.f,
                       float linear = 0.09f, float quadratic = 0.032f);

    // cutOff / outerCutOff: half-angles in degrees, full light inside the
    // inner cone, fading out to the outer one
    static Light spot(glm::vec3 color = glm::vec3(1.f), float intensity = 1.f,
                      float cutOff = 12.5f, float outerCutOff = 17.5f,
                      float linear = 0.09f, float quadratic = 0.032f);

    const char *type_name() const override { return "Light"; }

    glm::vec3 position() const;     // get light pos
    glm::vec3 direction() const;    // get light dir
    render::GpuLight toGpu() const; // put lights to gpu

    // Matrices for rendering the scene from the light (shadow pass)
    // face: only point lights, 0..5 = +X -X +Y -Y +Z -Z
    glm::mat4 shadowView(int face = 0) const;
    glm::mat4 shadowProjection() const;
    // Layers of the shadow array this light needs: 6 for point, 1 otherwise
    int shadowLayers() const { return type == LightType::Point ? 6 : 1; }

  private:
    // Private so every light comes from a factory with matching settings
    explicit Light(LightType type) : type(type) {}
};

} // namespace component
