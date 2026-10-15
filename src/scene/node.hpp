#pragma once

#include "../components/camera.hpp"
#include "../components/ecs.hpp"
#include "../util/guid.hpp"
#include <glm/ext/vector_float3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace scn {

// Decomposed position/rotation/scale, e.g. the result of collapsing a node's
// global transform on demand.
struct PosRotScale {
    glm::vec3 pos{0.f};
    glm::quat rot{1.f, 0.f, 0.f, 0.f};
    glm::vec3 scale{1.f};
};

class Node : public std::enable_shared_from_this<Node> {
  public:
    virtual ~Node() = default;

    // Tree structure. Change it only through add_child, which also
    // invalidates the cached global transforms.
    std::weak_ptr<Node> parent; // weak: parent owns children, not the reverse
    std::vector<std::shared_ptr<Node>> children;

    // Randomly assigned on creation, or read from the scene file. Used to
    // reference nodes (e.g. activeCamera) in scene files.
    util::Guid guid;

    // Attributes
    bool enabled;
    std::string name;

    // Local transform, stored decomposed so it can be edited (e.g. by an
    // inspector) without matrix decomposition. Only changeable through the
    // setters, so the cached matrices below know when to rebuild.
    const glm::vec3 &localPos() const {
        return pos_;
    }

    const glm::quat &localRot() const {
        return rot_;
    }

    const glm::vec3 &localScale() const {
        return scale_;
    }

    void setLocalPos(const glm::vec3 &pos);
    void setLocalRot(const glm::quat &rot);
    // Convenience for scene files, animations and hand written scenes:
    // (pitch, yaw, roll) in radians, converted to a quaternion
    void setLocalRotEuler(const glm::vec3 &rotEuler);
    void setLocalScale(const glm::vec3 &scale);

    // Cached: rebuilt only after this node or an ancestor changed
    glm::mat4 localTransform() const;
    glm::mat4 globalTransform() const;
    glm::vec3 globalPosition() const;
    PosRotScale globalPosRotScale() const;
    glm::quat globalRotation() const;
    // Identity for root nodes
    glm::mat4 parentGlobalTransform() const;

    // Setters take world space values and convert them into the local
    // fields through the parent chain
    void setGlobalTransform(const glm::mat4 &globalTransform);
    void setGlobalPosition(const glm::vec3 &pos);
    void setGlobalRotation(const glm::quat &rot);

    // ECS
    std::vector<std::shared_ptr<component::Component>> components;

    template <typename T, typename... Args>
    std::shared_ptr<T> add_component(Args &&...args) {
        static_assert(std::is_base_of_v<component::Component, T>,
                      "T must derive from component::Component");
        auto component = std::make_shared<T>(std::forward<Args>(args)...);
        component->node = shared_from_this();
        components.push_back(component);
        return component;
    }

    // Returns the first attached component of type T, or nullptr if none.
    template <typename T> std::shared_ptr<T> get_component() {
        for (auto &c : components) {
            if (c->type_id() == component::component_type_id<T>())
                return std::static_pointer_cast<T>(c);
        }
        return nullptr;
    }

    // Overridable functions
    void update(float dt);
    void draw(glm::mat4 projection, glm::mat4 invCameraTransform);

    // General functions
    static std::shared_ptr<Node> create(std::string name);
    static std::shared_ptr<Node> create(std::string name, glm::vec3 localPos);
    static std::shared_ptr<Node> create(std::string name,
                                        glm::mat4 localTransform);
    void add_child(std::shared_ptr<Node> child);
    void destroy();

    // Pretty-print this node and its subtree as a tree
    void print(std::ostream &os) const;
    std::string to_string() const;

  private:
    glm::vec3 pos_{0.f};
    glm::quat rot_{1.f, 0.f, 0.f, 0.f};
    glm::vec3 scale_{1.f};

    // Invariant: a node with a clean global cache has clean ancestors. So
    // once a node is dirty its whole subtree is too, and markGlobalDirty can
    // stop early.
    mutable glm::mat4 localCache{1.f};
    mutable glm::mat4 globalCache{1.f};
    mutable glm::quat globalRotCache{1.f, 0.f, 0.f, 0.f};
    mutable bool localDirty = true;
    mutable bool globalDirty = true;

    void markLocalDirty();  // local changed: rebuild local + subtree globals
    void markGlobalDirty(); // ancestor changed: rebuild subtree globals

    void print_subtree(std::ostream &os, int depth) const;
};

std::ostream &operator<<(std::ostream &os, const Node &node);

} // namespace scn
