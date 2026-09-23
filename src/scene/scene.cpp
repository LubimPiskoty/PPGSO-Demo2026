#include "scene.hpp"

#include "../ecs/mesh.hpp"
#include <cassert>
#include <cstdio>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/matrix.hpp>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace scn {

Guid Node::make_guid() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<Guid> dist;
    return dist(rng);
}

std::string Node::guid_to_string(Guid guid) {
    std::ostringstream os;
    os << std::hex << std::setw(16) << std::setfill('0') << guid;
    return os.str();
}

Guid Node::guid_from_string(const std::string &str) {
    Guid guid;
    std::istringstream is(str);
    is >> std::hex >> guid;
    if (is.fail())
        throw std::invalid_argument("invalid guid string: " + str);
    return guid;
}

std::string Node::guid_string() const { return guid_to_string(guid); }

std::shared_ptr<Node> Node::create(std::string name) {
    std::shared_ptr<Node> node = std::make_shared<Node>();

    node->guid = make_guid();
    node->enabled = true;
    node->name = name;

    return node;
}

std::shared_ptr<Node> Node::create(std::string name, glm::vec3 localPos) {
    auto node = Node::create(name);
    node->localPos = localPos;
    return node;
}

std::shared_ptr<Node> Node::create(std::string name, glm::mat4 localTransform) {
    auto node = Node::create(name);

    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(localTransform, node->localScale, node->localRot,
                    node->localPos, skew, perspective);

    return node;
}

void Node::update(double dt) {
    if (!enabled)
        return;

    for (auto &component : this->components)
        if (component->enabled)
            component->update(dt);

    for (auto const &child : this->children)
        child->update(dt);
}
void Node::draw(const std::shared_ptr<ecs::Camera> camera) {
    if (!enabled)
        return;

    std::weak_ptr<ecs::Mesh> mesh = get_component<ecs::Mesh>();
    if (!mesh.expired() && mesh.lock()->enabled)
        mesh.lock()->draw(camera);

    for (auto const &child : this->children)
        child->draw(camera);
}

void Node::add_child(std::shared_ptr<Node> child) {
    this->children.push_back(child);
    child->parent = shared_from_this();
}

glm::mat4 Node::localTransform() const {
    return glm::translate(glm::mat4(1.f), localPos) *
           glm::mat4_cast(localRot) *
           glm::scale(glm::mat4(1.f), localScale);
}

glm::mat4 Node::globalTransform() const {
    glm::mat4 transform = this->localTransform();
    for (auto p = parent.lock(); p; p = p->parent.lock())
        transform = p->localTransform() * transform;
    return transform;
}

glm::vec3 Node::globalPosition() const { return globalPosRotScale().pos; }

PosRotScale Node::globalPosRotScale() const {
    PosRotScale result;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(globalTransform(), result.scale, result.rot, result.pos,
                    skew, perspective);
    return result;
}

void Node::print_subtree(std::ostream &os, int depth) const {
    for (int i = 0; i < depth; ++i)
        os << "  ";
    os << "- " << name;
    if (!enabled)
        os << " (disabled)";
    const glm::vec3 pos = localPos;
    os << "  [" << pos.x << ", " << pos.y << ", " << pos.z << "]\n";

    for (const auto &component : components) {
        for (int i = 0; i < depth + 1; ++i)
            os << "  ";
        os << "* " << component->type_name() << "\n";
    }

    for (const auto &child : children)
        child->print_subtree(os, depth + 1);
}

void Node::print(std::ostream &os) const { print_subtree(os, 0); }

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

Scene::Scene() { this->root = Node::create("root"); }

void Scene::draw() {
    if (activeCamera.expired()) {
        std::puts("No camera is active!! Output will be blank");
        return;
    }
    this->root->draw(activeCamera.lock());
}

void Scene::update(double dt) { this->root->update(dt); }

std::shared_ptr<Node> Scene::findByGuid(Guid guid) const {
    std::vector<std::shared_ptr<Node>> stack{root};
    while (!stack.empty()) {
        auto node = stack.back();
        stack.pop_back();
        if (node->guid == guid)
            return node;
        for (auto const &child : node->children)
            stack.push_back(child);
    }
    return nullptr;
}

}; // namespace scn
