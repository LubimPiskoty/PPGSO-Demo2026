#include "instance_generators.hpp"

#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace component::instgen {

// Plane perpendicular to up. For up = +Y, u is X and v is Z.
struct Basis {
    glm::vec3 u, v;
    glm::quat rotation; // Maps X, Y, Z to u, up, v
};

static Basis makeBasis(glm::vec3 up) {
    up = glm::normalize(up);
    // Any axis that isn't parallel to up works as a reference
    glm::vec3 ref = std::abs(up.x) < 0.9f ? glm::vec3(1.f, 0.f, 0.f)
                                          : glm::vec3(0.f, 0.f, 1.f);
    glm::vec3 u = glm::normalize(ref - up * glm::dot(ref, up));
    glm::vec3 v = glm::cross(u, up);
    return {u, v, glm::quat_cast(glm::mat3(u, up, v))};
}

// yaw turns the instance around up, on top of the basis rotation
static Instance makeInstance(glm::vec3 position, const Basis &basis,
                             float yaw = 0.f) {
    glm::quat rotation =
        basis.rotation * glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f));
    return {position, glm::eulerAngles(rotation), glm::vec3(1.f)};
}

// Instance at angle on a circle around center, +X pointing outward
static Instance onCircle(glm::vec3 center, float radius, float angle,
                         const Basis &basis) {
    glm::vec3 offset =
        radius * (std::cos(angle) * basis.u + std::sin(angle) * basis.v);
    // Rotating by -angle around up turns +X (u) towards the offset
    return makeInstance(center + offset, basis, -angle);
}

std::vector<Instance> generateDisc(glm::vec3 center, float innerRadius,
                                   float outerRadius, glm::vec3 up, int count) {
    std::vector<Instance> instances;
    if (count <= 0)
        return instances;
    instances.reserve(count);
    Basis basis = makeBasis(up);

    // Sunflower spiral: the golden angle between neighbours, and the radius
    // picked so every instance covers the same area of the ring
    const float goldenAngle = glm::pi<float>() * (3.f - std::sqrt(5.f));
    float inner2 = innerRadius * innerRadius;
    float outer2 = outerRadius * outerRadius;
    for (int i = 0; i < count; i++) {
        float t = (i + 0.5f) / count;
        float r = std::sqrt(inner2 + (outer2 - inner2) * t);
        instances.push_back(onCircle(center, r, i * goldenAngle, basis));
    }
    return instances;
}

static float randomRange(float min, float max, std::mt19937 &rng) {
    std::uniform_real_distribution<float> dist(0.f, 1.f);
    return min + (max - min) * dist(rng);
}

static glm::vec3 randomRange(glm::vec3 min, glm::vec3 max, std::mt19937 &rng) {
    // Separate statements, argument evaluation order is unspecified
    float x = randomRange(min.x, max.x, rng);
    float y = randomRange(min.y, max.y, rng);
    float z = randomRange(min.z, max.z, rng);
    return {x, y, z};
}

void randomizeScale(std::vector<Instance> &instances, float min, float max,
                    std::mt19937 &rng) {
    for (auto &instance : instances)
        instance.scale *= randomRange(min, max, rng);
}

void randomizePosition(std::vector<Instance> &instances, glm::vec3 min,
                       glm::vec3 max, std::mt19937 &rng) {
    for (auto &instance : instances)
        instance.position += randomRange(min, max, rng);
}

void randomizeRotation(std::vector<Instance> &instances, glm::vec3 min,
                       glm::vec3 max, std::mt19937 &rng) {
    for (auto &instance : instances)
        instance.rotation += randomRange(min, max, rng);
}

} // namespace component::instgen
