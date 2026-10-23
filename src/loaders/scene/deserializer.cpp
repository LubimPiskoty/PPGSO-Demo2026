// Loading: JSON -> scene graph

#include "../scene_loader.hpp"

#include "../../components/UserMovement.hpp"
#include "../../components/animator.hpp"
#include "../../components/instance_generators.hpp"
#include "../../components/instances.hpp"
#include "../../components/light.hpp"
#include "../../components/mesh.hpp"
#include "../../components/physicsbody.hpp"
#include "../../components/shadow.hpp"
#include "../../components/skybox.hpp"
#include "../../components/spin.hpp"
#include "../curve_loader.hpp"
#include "../model_loader.hpp"
#include "../texture_loader.hpp"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace loader {

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
    // "<box>" (side 1) and "<sphere>" (radius 1) are the procedural models,
    // anything else is a file
    static render::Model load(const std::string &name) {
        if (name == "<box>")
            return createBox(glm::vec3(0.5f));
        if (name == "<sphere>")
            return createSphere(1.f, 1.f, 32, 16);
        return loadModel(name);
    }

    std::shared_ptr<render::Model> model(const std::string &name, bool unique) {
        if (unique)
            return std::make_shared<render::Model>(load(name));
        auto &model = models[name];
        if (!model)
            model = std::make_shared<render::Model>(load(name));
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

//
// Components
//

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

// Steps with an unknown "fn" are skipped
static bool deserializeInstanceStep(const cJSON *json,
                                    component::InstanceStep &step) {
    using namespace component;
    std::string fn = readString(json, "fn");
    if (fn == "generateCircle") {
        GenerateCircle s;
        s.center = readVec3(json, "center", s.center);
        s.radius = readNumber(json, "radius", s.radius);
        s.up = readVec3(json, "up", s.up);
        s.count = (int)readNumber(json, "count", (float)s.count);
        step = s;
    } else if (fn == "generateDisc") {
        GenerateDisc s;
        s.center = readVec3(json, "center", s.center);
        s.innerRadius = readNumber(json, "innerRadius", s.innerRadius);
        s.outerRadius = readNumber(json, "outerRadius", s.outerRadius);
        s.up = readVec3(json, "up", s.up);
        s.count = (int)readNumber(json, "count", (float)s.count);
        step = s;
    } else if (fn == "generateGrid") {
        GenerateGrid s;
        s.center = readVec3(json, "center", s.center);
        s.cellSize = readNumber(json, "cellSize", s.cellSize);
        s.cellCount = glm::ivec2(
            readVec2(json, "cellCount", glm::vec2(s.cellCount)));
        s.up = readVec3(json, "up", s.up);
        s.count = (int)readNumber(json, "count", (float)s.count);
        step = s;
    } else if (fn == "generatePlane") {
        GeneratePlane s;
        s.center = readVec3(json, "center", s.center);
        s.size = readVec2(json, "size", s.size);
        s.up = readVec3(json, "up", s.up);
        s.count = (int)readNumber(json, "count", (float)s.count);
        step = s;
    } else if (fn == "randomizeScale") {
        RandomizeScale s;
        s.min = readNumber(json, "min", s.min);
        s.max = readNumber(json, "max", s.max);
        step = s;
    } else if (fn == "randomizePosition") {
        RandomizePosition s;
        s.min = readVec3(json, "min", s.min);
        s.max = readVec3(json, "max", s.max);
        step = s;
    } else if (fn == "randomizeRotation") {
        RandomizeRotation s;
        s.min = readVec3(json, "min", s.min);
        s.max = readVec3(json, "max", s.max);
        step = s;
    } else {
        return false;
    }
    return true;
}

static void deserializeInstancesComponent(const cJSON *json,
                                          std::shared_ptr<scn::Node> node) {
    // A recipe takes priority over saved transforms
    auto generate = cJSON_GetObjectItemCaseSensitive(json, "generate");
    if (cJSON_IsArray(generate)) {
        component::InstanceRecipe recipe;
        // Not readNumber, a float can't hold every 32-bit seed
        auto seed = cJSON_GetObjectItemCaseSensitive(json, "seed");
        if (cJSON_IsNumber(seed))
            recipe.seed = (std::uint32_t)seed->valuedouble;
        const cJSON *s;
        cJSON_ArrayForEach(s, generate) {
            component::InstanceStep step;
            if (deserializeInstanceStep(s, step))
                recipe.steps.push_back(step);
            else
                std::cerr << "[Warning] Unknown instance generator '"
                          << readString(s, "fn") << "' on '" << node->name
                          << "', skipped" << std::endl;
        }
        node->add_component<component::Instances>(
            component::instgen::runRecipe(recipe));
        return;
    }

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
        std::string curveFile = readString(t, "curve");
        if (!curveFile.empty()) {
            track.curve = loadCurve(curveFile);
        } else {
            const cJSON *point;
            cJSON_ArrayForEach(point,
                               cJSON_GetObjectItemCaseSensitive(t, "control"))
                track.curve.control.push_back(toVec3(point, glm::vec3(0.f)));
        }
        animator->tracks.push_back(track);
    }
}

