// Saving: scene graph -> JSON

#include "../scene_loader.hpp"

#include "../../components/UserMovement.hpp"
#include "../../components/animator.hpp"
#include "../../components/instances.hpp"
#include "../../components/mesh.hpp"
#include "json.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <unordered_set>
#include <vector>

namespace loader {

// Materials are shared between meshes, so they're written once in a top-level
// list and meshes only reference them by name + guid. Filled while walking the
// graph; keeps first-seen order so the output is stable.
struct MaterialRegistry {
    std::vector<std::shared_ptr<render::Material>> materials;
    std::unordered_set<util::Guid> seen;

    void add(const std::shared_ptr<render::Material> &material) {
        if (material && seen.insert(material->guid).second)
            materials.push_back(material);
    }
};

static cJSON *
serializeMaterial(const std::shared_ptr<render::Material> &material) {
    auto json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "name", material->name.c_str());
    cJSON_AddStringToObject(json, "guid", material->guid.toString().c_str());

    auto shader = cJSON_AddObjectToObject(json, "shader");
    if (auto s = material->getShader()) {
        cJSON_AddStringToObject(shader, "vertex", s->vertexPath.c_str());
        cJSON_AddStringToObject(shader, "fragment", s->fragmentPath.c_str());
        cJSON_AddBoolToObject(shader, "instanced", s->isInstanced());
    }

    // Only slots that have a texture are written
    auto textures = cJSON_AddObjectToObject(json, "textures");
    for (int i = 0; i < (int)render::TextureSlot::Count; i++) {
        auto texture = material->getTexture((render::TextureSlot)i);
        if (texture)
            cJSON_AddStringToObject(textures, textureSlotNames[i],
                                    texture->path.c_str());
    }

    auto phong = cJSON_AddObjectToObject(json, "phong");
    cJSON_AddItemToObject(phong, "ambient", serializeVec3(material->ambient));
    cJSON_AddItemToObject(phong, "diffuse", serializeVec3(material->diffuse));
    cJSON_AddItemToObject(phong, "specular", serializeVec3(material->specular));
    cJSON_AddNumberToObject(phong, "shininess", material->shininess);
    cJSON_AddNumberToObject(json, "opacity", material->opacity);

    cJSON_AddStringToObject(json, "blend",
                            material->blend == render::BlendMode::Transparent
                                ? "transparent"
                                : "opaque");
    return json;
}

//
// Components
//

static void serializeCameraComponent(cJSON *json,
                                     std::shared_ptr<component::Camera> camera) {
    cJSON_AddNumberToObject(json, "fov", camera->getFov());
    cJSON_AddNumberToObject(json, "near", camera->getNear());
    cJSON_AddNumberToObject(json, "far", camera->getFar());
}

static void serializeMeshComponent(cJSON *json,
                                   std::shared_ptr<component::Mesh> mesh,
                                   MaterialRegistry &registry) {
    cJSON_AddStringToObject(json, "model", mesh->model->filename.c_str());

    // Only a reference here; the full material lives in the top-level list
    if (mesh->material) {
        auto material = cJSON_AddObjectToObject(json, "material");
        cJSON_AddStringToObject(material, "name", mesh->material->name.c_str());
        cJSON_AddStringToObject(material, "guid",
                                mesh->material->guid.toString().c_str());
        registry.add(mesh->material);
    } else {
        cJSON_AddNullToObject(json, "material");
    }
}

static void serializeUserMovementComponent(
    cJSON *json, std::shared_ptr<component::UserMovement> movement) {
    cJSON_AddNumberToObject(json, "speed", movement->speed);
    cJSON_AddNumberToObject(json, "sensitivity", movement->sensitivity);
    auto keys = cJSON_AddObjectToObject(json, "keys");
    cJSON_AddNumberToObject(keys, "forward", movement->forward_key);
    cJSON_AddNumberToObject(keys, "backward", movement->backward_key);
    cJSON_AddNumberToObject(keys, "left", movement->left_key);
    cJSON_AddNumberToObject(keys, "right", movement->right_key);
    cJSON_AddNumberToObject(keys, "up", movement->up_key);
    cJSON_AddNumberToObject(keys, "down", movement->down_key);
}

// Each instance is a flat [px, py, pz, rx, ry, rz, sx, sy, sz] array to keep
// the file small, there can be thousands of them
static void serializeInstancesComponent(
    cJSON *json, std::shared_ptr<component::Instances> instances) {
    auto array = cJSON_AddArrayToObject(json, "instances");
    for (const auto &instance : instances->getInstances()) {
        float values[9] = {
            instance.position.x, instance.position.y, instance.position.z,
            instance.rotation.x, instance.rotation.y, instance.rotation.z,
            instance.scale.x,    instance.scale.y,    instance.scale.z,
        };
        cJSON_AddItemToArray(array, cJSON_CreateFloatArray(values, 9));
    }
}

