#pragma once

#include "../components/camera.hpp"
#include "../components/physicsbody.hpp"
#include "../physics/common.hpp"
#include "../util/guid.hpp"
#include "../render/light_buffer.hpp"
#include "../components/skybox.hpp"
#include "node.hpp"
#include <memory>
#include <vector>

namespace scn {

class Scene {
  public:
    std::shared_ptr<Node> root;
    std::weak_ptr<component::Camera> activeCamera;
    std::weak_ptr<component::Skybox> skybox;
    physics::Context physics_context;

    Scene();

    // Call on whole scene graph
    void update(float dt);
    // Advances physics by physics_context.fixedDt (scaled by timeScale)
    void physics_step();
    void draw();

    // Depth-first search for the node with the given guid, or nullptr if not
    // found. Used to resolve guid references (activeCamera, parent links,
    // ...) when deserializing a scene.
    std::shared_ptr<Node> findByGuid(util::Guid guid) const;

  private:
    render::LightBuffer lightBuffer;
    // Recursively get all pbodies in scene (slow now, no caching)
    void gatherPhysicsBodies(std::shared_ptr<Node> node);
    // Collects lights from the scene and uploads them to the UBO
    void uploadLights();
};

} // namespace scn
