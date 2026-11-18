#pragma once

#include "ecs.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace component {

// One copy of the mesh, relative to the owner node
struct Instance {
    glm::vec3 position{0.f};
    glm::vec3 rotation{0.f}; // Euler angles (radians)
    glm::vec3 scale{1.f};

    glm::mat4 matrix() const;
};

// Draws the node's Mesh once per instance in a single instanced draw call.
// The Mesh needs an instanced shader, and its model must not be shared with
// non-instanced meshes (the instance buffer is attached to the model).
class Instances : public ComponentBase<Instances> {
  public:
    explicit Instances(std::vector<Instance> instances = {})
        : instances(std::move(instances)) {}

    // Instead of a fixed list, draw one instance per child node at the child's
    // local transform, refreshed every frame. The children can carry physics
    // bodies and no mesh, so a pile of bodies is a single draw call.
    bool followChildren = false;

    void update(float dt) override;

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
