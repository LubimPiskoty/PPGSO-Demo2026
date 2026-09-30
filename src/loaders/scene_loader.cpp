#include "scene_loader.hpp"

#include "../components/UserMovement.hpp"
#include "../components/animator.hpp"
#include "../components/instances.hpp"
#include "../components/mesh.hpp"
#include "curve_loader.hpp"
#include "model_loader.hpp"
#include "texture_loader.hpp"
#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// V3: shaders store whether they're instanced, and UserMovement, Instances
// and Animator components are serialized. V2 files still load, the missing
// fields fall back to defaults.
#define SERIALIZER_VERSION 3.0

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

// Indexed by PlayMode / AnimTarget
static const char *playModeNames[] = {"single", "loop", "pingpong"};
static const char *animTargetNames[] = {"position", "rotation", "scale"};

cJSON *serializeMaterial(const std::shared_ptr<render::Material> &material) {
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

void serializeCameraComponent(cJSON *json,
                              std::shared_ptr<component::Camera> camera) {
    cJSON_AddNumberToObject(json, "fov", camera->getFov());
    cJSON_AddNumberToObject(json, "near", camera->getNear());
    cJSON_AddNumberToObject(json, "far", camera->getFar());
}

void serializeMeshComponent(cJSON *json, std::shared_ptr<component::Mesh> mesh,
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

void serializeUserMovementComponent(
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
void serializeInstancesComponent(
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

void serializeAnimatorComponent(cJSON *json,
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

cJSON *serializeComponent(std::shared_ptr<component::Component> component,
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

//
// Loading
//

// Read helpers: return the fallback when the key is missing or has the wrong
// type, so older files and hand-edited ones still load
static float readNumber(const cJSON *json, const char *key, float fallback) {
    auto item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : fallback;
}

static bool readBool(const cJSON *json, const char *key, bool fallback) {
    auto item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

static std::string readString(const cJSON *json, const char *key,
                              const std::string &fallback = "") {
    auto item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}

static glm::vec3 toVec3(const cJSON *array, glm::vec3 fallback) {
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != 3)
        return fallback;
    glm::vec3 v;
    for (int i = 0; i < 3; i++)
        v[i] = (float)cJSON_GetNumberValue(cJSON_GetArrayItem(array, i));
    return v;
}

static glm::vec3 readVec3(const cJSON *json, const char *key,
                          glm::vec3 fallback) {
    return toVec3(cJSON_GetObjectItemCaseSensitive(json, key), fallback);
}

// Null guid when missing or malformed
static util::Guid readGuid(const cJSON *json, const char *key) {
    try {
        return util::Guid::fromString(readString(json, key));
    } catch (const std::invalid_argument &) {
        return util::Guid();
    }
}

// Index of name in names, or fallback
template <std::size_t N>
static int readEnum(const cJSON *json, const char *key, const char *(&names)[N],
                    int fallback) {
    std::string value = readString(json, key);
    for (std::size_t i = 0; i < N; i++)
        if (value == names[i])
            return (int)i;
    return fallback;
}

// Resources shared while loading one scene, so every model, shader and
// texture is only loaded once
struct LoadContext {
    std::unordered_map<std::string, std::shared_ptr<render::Model>> models;
    std::map<std::tuple<std::string, std::string, bool>,
             std::shared_ptr<render::Shader>>
        shaders;
    std::unordered_map<std::string, std::shared_ptr<render::Texture>> textures;
    std::unordered_map<util::Guid, std::shared_ptr<render::Material>> materials;

    // Instanced meshes get their own copy: the instance buffer is attached
    // to the model, so it can't be shared with other meshes
    std::shared_ptr<render::Model> model(const std::string &name, bool unique) {
        if (unique)
            return std::make_shared<render::Model>(loadModel(name));
        auto &model = models[name];
        if (!model)
            model = std::make_shared<render::Model>(loadModel(name));
        return model;
    }

    std::shared_ptr<render::Shader> shader(const std::string &vertex,
                                           const std::string &fragment,
                                           bool instanced) {
        auto &shader = shaders[{vertex, fragment, instanced}];
        if (!shader)
            shader =
                std::make_shared<render::Shader>(vertex, fragment, instanced);
        return shader;
    }

    std::shared_ptr<render::Texture> texture(const std::string &name) {
        auto &texture = textures[name];
        if (!texture)
            texture = loadTexture(name);
        return texture;
    }
};

static std::shared_ptr<render::Material> deserializeMaterial(const cJSON *json,
                                                             LoadContext &ctx) {
    auto shaderJson = cJSON_GetObjectItemCaseSensitive(json, "shader");
    auto shader = ctx.shader(readString(shaderJson, "vertex", "default.vs"),
                             readString(shaderJson, "fragment", "texture.fs"),
                             readBool(shaderJson, "instanced", false));

    auto material =
        std::make_shared<render::Material>(readString(json, "name"), shader);
    if (auto guid = readGuid(json, "guid"))
        material->guid = guid;

    auto textures = cJSON_GetObjectItemCaseSensitive(json, "textures");
    for (int i = 0; i < (int)render::TextureSlot::Count; i++) {
        std::string path = readString(textures, textureSlotNames[i]);
        if (!path.empty())
            material->setTexture((render::TextureSlot)i, ctx.texture(path));
    }

    auto phong = cJSON_GetObjectItemCaseSensitive(json, "phong");
    material->ambient = readVec3(phong, "ambient", material->ambient);
    material->diffuse = readVec3(phong, "diffuse", material->diffuse);
    material->specular = readVec3(phong, "specular", material->specular);
    material->shininess = readNumber(phong, "shininess", material->shininess);
    material->opacity = readNumber(json, "opacity", material->opacity);
    material->blend = readString(json, "blend") == "transparent"
                          ? render::BlendMode::Transparent
                          : render::BlendMode::Opaque;
    return material;
}

static void deserializeCameraComponent(const cJSON *json,
                                       std::shared_ptr<scn::Node> node) {
    // The size is only known by the window, the caller sets it with setSize()
    // TODO: Camera::fov is saved in radians but the constructor takes degrees
    float fov = readNumber(json, "fov", glm::radians(50.f));
    node->add_component<component::Camera>(glm::degrees(fov), 1.f, 1.f,
                                           readNumber(json, "near", 0.1f),
                                           readNumber(json, "far", 500.f));
}

static void deserializeMeshComponent(const cJSON *json,
                                     std::shared_ptr<scn::Node> node,
                                     bool instanced, LoadContext &ctx) {
    auto guid =
        readGuid(cJSON_GetObjectItemCaseSensitive(json, "material"), "guid");
    auto material = ctx.materials.find(guid);
    if (material == ctx.materials.end()) {
        std::cerr << "[Error] Mesh on '" << node->name
                  << "' references an unknown material, skipped" << std::endl;
        return;
    }
    node->add_component<component::Mesh>(
        ctx.model(readString(json, "model"), instanced), material->second);
}

static void deserializeUserMovementComponent(const cJSON *json,
                                             std::shared_ptr<scn::Node> node) {
    // V2 files have no fields here, so these defaults matter
    auto keys = cJSON_GetObjectItemCaseSensitive(json, "keys");
    node->add_component<component::UserMovement>(
        readNumber(json, "speed", 1.f), readNumber(json, "sensitivity", 0.1f),
        (int)readNumber(keys, "forward", 'W'),
        (int)readNumber(keys, "left", 'A'),
        (int)readNumber(keys, "backward", 'S'),
        (int)readNumber(keys, "right", 'D'), (int)readNumber(keys, "up", ' '),
        (int)readNumber(keys, "down", 'C'));
}

static void deserializeInstancesComponent(const cJSON *json,
                                          std::shared_ptr<scn::Node> node) {
    std::vector<component::Instance> instances;
    const cJSON *item;
    cJSON_ArrayForEach(item,
                       cJSON_GetObjectItemCaseSensitive(json, "instances")) {
        if (cJSON_GetArraySize(item) != 9)
            continue;
        float v[9];
        for (int i = 0; i < 9; i++)
            v[i] = (float)cJSON_GetNumberValue(cJSON_GetArrayItem(item, i));
        instances.push_back(
            {{v[0], v[1], v[2]}, {v[3], v[4], v[5]}, {v[6], v[7], v[8]}});
    }
    node->add_component<component::Instances>(std::move(instances));
}

static void deserializeAnimatorComponent(const cJSON *json,
                                         std::shared_ptr<scn::Node> node) {
    auto animator = node->add_component<component::Animator>();
    const cJSON *t;
    cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(json, "tracks")) {
        component::AnimatorTrack track;
        track.target = (component::AnimTarget)readEnum(
            t, "target", animTargetNames, (int)track.target);
        track.playback.duration =
            readNumber(t, "duration", track.playback.duration);
        track.playback.speed = readNumber(t, "speed", track.playback.speed);
        track.playback.mode = (component::PlayMode)readEnum(
            t, "mode", playModeNames, (int)track.playback.mode);
        track.playback.playing = readBool(t, "playing", true);

        // Either a curve file from res/curves or inline control points
        track.curveFile = readString(t, "curve");
        if (!track.curveFile.empty()) {
            track.curve = loadCurve(track.curveFile);
        } else {
            const cJSON *point;
            cJSON_ArrayForEach(point,
                               cJSON_GetObjectItemCaseSensitive(t, "control"))
                track.curve.control.push_back(toVec3(point, glm::vec3(0.f)));
        }
        animator->tracks.push_back(track);
    }
}

static std::shared_ptr<scn::Node> deserializeNode(const cJSON *json,
                                                  LoadContext &ctx) {
    auto node = scn::Node::create(readString(json, "name", "Node"));
    if (auto guid = readGuid(json, "guid"))
        node->guid = guid;
    node->enabled = readBool(json, "enabled", true);

    auto transform = cJSON_GetObjectItemCaseSensitive(json, "transform");
    node->localPos = readVec3(transform, "position", glm::vec3(0.f));
    node->localRotEuler = readVec3(transform, "rotation", glm::vec3(0.f));
    node->localScale = readVec3(transform, "scale", glm::vec3(1.f));

    auto components = cJSON_GetObjectItemCaseSensitive(json, "components");
    const cJSON *c;

    // The mesh needs to know up front if it will be instanced
    bool instanced = false;
    cJSON_ArrayForEach(c, components) {
        if (readString(c, "type_id") == "Instances")
            instanced = true;
    }

    cJSON_ArrayForEach(c, components) {
        std::string type = readString(c, "type_id");
        std::size_t count = node->components.size();

        if (type == "Camera")
            deserializeCameraComponent(c, node);
        else if (type == "Mesh")
            deserializeMeshComponent(c, node, instanced, ctx);
        else if (type == "UserMovement")
            deserializeUserMovementComponent(c, node);
        else if (type == "Instances")
            deserializeInstancesComponent(c, node);
        else if (type == "Animator")
            deserializeAnimatorComponent(c, node);
        else
            std::cerr << "[Warning] Can't load component '" << type << "' on '"
                      << node->name << "', skipped" << std::endl;

        if (node->components.size() > count)
            node->components.back()->enabled = readBool(c, "enabled", true);
    }

    const cJSON *child;
    cJSON_ArrayForEach(child,
                       cJSON_GetObjectItemCaseSensitive(json, "children"))
        node->add_child(deserializeNode(child, ctx));

    return node;
}

scn::Scene loadScene(std::string path) {
    scn::Scene scene = scn::Scene();

    std::ifstream file(path);
    if (file.fail()) {
        std::cerr << "[Error] Can't open scene " << path << std::endl;
        return scene;
    }
    std::stringstream stream;
    stream << file.rdbuf();
    std::string data = stream.str();

    cJSON *json = cJSON_Parse(data.c_str());
    if (!json) {
        std::cerr << "[Error] Scene " << path
                  << " is not valid JSON, near: " << cJSON_GetErrorPtr()
                  << std::endl;
        return scene;
    }

    float version = readNumber(json, "version", 0.f);
    if (version > SERIALIZER_VERSION)
        std::cerr << "[Warning] Scene version " << version
                  << " is newer than the loader (" << SERIALIZER_VERSION
                  << "), some data may be skipped" << std::endl;

    // Materials first, so meshes can resolve their references
    LoadContext ctx;
    const cJSON *m;
    cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(json, "materials")) {
        auto material = deserializeMaterial(m, ctx);
        ctx.materials[material->guid] = material;
    }

    auto sceneJson = cJSON_GetObjectItemCaseSensitive(json, "scene");
    auto graph = cJSON_GetObjectItemCaseSensitive(sceneJson, "graph");
    if (cJSON_IsObject(graph))
        scene.root = deserializeNode(graph, ctx);

    if (auto cameraNode = scene.findByGuid(readGuid(sceneJson, "activeCamera")))
        scene.activeCamera = cameraNode->get_component<component::Camera>();
    if (scene.activeCamera.expired())
        std::cerr << "[Warning] Scene " << path << " has no active camera"
                  << std::endl;

    cJSON_Delete(json);
    std::cout << "[Info] Loaded scene " << path << " (version " << version
              << ", " << ctx.models.size() << " models, "
              << ctx.materials.size() << " materials)" << std::endl;
    return scene;
}
} // namespace loader
