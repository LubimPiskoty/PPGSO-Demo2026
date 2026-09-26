#include "scene_loader.hpp"

#include "../ecs/mesh.hpp"
#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <unordered_set>
#include <vector>

#define SERIALIZER_VERSION 2.0

namespace loader {

cJSON *serializeVec3(const glm::vec3 &v) {
    auto json = cJSON_CreateArray();
    cJSON_AddItemToArray(json, cJSON_CreateNumber(v.x));
    cJSON_AddItemToArray(json, cJSON_CreateNumber(v.y));
    cJSON_AddItemToArray(json, cJSON_CreateNumber(v.z));
    return json;
}

// Serialized as euler angles (radians) for a human-readable file; the engine
// itself keeps rotations as quaternions internally.
cJSON *serializeRotation(const glm::quat &q) {
    return serializeVec3(glm::eulerAngles(q));
}

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

static const char *textureSlotNames[(int)render::TextureSlot::Count] = {
    "albedo",
    "normal",
    "specular",
};

cJSON *serializeMaterial(const std::shared_ptr<render::Material> &material) {
    auto json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "name", material->name.c_str());
    cJSON_AddStringToObject(json, "guid", material->guid.toString().c_str());

    auto shader = cJSON_AddObjectToObject(json, "shader");
    if (auto s = material->getShader()) {
        cJSON_AddStringToObject(shader, "vertex", s->vertexPath.c_str());
        cJSON_AddStringToObject(shader, "fragment", s->fragmentPath.c_str());
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

void serializeCameraComponent(cJSON *json,
                              std::shared_ptr<ecs::Camera> camera) {
    cJSON_AddNumberToObject(json, "fov", camera->getFov());
    cJSON_AddNumberToObject(json, "near", camera->getNear());
    cJSON_AddNumberToObject(json, "far", camera->getFar());
}

void serializeMeshComponent(cJSON *json, std::shared_ptr<ecs::Mesh> mesh,
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

cJSON *serializeComponent(std::shared_ptr<ecs::Component> component,
                          MaterialRegistry &registry) {
    cJSON *json = cJSON_CreateObject();

    // cJSON_AddNumberToObject(json, "type_id", component->type_id());
    cJSON_AddStringToObject(json, "type_id", component->type_name());
    cJSON_AddBoolToObject(json, "enabled", component->enabled);
    // TODO: Refactor get_component cast to the component too but forget it now
    if (component->type_id() == ecs::component_type_id<ecs::Camera>()) {
        serializeCameraComponent(
            json, component->node.lock()->get_component<ecs::Camera>());
        // cJSON_AddObjectToObject(cJSON *const object, const char *const name);
    } else if (component->type_id() == ecs::component_type_id<ecs::Mesh>()) {

        serializeMeshComponent(
            json, component->node.lock()->get_component<ecs::Mesh>(), registry);
    } else {
        cJSON_AddTrueToObject(json, "missing_serializer");
    }
    return json;
}

cJSON *serializeNode(std::shared_ptr<scn::Node> node,
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
                          serializeRotation(node->localRot));
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
    // Added before "scene" so readers can resolve material refs in one pass,
    // but filled after the graph walk has collected them
    auto graph = cJSON_AddObjectToObject(json, "scene");
    auto materials = cJSON_AddArrayToObject(json, "materials");
    cJSON_AddStringToObject(
        graph, "activeCamera",
        scene->activeCamera.lock()->node.lock()->guid.toString().c_str());

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
