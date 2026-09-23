#pragma once

#include <cstddef>
#include <memory>

namespace scn {
class Node;
} // namespace scn

namespace ecs {

using ComponentTypeId = std::size_t;

namespace detail {
inline ComponentTypeId next_component_type_id() {
    static ComponentTypeId counter = 0;
    return counter++;
}
} // namespace detail

// Unique, stable id per concrete component type (lazily assigned on first
// use). Lets us cast a shared_ptr<Component> back to its concrete type
// without dynamic_cast/RTTI.
template <typename T> ComponentTypeId component_type_id() {
    static const ComponentTypeId id = detail::next_component_type_id();
    return id;
}

class Component {
  public:
    bool enabled;

    Component() { enabled = true; }
    virtual ~Component() = default;

    virtual void update(double dt) {}
    virtual ComponentTypeId type_id() const = 0;
    virtual const char *type_name() const = 0;

    // The node this component is attached to.
    // weak_ptr: the node owns the component, not the other way round.
    std::weak_ptr<scn::Node> node;
};

// Concrete components derive from ComponentBase<Self> instead of Component
// directly so they don't have to hand-write type_id().
template <typename Derived> class ComponentBase : public Component {
  public:
    ComponentTypeId type_id() const override {
        return component_type_id<Derived>();
    }
    // type_name() is left for Derived to override with a clean, human-picked
    // name (e.g. "Camera") instead of a mangled typeid name.
};

} // namespace ecs
