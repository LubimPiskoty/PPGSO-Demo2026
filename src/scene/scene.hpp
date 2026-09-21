#pragma once

#include <glm/ext/vector_float3.hpp>
#include <glm/matrix.hpp>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace scn {
class Node : public std::enable_shared_from_this<Node> { // TODO: Make into ECS
  public:
    virtual ~Node() = default;

    // Tree structure
    std::weak_ptr<Node> parent; // weak: parent owns children, not the reverse
    std::vector<std::shared_ptr<Node>> children;

    // Attributes
    bool enabled;
    std::string name;
    glm::mat4 localTransform;
    glm::mat4 globalTransform();
    void setGlobalTransform(glm::mat4 globalTransform);

    // Overridable functions
    virtual void update(double dt);
    virtual void draw();

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
    void print_subtree(std::ostream &os, const std::string &prefix, bool isLast,
                       bool isRoot) const;
};

std::ostream &operator<<(std::ostream &os, const Node &node);

class Scene {
  public:
    Scene();

    // Call on whole scene graph
    void update(double dt);
    void draw();

  public:
    std::shared_ptr<Node> tree;
};

} // namespace scn