static void
serializeAnimatorComponent(cJSON *json,
                           std::shared_ptr<component::Animator> animator) {
    auto tracks = cJSON_AddArrayToObject(json, "tracks");
    for (const auto &track : animator->tracks) {
        auto t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "target",
                                animTargetNames[(int)track.target]);
        cJSON_AddNumberToObject(t, "duration", track.playback.duration);
        cJSON_AddNumberToObject(t, "speed", track.playback.speed);
        cJSON_AddStringToObject(t, "mode",
                                playModeNames[(int)track.playback.mode]);
        cJSON_AddBoolToObject(t, "playing", track.playback.playing);
        if (!track.curveFile.empty()) {
            cJSON_AddStringToObject(t, "curve", track.curveFile.c_str());
        } else {
            auto control = cJSON_AddArrayToObject(t, "control");
            for (const auto &point : track.curve.control)
                cJSON_AddItemToArray(control, serializeVec3(point));
        }
        cJSON_AddItemToArray(tracks, t);
    }
}

static cJSON *
serializeComponent(std::shared_ptr<component::Component> component,
                   MaterialRegistry &registry) {
    cJSON *json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "type_id", component->type_name());
    cJSON_AddBoolToObject(json, "enabled", component->enabled);

    auto id = component->type_id();
    using namespace component;
    if (id == component_type_id<Camera>())
        serializeCameraComponent(json,
                                 std::static_pointer_cast<Camera>(component));
    else if (id == component_type_id<Mesh>())
        serializeMeshComponent(json, std::static_pointer_cast<Mesh>(component),
                               registry);
    else if (id == component_type_id<UserMovement>())
        serializeUserMovementComponent(
            json, std::static_pointer_cast<UserMovement>(component));
    else if (id == component_type_id<Instances>())
        serializeInstancesComponent(
            json, std::static_pointer_cast<Instances>(component));
    else if (id == component_type_id<Animator>())
        serializeAnimatorComponent(
            json, std::static_pointer_cast<Animator>(component));
    else
        cJSON_AddTrueToObject(json, "missing_serializer");
    return json;
}

//
// Graph
//

static cJSON *serializeNode(std::shared_ptr<scn::Node> node,
                            MaterialRegistry &registry) {
    // Serialize this node
    // INFO: We skip parent reference because it can be deducted from the
    // structure
    auto json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "name", node->name.c_str());
    cJSON_AddStringToObject(json, "guid", node->guid.toString().c_str());
    cJSON_AddBoolToObject(json, "enabled", node->enabled);
    // Serialize the local transform as pos/rot/scale vectors rather than the
    // composed matrix, so it round-trips exactly and stays human-editable.
    auto transform = cJSON_AddObjectToObject(json, "transform");
    cJSON_AddItemToObject(transform, "position", serializeVec3(node->localPos));
    cJSON_AddItemToObject(transform, "rotation",
                          serializeVec3(node->localRotEuler));
    cJSON_AddItemToObject(transform, "scale", serializeVec3(node->localScale));
    // Serialize components
    auto components = cJSON_AddArrayToObject(json, "components");
    for (const auto &component : node->components)
        cJSON_AddItemToArray(components,
                             serializeComponent(component, registry));
    // Serialize children
    auto array = cJSON_AddArrayToObject(json, "children");
    for (auto const &child : node->children)
        cJSON_AddItemToArray(array, serializeNode(child, registry));

    return json;
}

void saveScene(const scn::Scene *scene, const std::string path) {
    std::puts("[Info] Starting to save scene");
    std::ofstream file(path);
    if (file.fail()) {
        std::puts("[ERROR] Saving scene has failed!!");
        return;
    }

    // Serialize scene
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "version", SERIALIZER_VERSION);
    auto graph = cJSON_AddObjectToObject(json, "scene");
    // Filled after the graph walk has collected them
    auto materials = cJSON_AddArrayToObject(json, "materials");
    if (auto camera = scene->activeCamera.lock())
        cJSON_AddStringToObject(graph, "activeCamera",
                                camera->node.lock()->guid.toString().c_str());

    MaterialRegistry registry;
    cJSON_AddItemToObject(graph, "graph", serializeNode(scene->root, registry));
    for (const auto &material : registry.materials)
        cJSON_AddItemToArray(materials, serializeMaterial(material));

    char *json_str = cJSON_Print(json);
    file.write(json_str, std::strlen(json_str));
    file.close();

    cJSON_free(json_str);
    cJSON_Delete(json);
    puts("[Info] Scene saved successfuly!!");
}

} // namespace loader