static void deserializeLightComponent(const cJSON *json,
                                      std::shared_ptr<scn::Node> node) {
    using component::Light;
    using component::LightType;
    auto type = (LightType)readEnum(json, "light", lightTypeNames,
                                    (int)LightType::Directional);
    // Lights only come from the factories, the fields are overwritten below
    Light light = type == LightType::Point  ? Light::point()
                  : type == LightType::Spot ? Light::spot()
                                            : Light::directional();
    light.color = readVec3(json, "color", light.color);
    light.intensity = readNumber(json, "intensity", light.intensity);
    light.ambient = readNumber(json, "ambient", light.ambient);
    light.constant = readNumber(json, "constant", light.constant);
    light.linear = readNumber(json, "linear", light.linear);
    light.quadratic = readNumber(json, "quadratic", light.quadratic);
    light.innerCone = readNumber(json, "innerCone", light.innerCone);
    light.outerCone = readNumber(json, "outerCone", light.outerCone);
    light.castsShadow = readBool(json, "castsShadow", light.castsShadow);
    light.shadowExtent = readNumber(json, "shadowExtent", light.shadowExtent);
    light.shadowFocusAhead =
        readNumber(json, "shadowFocusAhead", light.shadowFocusAhead);
    light.shadowNear = readNumber(json, "shadowNear", light.shadowNear);
    light.shadowFar = readNumber(json, "shadowFar", light.shadowFar);
    node->add_component<Light>(light);
}

static void deserializeSkyboxComponent(const cJSON *json,
                                       std::shared_ptr<scn::Node> node,
                                       scn::Scene &scene) {
    scene.skybox = node->add_component<component::Skybox>(
        readString(json, "texture", "skybox.png"));
}

static void deserializeSpinComponent(const cJSON *json,
                                     std::shared_ptr<scn::Node> node) {
    glm::vec3 axis = readVec3(json, "axis", glm::vec3(0.f, 1.f, 0.f));
    if (glm::dot(axis, axis) < 1e-8f)
        axis = glm::vec3(0.f, 1.f, 0.f);
    node->add_component<component::Spin>(readNumber(json, "speed", 10.f), axis);
}

