#include "scene.hpp"

#include "../ecs/mesh.hpp"
#include <cassert>
#include <cstdio>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace scn {

std::shared_ptr<Node> Node::create(std::string name, glm::mat4 localTransform) {
    std::shared_ptr<Node> node = std::make_shared<Node>();

    node->children = std::vector<std::shared_ptr<Node>>();

    node->enabled = true;
    node->name = name;
    node->localTransform = localTransform;

    return node;
}

std::shared_ptr<Node> Node::create(std::string name, glm::vec3 localPos) {
    glm::mat4 localTransform = glm::translate(glm::mat4(1.f), localPos);
    return Node::create(name, localTransform);
}

std::shared_ptr<Node> Node::create(std::string name) {
    return Node::create(name, glm::mat4(1.f));
}

void Node::update(double dt) {
    if (!enabled)
        return;

    for (auto &component : this->components)
        component->update(dt);
}
void Node::draw(const std::shared_ptr<ecs::Camera> camera) {
    if (!enabled)
        return;

    std::weak_ptr<ecs::Mesh> mesh = get_component<ecs::Mesh>();
    if (!mesh.expired())
        mesh.lock()->draw(camera);

    for (auto const &child : this->children)
        child->draw(camera);
}

void Node::add_child(std::shared_ptr<Node> child) {
    this->children.push_back(child);
    child->parent = shared_from_this();
}

glm::vec3 Node::globalPosition() const { return globalTransform()[3]; }

glm::mat4 Node::globalTransform() const {
    glm::mat4 transform = this->localTransform;
    for (auto p = parent.lock(); p; p = p->parent.lock())
        transform = p->localTransform * transform;
    return transform;
}

void Node::print_subtree(std::ostream &os, const std::string &prefix,
                         bool isLast, bool isRoot) const {
    os << prefix;
    if (!isRoot)
        os << (isLast ? "└── " : "├── ");
    os << name;
    if (!enabled)
        os << " (disabled)";
    const glm::vec3 pos(localTransform[3]);
    os << "  [" << pos.x << ", " << pos.y << ", " << pos.z << "]\n";

    const std::string childPrefix =
        isRoot ? prefix : prefix + (isLast ? "    " : "│   ");
    for (size_t i = 0; i < children.size(); ++i)
        children[i]->print_subtree(os, childPrefix, i + 1 == children.size(),
                                   false);
}

void Node::print(std::ostream &os) const { print_subtree(os, "", true, true); }

std::string Node::to_string() const {
    std::ostringstream os;
    print(os);
    return os.str();
}

std::ostream &operator<<(std::ostream &os, const Node &node) {
    node.print(os);
    return os;
}

[[deprecated("NotImplemented")]]
void Node::destroy() {}

[[deprecated("NotImplemented")]]
void Node::setGlobalTransform(const glm::mat4 &globalTransform) {}

Scene::Scene() { this->tree = Node::create("root"); }

void Scene::draw() {
    if (activeCamera.expired()) {
        std::puts("No camera is active!! Output will be blank");
        return;
    }
    this->tree->draw(activeCamera.lock());
}

void Scene::update(double dt) { this->tree->update(dt); }

}; // namespace scn
