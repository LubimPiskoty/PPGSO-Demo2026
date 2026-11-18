#pragma once

#include "../components/camera.hpp"
#include "../components/light.hpp"
#include "../components/physicsbody.hpp"
#include "../components/skybox.hpp"
#include "../physics/common.hpp"
#include "../render/light_buffer.hpp"
#include "../render/light_culling.hpp"
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
    // Opaque depth and the thin G-buffer's world-space normals from the
    // camera, before draw (render::PostProcessing::begin binds the buffers):
    // draw then shades every pixel about once, the post passes read both
    void depth_prepass();
    // Forward+: uploads the lights, then each screen tile's list of those
    // that reach it (render::LightCulling), for draw and drawTransparent.
    // After the depth prepass, from its depth resolved (width x height).
    void cullLights(GLuint depthTexture, int width, int height);
    // The opaque meshes (and the sky)
    void draw();
    // Then the transparent ones draw() set aside, farthest first: after
    // render::PostProcessing::resolveDepth, the water reads the opaque depth
    void drawTransparent();
    // draw()'s transparent meshes, for drawTransparent
    std::vector<TransparentDraw> transparent;
    // Renders the scene depth from every shadow casting light into shadowMaps
    void shadow_pass();
    // Render mode (--render): baked maps are drawn every frame too, with the
    // same casters, so the frames don't depend on when a map was baked
    static inline bool redrawBakedShadows = false;
    const render::ShadowMaps &getShadowMaps() const { return *shadowMaps; }
    // Light that map i belongs to, as of the last shadow_pass
    const std::vector<std::weak_ptr<component::Light>> &
    getShadowLights() const {
        return shadowLights;
    }

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
    // Behind pointers: they own GL programs and can't be copied, the scene
    // still moves
    std::unique_ptr<render::Shader> depthShader, depthShaderInstanced;

    render::LightBuffer lightBuffer;
    // Behind a pointer like the shaders; made on the first cullLights, so a
    // scene can be built before there's a GL context
    std::unique_ptr<render::LightCulling> lightCulling;
    // Matrix each shadow layer was last rendered with, in the suns' array
    // and in the small one
    std::vector<glm::mat4> shadowMatrices, smallShadowMatrices;
    // Recursively get all pbodies in scene (slow now, no caching)
    void gatherPhysicsBodies(const std::shared_ptr<Node> &node);
    // Collects lights from the scene and uploads them (LightBuffer)
    void uploadLights();
};

} // namespace scn
