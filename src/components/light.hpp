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

    // Renders a shadow map for this light (point lights use 6 layers, one per
    // cube face)
    bool castsShadow = false;
    // directional: half-size of the orthographic box around the camera
    float shadowExtent = 20.f;
    // directional: box center is moved this fraction of shadowExtent ahead of
    // the camera (0 = centered on the camera, 1 = box starts at the camera)
    // directional: a second, small and sharp shadow box (layer 1) around the
    // camera, blended over the big one near its middle. 0 = off.
    float localShadowExtent = 0.f;
    float localShadowFar = 60.f;
    // Like shadowFocusAhead, for the local box
    float localShadowFocusAhead = 1.f;
    float shadowFocusAhead = 0.6f;
    float shadowNear = 0.1f;
    float shadowFar = 100.f;

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

    // Matrices for rendering the scene from the light (shadow pass)
    // focus: where a directional light's shadow box is centered (the camera)
    // face: only point lights, 0..5 = +X -X +Y -Y +Z -Z
    glm::mat4 shadowView(glm::vec3 focus = glm::vec3(0.f), int face = 0) const;
    // layer: directional lights with a local box, 0 = big, 1 = local
    glm::mat4 shadowProjection(int layer = 0) const;
    // Layers of the shadow array this light needs: 6 for point, 1 otherwise
    int shadowLayers() const {
        if (type == LightType::Point)
            return 6;
        return type == LightType::Directional && localShadowExtent > 0.f ? 2
                                                                           : 1;
    }

  private:
    // Private so every light comes from a factory with matching settings
    explicit Light(LightType type) : type(type) {}
};

} // namespace component
