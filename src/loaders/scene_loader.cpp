#include "scene_loader.hpp"

#include "../ecs/mesh.hpp"
#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>

#define SERIALIZER_VERSION 1.0

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

void serializeCameraComponent(cJSON *json,
                              std::shared_ptr<ecs::Camera> camera) {
    cJSON_AddNumberToObject(json, "fov", camera->getFov());
    cJSON_AddNumberToObject(json, "near", camera->getNear());
    cJSON_AddNumberToObject(json, "far", camera->getFar());
}

void serializeMeshComponent(cJSON *json, std::shared_ptr<ecs::Mesh> mesh) {

    cJSON_AddStringToObject(json, "model", mesh->get_model_name());
    cJSON_AddStringToObject(json, "vertex", mesh->get_vertex_name());
    cJSON_AddStringToObject(json, "fragment", mesh->get_fragment_name());
}

cJSON *serializeComponent(std::shared_ptr<ecs::Component> component) {
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
            json, component->node.lock()->get_component<ecs::Mesh>());
    } else {
        cJSON_AddTrueToObject(json, "missing_serializer");
    }
    return json;
}

cJSON *serializeNode(std::shared_ptr<scn::Node> node) {
    // Serialize this node
    // INFO: We skip parent reference because it can be deducted from the
    // structure
    auto json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "name", node->name.c_str());
    cJSON_AddStringToObject(json, "guid", node->guid_string().c_str());
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
        cJSON_AddItemToArray(components, serializeComponent(component));
    // Serialize children
    auto array = cJSON_AddArrayToObject(json, "children");
    for (auto const &child : node->children)
        cJSON_AddItemToArray(array, serializeNode(child));

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
    cJSON_AddStringToObject(
        graph, "activeCamera",
        scene->activeCamera.lock()->node.lock()->guid_string().c_str());
    cJSON_AddItemToObject(graph, "graph", serializeNode(scene->root));

    char *json_str = cJSON_Print(json);
    file.write(json_str, std::strlen(json_str));
    file.close();

    cJSON_free(json_str);
    cJSON_Delete(json);
    puts("[Info] Scene saved successfuly!!");
}

} // namespace loader
