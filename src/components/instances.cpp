#include "instances.hpp"

#include "../scene/scene.hpp"
#include "mesh.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace component {

// Same composition as Node::localTransform
glm::mat4 Instance::matrix() const {
    return glm::translate(glm::mat4(1.f), position) *
           glm::mat4_cast(glm::quat(rotation)) *
           glm::scale(glm::mat4(1.f), scale);
}

void Instances::setInstances(std::vector<Instance> instances) {
    this->instances = std::move(instances);
    dirty = true;
}

void Instances::update(double dt) {
    if (!dirty)
        return;
    auto owner = node.lock();
    if (!owner)
        return;
    auto mesh = owner->get_component<Mesh>();
    if (!mesh || !mesh->model)
        return;

    std::vector<glm::mat4> transforms;
    transforms.reserve(instances.size());
    for (const auto &instance : instances)
        transforms.push_back(instance.matrix());
    mesh->model->makeInstanced(transforms);
    dirty = false;
}

} // namespace component
