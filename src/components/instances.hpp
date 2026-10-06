#pragma once

#include "ecs.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <vector>

namespace component {

// One copy of the mesh, relative to the owner node
struct Instance {
    glm::vec3 position{0.f};
    glm::vec3 rotation{0.f}; // Euler angles (radians)
    glm::vec3 scale{1.f};

    glm::mat4 matrix() const;
};

// Recipe steps, run in order by instgen::runRecipe (instance_generators.hpp).
// generate* steps append instances, randomize* steps modify every instance
// generated so far.
struct GenerateCircle {
    glm::vec3 center{0.f};
    float radius = 1.f;
    glm::vec3 up{0.f, 1.f, 0.f};
    int count = 0;
};

struct GenerateDisc {
    glm::vec3 center{0.f};
    float innerRadius = 0.f;
    float outerRadius = 1.f;
    glm::vec3 up{0.f, 1.f, 0.f};
    int count = 0;
};

struct GenerateGrid {
    glm::vec3 center{0.f};
    float cellSize = 1.f;
    glm::ivec2 cellCount{1};
    glm::vec3 up{0.f, 1.f, 0.f};
    int count = 0;
};

struct GeneratePlane {
    glm::vec3 center{0.f};
    glm::vec2 size{1.f};
    glm::vec3 up{0.f, 1.f, 0.f};
    int count = 0;
};

// Uniform scale, so the mesh keeps its proportions
struct RandomizeScale {
    float min = 1.f;
    float max = 1.f;
};

// Offset added to the position
struct RandomizePosition {
    glm::vec3 min{0.f};
    glm::vec3 max{0.f};
};

// Euler angles (radians) added to the rotation
struct RandomizeRotation {
    glm::vec3 min{0.f};
    glm::vec3 max{0.f};
};

using InstanceStep =
    std::variant<GenerateCircle, GenerateDisc, GenerateGrid, GeneratePlane,
                 RandomizeScale, RandomizePosition, RandomizeRotation>;

// Scene files can store this instead of the transforms, the seed keeps the
// result the same on every load
struct InstanceRecipe {
    std::uint32_t seed = 0;
    std::vector<InstanceStep> steps;
};

// Draws the node's Mesh once per instance in a single instanced draw call.
// The Mesh needs an instanced shader, and its model must not be shared with
// non-instanced meshes (the instance buffer is attached to the model).
class Instances : public ComponentBase<Instances> {
  public:
    explicit Instances(std::vector<Instance> instances = {})
        : instances(std::move(instances)) {}

    // Uploads on the next update()
    void setInstances(std::vector<Instance> instances);
    const std::vector<Instance> &getInstances() const {
        return instances;
    }

    void update(double dt) override;

    const char *type_name() const override {
        return "Instances";
    }

    std::string info() const override {
        return std::to_string(instances.size()) + " instances";
    }

  private:
    std::vector<Instance> instances;
    bool dirty = true;
};

} // namespace component
