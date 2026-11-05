#include "light.hpp"
#include "../scene/scene.hpp"
#include "glm/gtc/constants.hpp"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace component {

Light Light::directional(glm::vec3 color, float intensity, float ambient) {
    Light light(LightType::Directional);
    light.color = color;
    light.intensity = intensity;
    light.ambient = ambient;
    return light;
}

Light Light::point(glm::vec3 color, float intensity, float linear,
                   float quadratic) {
    Light light(LightType::Point);
    light.color = color;
    light.intensity = intensity;
    light.linear = linear;
    light.quadratic = quadratic;
    return light;
}

Light Light::spot(glm::vec3 color, float intensity, float cutOff, float outerCutOff, float linear, float quadratic) {
    Light light(LightType::Spot);
    light.color = color;
    light.intensity = intensity;
    light.cutOff = cutOff;
    light.outerCutOff = outerCutOff;
    light.linear = linear;
    light.quadratic = quadratic;
    return light;
}

glm::vec3 Light::direction() const {
    auto n = node.lock();
    if (!n)
        return {0.f, -1.f, 0.f};
    return glm::normalize(n->globalPosRotScale().rot *
                          glm::vec3(0.f, 0.f, -1.f));
}

glm::vec3 Light::position() const {
    auto n = node.lock();
    return n ? n->globalPosition() : glm::vec3(0.f);
}

glm::mat4 Light::shadowView(int face) const {
    if (type == LightType::Point) {
        // Cube faces, same order the lit shader picks them in
        static const glm::vec3 dirs[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                          {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
        static const glm::vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1},
                                         {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
        face = glm::clamp(face, 0, 5);
        return glm::lookAt(position(), position() + dirs[face], ups[face]);
    }
    const glm::vec3 dir = direction();
    // lookAt breaks when looking straight along the up vector
    const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(1.f, 0.f, 0.f)
                                                 : glm::vec3(0.f, 1.f, 0.f);
    if (type == LightType::Directional) {
        // Box centered on the node: the eye sits half the depth range back
        const glm::vec3 eye = position() - dir * (shadowFar * 0.5f);
        return glm::lookAt(eye, eye + dir, up);
    }
    return glm::lookAt(position(), position() + dir, up);
}

glm::mat4 Light::shadowProjection() const {
    if (type == LightType::Spot)
        return glm::perspective(glm::radians(cutOff * 2.f), 1.f, shadowNear,
                                shadowFar);
    else if (type == LightType::Point)
        return glm::perspective(glm::half_pi<float>(), 1.f, shadowNear,
                                shadowFar);
    return glm::ortho(-shadowExtent, shadowExtent, -shadowExtent, shadowExtent,
                      shadowNear, shadowFar);
}

// translate for buffer to gpu
render::GpuLight Light::toGpu() const {
    render::GpuLight g;
    g.position = glm::vec4(position(), (float)type); // xyz pos, w typ
    g.direction = glm::vec4(direction(), 0.f);       // xyz dir, w cutoff
    g.cutOff = glm::vec4(glm::cos(glm::radians(cutOff)), glm::cos(glm::radians(outerCutOff)), 0.f, 0.f);
    g.color = glm::vec4(color * intensity, ambient); // rgb col, w ambient
    g.attenuation = glm::vec4(constant, linear, quadratic, 0.f); // point light
    g.shadow = glm::vec4(-1.f, -1.f, 0.f, 0.f); // Scene::uploadLights sets xy
    return g;
}

} // namespace component
