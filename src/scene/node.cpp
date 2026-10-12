#include "node.hpp"

#include "../components/mesh.hpp"
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/matrix.hpp>
#include <memory>
#include <sstream>
#include <string>

namespace scn {

std::shared_ptr<Node> Node::create(std::string name) {
    std::shared_ptr<Node> node = std::make_shared<Node>();

    node->guid = util::Guid::generate();
    node->enabled = true;
    node->name = name;

    return node;
}

std::shared_ptr<Node> Node::create(std::string name, glm::vec3 localPos) {
    auto node = Node::create(name);
    node->setLocalPos(localPos);
    return node;
}

std::shared_ptr<Node> Node::create(std::string name, glm::mat4 localTransform) {
    auto node = Node::create(name);

    glm::vec3 skew, pos, scale;
    glm::vec4 perspective;
    glm::quat rot;
    glm::decompose(localTransform, scale, rot, pos, skew, perspective);
    node->setLocalPos(pos);
    node->setLocalRot(rot);
    node->setLocalScale(scale);

    return node;
}

void Node::update(float dt) {
    if (!enabled)
        return;

    for (auto &component : this->components)
        if (component->enabled)
            component->update(dt);

    for (auto const &child : this->children)
        child->update(dt);
}

void Node::draw(const std::shared_ptr<component::Camera> camera) {
    if (!enabled)
        return;

    std::weak_ptr<component::Mesh> mesh = get_component<component::Mesh>();
    if (!mesh.expired() && mesh.lock()->enabled)
        mesh.lock()->draw(camera);

    for (auto const &child : this->children)
        child->draw(camera);
}

void Node::add_child(std::shared_ptr<Node> child) {
    this->children.push_back(child);
    child->parent = shared_from_this();
    child->markGlobalDirty();
}

void Node::setLocalPos(const glm::vec3 &pos) {
    pos_ = pos;
    markLocalDirty();
}

void Node::setLocalRot(const glm::quat &rot) {
    rot_ = rot;
    markLocalDirty();
}

void Node::setLocalRotEuler(const glm::vec3 &rotEuler) {
    setLocalRot(glm::quat(rotEuler));
}

void Node::setLocalScale(const glm::vec3 &scale) {
    scale_ = scale;
    markLocalDirty();
}

void Node::markLocalDirty() {
    localDirty = true;
    markGlobalDirty();
}

void Node::markGlobalDirty() {
    // Already dirty -> whole subtree already dirty (see invariant)
    if (globalDirty)
        return;
    globalDirty = true;
    for (const auto &child : children)
        child->markGlobalDirty();
}

glm::mat4 Node::localTransform() const {
    if (localDirty) {
        localCache = glm::translate(glm::mat4(1.f), pos_) *
                     glm::mat4_cast(rot_) * glm::scale(glm::mat4(1.f), scale_);
        localDirty = false;
    }
    return localCache;
}

glm::mat4 Node::globalTransform() const {
    if (globalDirty) {
        // Parent first: its cache is clean afterwards, keeping the invariant
        if (auto p = parent.lock()) {
            globalCache = p->globalTransform() * localTransform();
            globalRotCache = p->globalRotation() * rot_;
        } else {
            globalCache = localTransform();
            globalRotCache = rot_;
        }
        globalDirty = false;
    }
    return globalCache;
}

glm::vec3 Node::globalPosition() const {
    // Translation column, no need for a full decompose
    return glm::vec3(globalTransform()[3]);
}

glm::mat4 Node::parentGlobalTransform() const {
    if (auto p = parent.lock())
        return p->globalTransform();
    return glm::mat4(1.f);
}

glm::quat Node::globalRotation() const {
    // Just the rotations, scale doesn't affect them (ignores the skew a
    // non-uniformly scaled parent would add). Cached with the transform.
    globalTransform();
    return globalRotCache;
}

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
    const glm::vec3 pos = pos_;
    os << "  [" << pos.x << ", " << pos.y << ", " << pos.z << "]\n";

    for (const auto &component : components) {
        for (int i = 0; i < depth + 1; ++i)
            os << "  ";
        os << "* " << component->type_name();
        std::string info = component->info();
        if (!info.empty())
            os << " (" << info << ")";
        os << "\n";
    }

    for (const auto &child : children)
        child->print_subtree(os, depth + 1);
}

void Node::print(std::ostream &os) const {
    print_subtree(os, 0);
}

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

void Node::setGlobalTransform(const glm::mat4 &globalTransform) {
    glm::mat4 local = glm::inverse(parentGlobalTransform()) * globalTransform;
    glm::quat rot;
    glm::vec3 skew, pos, scale;
    glm::vec4 perspective;
    glm::decompose(local, scale, rot, pos, skew, perspective);
    setLocalPos(pos);
    setLocalRot(rot);
    setLocalScale(scale);
}

void Node::setGlobalPosition(const glm::vec3 &pos) {
    // Fast path: under an identity parent (e.g. the scene root) local is
    // already world space, skip the matrix inverse
    glm::mat4 parentTf = parentGlobalTransform();
    if (parentTf == glm::mat4(1.f)) {
        setLocalPos(pos);
        return;
    }
    setLocalPos(glm::vec3(glm::inverse(parentTf) * glm::vec4(pos, 1.f)));
}

void Node::setGlobalRotation(const glm::quat &rot) {
    glm::quat parentRot(1.f, 0.f, 0.f, 0.f);
    if (auto p = parent.lock())
        parentRot = p->globalRotation();
    // Fast path: identity parent rotation, skip the extra quat product
    if (parentRot == glm::quat(1.f, 0.f, 0.f, 0.f)) {
        setLocalRot(rot);
        return;
    }
    setLocalRot(glm::inverse(parentRot) * rot);
}

} // namespace scn
