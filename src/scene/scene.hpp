#pragma once

#include "../ecs/camera.hpp"
#include "../ecs/ecs.hpp"
#include <glm/ext/vector_float3.hpp>
#include <glm/matrix.hpp>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace scn {
class Node : public std::enable_shared_from_this<Node> {
  public:
    virtual ~Node() = default;

    // Tree structure
    std::weak_ptr<Node> parent; // weak: parent owns children, not the reverse
    std::vector<std::shared_ptr<Node>> children;

    // Attributes
    bool enabled;
    std::string name;
    glm::mat4 localTransform;
    glm::mat4 globalTransform() const;
    glm::vec3 globalPosition() const;
    void setGlobalTransform(const glm::mat4 &globalTransform);

    // ECS
    std::vector<std::shared_ptr<ecs::Component>> components;

    template <typename T, typename... Args>
    std::shared_ptr<T> add_component(Args &&...args) {
        static_assert(std::is_base_of_v<ecs::Component, T>,
                      "T must derive from ecs::Component");
        auto component = std::make_shared<T>(std::forward<Args>(args)...);
        component->node = shared_from_this();
        components.push_back(component);
        return component;
    }

    // Returns the first attached component of type T, or nullptr if none.
    template <typename T> std::shared_ptr<T> get_component() {
        for (auto &c : components) {
            if (c->type_id() == ecs::component_type_id<T>())
                return std::static_pointer_cast<T>(c);
        }
        return nullptr;
    }

    // Overridable functions
    void update(double dt);
    void draw(const std::shared_ptr<ecs::Camera> camera);

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
    void print_subtree(std::ostream &os, int depth) const;
};

std::ostream &operator<<(std::ostream &os, const Node &node);

class Scene {
  public:
    std::weak_ptr<ecs::Camera> activeCamera;
    Scene();

    // Call on whole scene graph
    void update(double dt);
    void draw();

  public:
    std::shared_ptr<Node> tree;
};

} // namespace scn
