#pragma once

#include "../scene/node.hpp"
#include "ecs.hpp"
#include "mesh.hpp"
#include <cstdio>

namespace component {

// Marks a node as a shadow caster. Only nodes with this component are drawn in
// the shadow pass (Scene::shadow_pass); it doesn't draw anything in the normal
// pass, the Mesh does that.
class Shadow : public component::ComponentBase<Shadow> {
  public:
    // Called by Scene::shadow_pass with the light's matrices and depth shaders
    void drawShadow(glm::mat4 projection, glm::mat4 lightView,
                    const render::Shader &depthShader,
                    const render::Shader &depthShaderInstanced) {
        const auto n = node.lock();
        if (!n)
            return;
        const auto mesh = n->get_component<Mesh>();
        if (mesh == nullptr) {
            printf("[Warning] Shadow component missing mesh component!! %s\n",
                   n->guid.toString().c_str());
            return;
        }

        mesh->drawDepth(projection, lightView,
                        mesh->model->isInstanced() ? depthShaderInstanced
                                                   : depthShader);
    }

    const char *type_name() const override { return "Shadow"; }

  private:
};

} // namespace component
