#pragma once

#include "../scene/node.hpp"
#include "../util/guid.hpp"
#include "ecs.hpp"
#include "light.hpp"
#include <algorithm>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace component {

// Put on the camera node. Enables the `inside` node while the camera is in the
// box and the `outside` node while it is not, so a scene can drop the lights,
// meshes and instances that can't be seen from the other side. Keep whatever
// both sides need (walls, ground) outside both nodes.
//
// With blend > 0 the switch is gradual: from `blend` metres outside the box
// walls to `blendInside` metres inside them both nodes stay enabled and the lights under them fade
// (the inside ones in, the outside ones out, or the reverse). Meshes and
// instances can't fade, they switch off once their side is fully faded out.
class ZoneSwitch : public ComponentBase<ZoneSwitch> {
  public:
    glm::vec3 boxMin{-1.f}, boxMax{1.f};
    util::Guid insideGuid, outsideGuid;
    float blend = 0.f;       // fade distance outside the walls
    float blendInside = 0.f; // and inside them, keep it short: the outside
                             // side stays switched on until it is over

    const char *type_name() const override { return "ZoneSwitch"; }

    void update(float) override {
        auto owner = node.lock();
        if (!owner)
            return;
        if (!resolved)
            resolve(owner);

        // 0 = fully outside, 1 = fully inside
        const float in = 1.f - outsideAmount(owner->globalPosition());
        setSide(insideNode, insideLights, in);
        setSide(outsideNode, outsideLights, 1.f - in);
    }

    bool isInside() const { return inside; }

  private:
    struct LightBase {
        std::shared_ptr<Light> light;
        float intensity, ambient;
    };

    bool resolved = false;
    bool inside = false;
    std::weak_ptr<scn::Node> insideNode, outsideNode;
    std::vector<LightBase> insideLights, outsideLights;

    // 1 well outside the box, 0 well inside, smooth across the walls
    float outsideAmount(const glm::vec3 &p) {
        const glm::vec3 half = (boxMax - boxMin) * 0.5f;
        const glm::vec3 q = glm::abs(p - (boxMin + boxMax) * 0.5f) - half;
        // Signed distance to the box, negative inside
        const float d = glm::length(glm::max(q, 0.f)) +
                        glm::min(glm::max(q.x, glm::max(q.y, q.z)), 0.f);
        inside = d <= 0.f;
        if (blend <= 0.f)
            return inside ? 0.f : 1.f;
        const float t =
            glm::clamp((d + blendInside) / (blend + blendInside), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    // weight 0 switches the side off, anything above fades its lights
    static void setSide(const std::weak_ptr<scn::Node> &side,
                        const std::vector<LightBase> &lights, float weight) {
        if (auto n = side.lock())
            n->enabled = weight > 0.f;
        for (const auto &l : lights) {
            l.light->intensity = l.intensity * weight;
            l.light->ambient = l.ambient * weight;
        }
    }

    static void collectLights(const std::shared_ptr<scn::Node> &n,
                              std::vector<LightBase> &out) {
        if (auto light = n->get_component<Light>())
            out.push_back({light, light->intensity, light->ambient});
        for (auto &c : n->children)
            collectLights(c, out);
    }

    // Depth-first from the scene root, both targets are somewhere under it
    void resolve(const std::shared_ptr<scn::Node> &owner) {
        resolved = true;
        auto root = owner;
        while (auto parent = root->parent.lock())
            root = parent;
        std::vector<std::shared_ptr<scn::Node>> stack{root};
        while (!stack.empty()) {
            auto n = stack.back();
            stack.pop_back();
            if (n->guid == insideGuid) {
                insideNode = n;
                collectLights(n, insideLights);
            } else if (n->guid == outsideGuid) {
                outsideNode = n;
                collectLights(n, outsideLights);
            }
            for (auto &c : n->children)
                stack.push_back(c);
        }
    }
};

} // namespace component