// Mass 0 (or missing) makes the body static. Inertia is computed from the
// collider, so it isn't stored
static void deserializePhysicsBodyComponent(const cJSON *json,
                                            std::shared_ptr<scn::Node> node) {
    auto pb = node->add_component<component::PhysicsBody>();
    auto &body = pb->body;

    auto collider = cJSON_GetObjectItemCaseSensitive(json, "collider");
    if (readString(collider, "shape") == "plane") {
        physics::PlaneDesc plane;
        plane.halfSize = readVec2(collider, "halfSize", plane.halfSize);
        pb->collider = plane;
    } else {
        physics::SphereDesc sphere;
        sphere.radius = readNumber(collider, "radius", sphere.radius);
        pb->collider = sphere;
    }
    pb->collider.offset = readVec3(collider, "offset", glm::vec3(0.f));

    body.restitution = readNumber(json, "restitution", body.restitution);
    body.friction = readNumber(json, "friction", body.friction);
    body.velocity = readVec3(json, "velocity", glm::vec3(0.f));
    body.angularVelocity = readVec3(json, "angularVelocity", glm::vec3(0.f));

    // Planes can only be static
    float mass = readNumber(json, "mass", 0.f);
    if (mass <= 0.f || pb->collider.type == physics::Shape::Type::Plane) {
        body.mass = body.invMass = 0.f;
        body.invInertiaLocal = glm::mat3(0.f);
    } else {
        // Solid sphere: I = 2/5 m r^2
        float r = pb->collider.sphere.radius;
        body.mass = mass;
        body.invMass = 1.f / mass;
        body.invInertiaLocal = glm::mat3(1.f / (0.4f * mass * r * r));
    }
    body.invInertiaWorld = body.invInertiaLocal;
}

static void deserializePhysicsContext(const cJSON *json,
                                      physics::Context &ctx) {
    ctx.gravity = readVec3(json, "gravity", ctx.gravity);
    ctx.windVel = readVec3(json, "wind", ctx.windVel);
    ctx.drag = readNumber(json, "drag", ctx.drag);
    ctx.angularDrag = readNumber(json, "angularDrag", ctx.angularDrag);
    ctx.timeScale = readNumber(json, "timeScale", ctx.timeScale);
    // 0 or less would never advance and hang the main loop
    float fixedDt = readNumber(json, "fixedDt", ctx.fixedDt);
    if (fixedDt > 0.f)
        ctx.fixedDt = fixedDt;
}

//
// Graph
//

static std::shared_ptr<scn::Node> deserializeNode(const cJSON *json,
                                                  LoadContext &ctx,
                                                  scn::Scene &scene) {
    auto node = scn::Node::create(readString(json, "name", "Node"));
    if (auto guid = readGuid(json, "guid"))
        node->guid = guid;
    node->enabled = readBool(json, "enabled", true);

    auto transform = cJSON_GetObjectItemCaseSensitive(json, "transform");
    node->setLocalPos(readVec3(transform, "position", glm::vec3(0.f)));
    node->setLocalRotEuler(readVec3(transform, "rotation", glm::vec3(0.f)));
    node->setLocalScale(readVec3(transform, "scale", glm::vec3(1.f)));

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
        else if (type == "PhysicsBody")
            deserializePhysicsBodyComponent(c, node);
        else if (type == "Light")
            deserializeLightComponent(c, node);
        else if (type == "Shadow")
            node->add_component<component::Shadow>();
        else if (type == "Skybox")
            deserializeSkyboxComponent(c, node, scene);
        else if (type == "Spin")
            deserializeSpinComponent(c, node);
        else
            std::cerr << "[Warning] Can't load component '" << type << "' on '"
                      << node->name << "', skipped" << std::endl;

        if (node->components.size() > count)
            node->components.back()->enabled = readBool(c, "enabled", true);
    }

    const cJSON *child;
    cJSON_ArrayForEach(child,
                       cJSON_GetObjectItemCaseSensitive(json, "children"))
        node->add_child(deserializeNode(child, ctx, scene));

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
    if (version > SCENE_FORMAT_VERSION)
        std::cerr << "[Warning] Scene version " << version
                  << " is newer than the loader (" << SCENE_FORMAT_VERSION
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
        scene.root = deserializeNode(graph, ctx, scene);
    deserializePhysicsContext(
        cJSON_GetObjectItemCaseSensitive(sceneJson, "physics"),
        scene.physics_context);

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
