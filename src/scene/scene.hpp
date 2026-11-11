#pragma once

#include "../components/camera.hpp"
#include "../components/light.hpp"
#include "../components/physicsbody.hpp"
#include "../components/skybox.hpp"
#include "../physics/common.hpp"
#include "../render/light_buffer.hpp"
#include "../render/material.hpp"
#include "../render/shadow_maps.hpp"
#include "../util/guid.hpp"
#include "node.hpp"
#include <memory>
#include <string>
#include <vector>

namespace scn {

class Scene {
  public:
    std::shared_ptr<Node> root;
    std::weak_ptr<component::Camera> activeCamera;
    std::weak_ptr<component::Skybox> skybox;
    physics::Context physics_context;
    std::unique_ptr<render::ShadowMaps> shadowMaps =
        std::make_unique<render::ShadowMaps>();
    std::vector<std::weak_ptr<component::Light>> shadowLights;
    // Where each of those maps is: its array and layer there
    struct ShadowSlot {
        render::ShadowMaps::Bank bank;
        size_t layer;
    };
    std::vector<ShadowSlot> shadowSlots;
    // A scene that ends by cutting to another one (the demo's shots) writes
    // that one's SceneManager name here, the manager loads it before the next
    // frame. Shared, so components can hold it and the scene can still move.
    std::shared_ptr<std::string> cutTo = std::make_shared<std::string>();
    // Brightness of the frame for the fade transition pass (main.cpp), 1 is
    // the normal image, 0 black. Shared like cutTo, the scene's animations
    // fade it around the cuts.
    std::shared_ptr<float> fade = std::make_shared<float>(1.f);

    Scene();

    // Call on whole scene graph
    // dt in real seconds, the timescale is applied here
    void update(float dt);
    // Advances physics by physics_context.fixedDt (scaled by timeScale)
    void physics_step();
    void draw();
    // Renders the scene depth from every shadow casting light into shadowMaps
    void shadow_pass();
    const render::ShadowMaps &getShadowMaps() const { return *shadowMaps; }
    // Light that map i belongs to, as of the last shadow_pass
    const std::vector<std::weak_ptr<component::Light>> &
    getShadowLights() const {
        return shadowLights;
    }

    // Depth-first search for the node with the given guid, or nullptr if not
    // found. Used to resolve guid references (activeCamera, parent links,
    // ...) when deserializing a scene.
    std::shared_ptr<Node> findByGuid(util::Guid guid) const;

    // Depth-first (in scene order) collect of every component of type T on
    // enabled nodes
    // (disabled nodes skip their whole subtree). Disabled components are
    // included, check ->enabled if it matters.
    template <typename T>
    std::vector<std::shared_ptr<T>> getAllComponents() const {
        std::vector<std::shared_ptr<T>> result;
        std::vector<std::shared_ptr<Node>> stack{root};
        while (!stack.empty()) {
            auto node = stack.back();
            stack.pop_back();
            if (!node->enabled)
                continue;
            for (auto &c : node->components)
                if (c->type_id() == component::component_type_id<T>())
                    result.push_back(std::static_pointer_cast<T>(c));
            // Reversed so the stack pops children in scene order
            for (auto it = node->children.rbegin(); it != node->children.rend();
                 ++it)
                stack.push_back(*it);
        }
        return result;
    }

  private:
    render::Shader depthShader, depthShaderInstanced;

    render::LightBuffer lightBuffer;
    // Matrix each shadow layer was last rendered with, in the suns' array
    // and in the small one
    std::vector<glm::mat4> shadowMatrices, smallShadowMatrices;
    // Recursively get all pbodies in scene (slow now, no caching)
    void gatherPhysicsBodies(const std::shared_ptr<Node> &node);
    // Collects lights from the scene and uploads them to the UBO
    void uploadLights();
};

} // namespace scn
