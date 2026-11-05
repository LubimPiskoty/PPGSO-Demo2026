#include "ironheart_forge.hpp"

#include "../components/UserMovement.hpp"
#include "../components/animator.hpp"
#include "../components/camera.hpp"
#include "../components/instances.hpp"
#include "../components/light.hpp"
#include "../components/mesh.hpp"
#include "../components/particle.hpp"
#include "../components/physicsbody.hpp"
#include "../components/shadow.hpp"
#include "../components/skybox.hpp"
#include "../loaders/model_loader.hpp"
#include "../render/compute_shader.hpp"
#include "../render/material.hpp"

#include <algorithm>
#include <cJSON.h>
#include <cmath>
#include <fstream>
#include <glm/gtc/constants.hpp>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace scenes {

namespace {

using component::Instance;

// ---------------------------------------------------------------------------
// Layout file
// ---------------------------------------------------------------------------

float num(const cJSON *item, int index) {
    return (float)cJSON_GetNumberValue(cJSON_GetArrayItem(item, index));
}

glm::vec3 vec3At(const cJSON *item) {
    return {num(item, 0), num(item, 1), num(item, 2)};
}

glm::vec3 vec3Of(const cJSON *obj, const char *key) {
    return vec3At(cJSON_GetObjectItemCaseSensitive(obj, key));
}

const cJSON *get(const cJSON *obj, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(obj, key);
}

std::string str(const cJSON *obj, const char *key) {
    const cJSON *item = get(obj, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

// [px, py, pz, rx, ry, rz, sx, sy, sz]
Instance instanceAt(const cJSON *t) {
    return {{num(t, 0), num(t, 1), num(t, 2)},
            {num(t, 3), num(t, 4), num(t, 5)},
            {num(t, 6), num(t, 7), num(t, 8)}};
}

std::vector<Instance> instanceList(const cJSON *list) {
    std::vector<Instance> out;
    const cJSON *t;
    cJSON_ArrayForEach(t, list) out.push_back(instanceAt(t));
    return out;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Euler (pitch, yaw, 0) that points a node's -Z along d
glm::vec3 lookEuler(glm::vec3 d) {
    d = glm::normalize(d);
    return {std::asin(d.y), std::atan2(-d.x, -d.z), 0.f};
}

void makeStatic(component::PhysicsBody &pb) {
    pb.body.mass = pb.body.invMass = 0.f;
    pb.body.invInertiaLocal = pb.body.invInertiaWorld = glm::mat3(0.f);
}

struct Builder {
    scn::Scene &scene;
    std::shared_ptr<render::Material> lit, litInstanced;
    std::map<std::string, std::shared_ptr<render::Model>> shared;

    explicit Builder(scn::Scene &scene) : scene(scene) {
        lit = std::make_shared<render::Material>(
            "Lit", std::make_shared<render::Shader>("default.vs", "lit.fs"));
        litInstanced = std::make_shared<render::Material>(
            "Lit instanced", std::make_shared<render::Shader>(
                                 "default_instanced.vs", "lit.fs", true));
    }

    // Instanced meshes need their own model, the instance buffer lives on it
    std::shared_ptr<render::Model> model(const std::string &asset) {
        auto &m = shared[asset];
        if (!m)
            m = std::make_shared<render::Model>(
                loader::loadModel(asset + ".glb"));
        return m;
    }

    std::shared_ptr<render::Model> uniqueModel(const std::string &asset) {
        return std::make_shared<render::Model>(
            loader::loadModel(asset + ".glb"));
    }

    // A node with a plain mesh
    std::shared_ptr<scn::Node> prop(const std::string &name,
                                    const std::string &asset, glm::vec3 pos,
                                    glm::vec3 rot, glm::vec3 scale,
                                    bool castsShadow) {
        auto node = scn::Node::create(name, pos);
        node->setLocalRotEuler(rot);
        node->setLocalScale(scale);
        node->add_component<component::Mesh>(model(asset), lit);
        if (castsShadow)
            node->add_component<component::Shadow>();
        return node;
    }

    // A node drawing the transforms in one instanced draw call
    std::shared_ptr<scn::Node> instanced(const std::string &name,
                                         const std::string &asset,
                                         std::vector<Instance> transforms,
                                         bool castsShadow) {
        auto node = scn::Node::create(name);
        node->add_component<component::Mesh>(uniqueModel(asset), litInstanced);
        node->add_component<component::Instances>(std::move(transforms));
        if (castsShadow)
            node->add_component<component::Shadow>();
        return node;
    }
};

} // namespace

scn::Scene ironheartForge() {
    scn::Scene scene;

    std::ifstream file("ironheart_layout.json");
    std::stringstream buffer;
    buffer << file.rdbuf();
    cJSON *layout = cJSON_Parse(buffer.str().c_str());
    if (!layout) {
        std::cerr << "[Error] Can't read ironheart_layout.json" << std::endl;
        return scene;
    }

    Builder b(scene);

    // Physics: 120 Hz, wind that pushes in bellows-like pulses and sucks back
    // a little while the bellows refill, so the coals shuffle instead of
    // drifting to one wall
    auto &phys = scene.physics_context;
    phys.fixedDt = 1.f / 120.f;
    phys.angularDrag = 12.f;
    phys.windVel = phys.windPeak = {2.5f, 0.25f, 0.4f};
    phys.windPulsePeriod = 3.f;
    phys.windReturn = 0.5f;

    // ---- Placed objects ----
    // The shell is seen from inside and outside, so it stays at the top level.
    // Everything else in the layout is inside the forge.
    const std::set<std::string> shell = {
        "SM_Forge_Building",  "SM_Forge_Roof", "SM_Forge_Hood",
        "SM_Window_Shutters", "SM_Door_L",     "SM_Door_R",
        "SM_Terrain",         "SM_River_Water"};
    // Water and the huge terrain would be drawn into every shadow layer
    const std::set<std::string> noShadow = {"SM_River_Water", "SM_QuenchWater",
                                            "SM_Terrain"};
    auto forge = scn::Node::create("Forge");
    auto inside = scn::Node::create("Inside");
    const cJSON *o;
    cJSON_ArrayForEach(o, get(layout, "objects")) {
        const std::string asset = str(o, "asset");
        auto node = b.prop(str(o, "name"), asset, vec3Of(o, "position"),
                           vec3Of(o, "rotation"), vec3Of(o, "scale"),
                           !noShadow.count(asset));
        (shell.count(asset) ? forge : inside)->add_child(node);
    }

    // ---- Coal: one physics sphere per piece in the fire pot ----
    const cJSON *assets = get(layout, "assets");
    const cJSON *bed = get(get(assets, "SM_Forge_Hearth"), "coal_bed");
    const float potFloor = (float)cJSON_GetNumberValue(get(bed, "z")) - 0.005f;
    const float hx = num(get(bed, "size"), 0) / 2,
                hz = num(get(bed, "size"), 1) / 2;
    glm::vec3 hearth(0.f);
    cJSON_ArrayForEach(o, get(layout, "objects")) {
        if (str(o, "asset") == "SM_Forge_Hearth")
            hearth = vec3Of(o, "position");
    }
    const float cx = hearth.x, cz = hearth.z;

    auto coal = scn::Node::create("Coal");
    // Planes face their local +Y, the walls are turned to face the pot centre
    auto addWall = [&](const char *name, glm::vec3 pos, glm::vec3 rot) {
        auto wall = scn::Node::create(name, pos);
        wall->setLocalRotEuler(rot);
        auto pb = wall->add_component<component::PhysicsBody>();
        pb->collider = physics::PlaneDesc{};
        makeStatic(*pb);
        pb->body.friction = 1.f;
        pb->body.restitution = 0.1f;
        coal->add_child(wall);
    };
    const float halfPi = glm::half_pi<float>();
    addWall("Pot floor", {cx, potFloor, cz}, {0, 0, 0});
    addWall("Pot wall -X", {cx - hx, 0, cz}, {0, 0, -halfPi});
    addWall("Pot wall +X", {cx + hx, 0, cz}, {0, 0, halfPi});
    addWall("Pot wall -Z", {cx, 0, cz - hz}, {halfPi, 0, 0});
    addWall("Pot wall +Z", {cx, 0, cz + hz}, {-halfPi, 0, 0});

    // The pack's coal bed, twice over: the second set is the same pieces
    // turned and scaled a little differently. They start on a grid above the
    // floor, the pack's own positions overlap and the solver would throw
    // them out of the pot.
    struct Piece {
        std::string asset;
        float radius, scale;
        glm::vec3 rot;
    };
    std::vector<Piece> pieces;
    std::mt19937 rng(7);
    auto uniform = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    constexpr int coalCopies = 2;
    // The pack's radius is 85 % of the mesh, a bit bigger keeps pieces from
    // sinking into each other
    constexpr float colliderScale = 1.25f;
    float maxDiameter = 0.f;
    for (int copy = 0; copy < coalCopies; copy++) {
        const cJSON *group = get(get(layout, "instances"), "coal_bed");
        const cJSON *list;
        cJSON_ArrayForEach(list, group) {
            const std::string asset = list->string;
            const float radius = (float)cJSON_GetNumberValue(
                get(get(get(assets, asset.c_str()), "collider"), "radius"));
            const cJSON *t;
            cJSON_ArrayForEach(t, list) {
                Piece p{asset,
                        radius,
                        num(t, 6),
                        {num(t, 3), num(t, 4), num(t, 5)}};
                if (copy) {
                    p.scale *= uniform(0.85f, 1.1f);
                    p.rot = {uniform(0, 6.28f), uniform(0, 6.28f),
                             uniform(0, 6.28f)};
                }
                maxDiameter =
                    std::max(maxDiameter, 2 * radius * p.scale * colliderScale);
                pieces.push_back(p);
            }
        }
    }
    const float spacing = maxDiameter * 1.1f;
    const int cols = (int)((2 * hx - spacing) / spacing) + 1;
    const int rows = (int)((2 * hz - spacing) / spacing) + 1;

    // One instanced draw per coal type: the node draws, its children are the
    // physics bodies (no mesh) that the instances follow
    std::map<std::string, std::shared_ptr<scn::Node>> coalTypes;
    for (size_t i = 0; i < pieces.size(); i++) {
        const Piece &p = pieces[i];
        auto &type = coalTypes[p.asset];
        if (!type) {
            type = scn::Node::create(p.asset);
            type->add_component<component::Mesh>(b.uniqueModel(p.asset),
                                                 b.litInstanced);
            type->add_component<component::Instances>()->followChildren = true;
            coal->add_child(type);
        }
        const int layer = (int)i / (cols * rows), k = (int)i % (cols * rows);
        const glm::vec3 pos(cx - hx + spacing * (k % cols + 0.5f) -
                                (cols * spacing - 2 * hx) / 2,
                            potFloor + spacing * (layer + 0.6f),
                            cz - hz + spacing * (k / cols + 0.5f) -
                                (spacing * rows - 2 * hz) / 2);

        auto piece = scn::Node::create(p.asset + " " + std::to_string(i), pos);
        piece->setLocalRotEuler(p.rot);
        piece->setLocalScale(glm::vec3(p.scale));
        auto pb = piece->add_component<component::PhysicsBody>();
        const float r = p.radius * p.scale * colliderScale;
        const float mass = 0.03f * p.scale * p.scale * p.scale;
        pb->collider = physics::SphereDesc{r};
        pb->body.mass = mass;
        pb->body.invMass = 1.f / mass;
        pb->body.restitution = 0.1f;
        pb->body.friction = 1.5f;
        pb->body.invInertiaLocal = glm::mat3(1.f / (0.4f * mass * r * r));
        pb->body.invInertiaWorld = pb->body.invInertiaLocal;
        type->add_child(piece);
    }
    inside->add_child(coal);

    // ---- Sparks rising off the coal bed ----
    {
        glm::vec3 pos(0.f);
        const cJSON *m;
        cJSON_ArrayForEach(m, get(layout, "markers")) {
            if (str(m, "name") == "FX_CoalBed")
                pos = vec3Of(m, "position");
        }
        auto node = scn::Node::create("Coal sparks", pos);
        auto particleMaterial = std::make_shared<render::Material>(
            "Particles",
            std::make_shared<render::Shader>("particle.vs", "particle.fs"));
        auto p = node->add_component<component::ParticleSystem>(
            particleMaterial,
            std::make_shared<render::ComputeShader>("spawnParticle.comp"),
            std::make_shared<render::ComputeShader>("updateParticle.comp"));
        p->minColor = {1.0f, 0.8f, 0.3f, 1.0f};
        p->maxColor = {0.9f, 0.2f, 0.0f, 0.0f};
        p->minPosition = {-hx, 0, -hz};
        p->maxPosition = {hx, 0.05f, hz};
        p->minVelocity = {-0.15f, 0.3f, -0.15f};
        p->maxVelocity = {0.25f, 1.0f, 0.15f};
        p->minAccel = {0, -0.25f, 0};
        p->maxAccel = {0.1f, 0.1f, 0};
        p->minLife = 0.6f;
        p->maxLife = 2.2f;
        p->size = 0.012f;
        p->stretch = 0.03f;
        p->spawnInterval = 0.01f;
        p->maxParticles = 4096;
        inside->add_child(node);
    }

    // ---- Outside: the sun, the houses and the trees ----
    auto outside = scn::Node::create("Outside");

    // The sun first, so it gets the first shadow layer. Everything it shines
    // on is static, so its maps are only rendered once (baked).
    {
        auto node = scn::Node::create("Sun");
        // Low from the front left of the camera flight (which heads to -z),
        // so the shadows fall towards the camera instead of hiding behind
        // what casts them
        const glm::vec3 sunRot = lookEuler({0.42f, -0.7f, 0.91f});
        node->setLocalRotEuler(sunRot);
        auto sun =
            component::Light::directional({1.0f, 0.82f, 0.6f}, 1.5f, 0.3f);
        sun.castsShadow = true;
        sun.bakedShadow = true;
        sun.shadowExtent = 300.f;
        sun.shadowNear = 200.f;
        sun.shadowFar = 800.f;
        node->add_component<component::Light>(sun);
        outside->add_child(node);

        // Close-up shadow over the village: a shadow-only copy of the sun
        // (same rotation, so the shadows line up) centered on the forge (at
        // the origin) and every house. Half the diagonal covers them from
        // any sun direction, + 10 for the house size.
        glm::vec2 lo(0.f), hi(0.f);
        const cJSON *list;
        cJSON_ArrayForEach(list, get(get(layout, "instances"), "houses")) {
            for (const Instance &h : instanceList(list)) {
                lo = glm::min(lo, glm::vec2(h.position.x, h.position.z));
                hi = glm::max(hi, glm::vec2(h.position.x, h.position.z));
            }
        }
        const glm::vec2 mid = (lo + hi) * 0.5f;
        auto closeNode = scn::Node::create("Sun close-up shadow",
                                           glm::vec3(mid.x, 0.f, mid.y));
        closeNode->setLocalRotEuler(sunRot);
        auto closeUp = sun;
        closeUp.shadowOnly = true;
        closeUp.shadowExtent = glm::length(hi - lo) * 0.25f + 10.f;
        // Box only 100 m deep around the village instead of the sun's 600:
        // the shader's bias is in depth units, so this keeps it at 5-50 cm
        closeUp.shadowNear = 0.f;
        closeUp.shadowFar = 100.f;
        closeNode->add_component<component::Light>(closeUp);
        outside->add_child(closeNode);
    }
    {
        auto trees = scn::Node::create("Trees");
        const cJSON *tree;
        constexpr int farTreeCopies = 2;
        cJSON_ArrayForEach(tree, get(layout, "trees")) {
            const std::string type = tree->string;
            auto group = scn::Node::create(type);
            // Every tree of this type. The layout's far list is copied five
            // times over: each one gets copies a few metres away, turned and
            // scaled a little differently, so the forest keeps its shape (and
            // stays off the river).
            std::vector<Instance> all = instanceList(get(tree, "near"));
            std::mt19937 treeRng(11);
            auto jitter = [&](float lo, float hi) {
                return std::uniform_real_distribution<float>(lo, hi)(treeRng);
            };
            for (const Instance &original : instanceList(get(tree, "far"))) {
                all.push_back(original);
                for (int copy = 0; copy < farTreeCopies - 1; copy++) {
                    Instance t = original;
                    const float angle = jitter(0.f, glm::two_pi<float>());
                    const float dist = jitter(2.f, 6.f);
                    t.position.x += std::cos(angle) * dist;
                    t.position.z += std::sin(angle) * dist;
                    t.rotation.y = jitter(0.f, glm::two_pi<float>());
                    t.scale *= jitter(0.85f, 1.15f);
                    all.push_back(t);
                }
            }
#ifdef NDEBUG
            // Release: the full bark + leaves model
            group->add_child(
                b.instanced(type + " Bark", str(tree, "bark"), all, true));
            group->add_child(b.instanced(type + " Leaves", str(tree, "leaves"),
                                         std::move(all), true));
#else
            // Debug: the cheap LOD1 mesh
            group->add_child(b.instanced(type + " LOD1", str(tree, "lod1"),
                                         std::move(all), true));
#endif
            trees->add_child(group);
        }
        outside->add_child(trees);

        // Same 80 s clock as the camera: the trees switch off at 27 s
        auto &track = trees->add_component<component::Animator>()->addTrack(
            component::AnimTarget::Property,
            component::Curve<glm::vec3>::flat({{0.f, glm::vec3(1.f)},
                                               {27.f, glm::vec3(1.f)},
                                               {27.f, glm::vec3(0.f)}}),
            component::PlayMode::Single);
        track.setProperty = [trees = trees.get()](const glm::vec3 &v) {
            trees->enabled = v.x > 0.5f;
        };
#ifndef DEBUG
        track.playback.playing = true;
#else
        track.playback.playing = false;
#endif // !DEBUG
    }
    {
        const cJSON *list;
        cJSON_ArrayForEach(list, get(get(layout, "instances"), "houses"))
            outside->add_child(b.instanced(list->string, list->string,
                                           instanceList(list), true));
    }

    // Trees and houses only shadow the baked sun, the forge lights would
    // redraw them all every frame
    {
        std::vector<std::shared_ptr<scn::Node>> stack{outside};
        while (!stack.empty()) {
            auto n = stack.back();
            stack.pop_back();
            if (auto shadow = n->get_component<component::Shadow>())
                shadow->bakedOnly = true;
            for (auto &c : n->children)
                stack.push_back(c);
        }
    }

    // ---- Forge lights ----
    std::shared_ptr<scn::Node> forgeLight;
    {
        const cJSON *l;
        cJSON_ArrayForEach(l, get(layout, "lights")) {
            const std::string type = str(l, "type"), name = str(l, "name");
            if (type == "directional")
                continue; // the sun is built above
            const glm::vec3 color = vec3Of(l, "color");
            // intensity_w is in watts in the pack, the engine wants a plain
            // multiplier
            const float intensity =
                (float)cJSON_GetNumberValue(get(l, "intensity_w")) / 100.f;
            auto node = scn::Node::create(name, vec3Of(l, "position"));
            std::shared_ptr<component::Light> light;
            if (type == "point") {
                auto pl =
                    component::Light::point(color, intensity, 0.22f, 0.2f);
                pl.ambient = 0.05f;
                pl.castsShadow = name == "ForgeCoals";
                pl.shadowFar = 15.f;
                light = node->add_component<component::Light>(pl);
            } else {
                node->setLocalRotEuler(lookEuler(vec3Of(l, "direction")));
                auto sl = component::Light::spot(
                    color, intensity,
                    (float)cJSON_GetNumberValue(get(l, "cone_deg")), 0.09f,
                    0.032f);
                sl.ambient = 0.05f;
                sl.castsShadow = true;
                sl.shadowFar = 20.f;
                light = node->add_component<component::Light>(sl);
            }
            if (name == "ForgeCoals")
                forgeLight = node;
            inside->add_child(node);
        }
    }

    // ---- Bellows cycle, one Animator per animated part ----
    // The wind pulses once per bellows cycle (windPulsePeriod). The board
    // closes during the push (the first 45 %), the leather folds with it and
    // the forge light swells, then everything eases back while the bellows
    // refill. The tracks loop with the same period, so they stay in step with
    // the wind.
    {
        constexpr float period = 3.f, squeeze = 0.45f;
        constexpr int steps = 20; // keys per cycle
        const float openAngle = 0.20944f;
        auto smooth = [](float t) { return t * t * (3.f - 2.f * t); };
        // 0 open ... 1 closed, over one cycle
        auto closed = [&](float phase) {
            return phase < squeeze
                       ? smooth(phase / squeeze)
                       : 1.f - smooth((phase - squeeze) / (1.f - squeeze));
        };
        // 0 .. 1 at the peak of the wind push
        auto push = [&](float phase) {
            return phase < squeeze
                       ? std::sin(glm::pi<float>() * phase / squeeze)
                       : 0.f;
        };
        auto addCycle = [&](const std::shared_ptr<scn::Node> &node,
                            component::AnimTarget target,
                            const std::vector<glm::vec3> &keys) {
            if (!node)
                return;
            auto animator = node->get_component<component::Animator>();
            if (!animator)
                animator = node->add_component<component::Animator>();
            auto &track = animator->addTrack(
                target, component::Curve<glm::vec3>::smoothEven(keys, period),
                component::PlayMode::Loop);
            track.playback.playing = true;
        };
        auto find = [&](const char *name) -> std::shared_ptr<scn::Node> {
            for (auto &c : inside->children)
                if (c->name == name)
                    return c;
            return nullptr;
        };

        std::vector<glm::vec3> board, leather, glow;
        const glm::vec3 idle(1.0f, 0.42f, 0.12f), peak(1.0f, 0.75f, 0.4f);
        for (int k = 0; k <= steps; k++) {
            const float phase = (float)k / steps;
            const float angle = openAngle * (1.f - closed(phase));
            board.push_back({0.f, 0.f, angle});
            // The leather is exported open, it scales with tan(angle)
            leather.push_back(
                {1.f, std::max(std::tan(angle) / std::tan(openAngle), 0.02f),
                 1.f});
            glow.push_back(glm::mix(idle, peak, push(phase)));
        }
        addCycle(find("Bellows_Top"), component::AnimTarget::Rotation, board);
        addCycle(find("Bellows_Leather"), component::AnimTarget::Scale,
                 leather);
        addCycle(forgeLight, component::AnimTarget::Color, glow);
    }

    // ---- Camera: one 80 s flight, from the valley down into the forge ----
    // The doors run on the same clock (open 27-31 s, close 36-40 s), so keep
    // the shots that pass through them around those times.
    struct Shot {
        float time;
        glm::vec3 pos, lookAt;
    };
    const Shot shots[] = {
        {0.f, {-170.f, 115.f, 250.f}, {10.f, 0.f, -30.f}},     // valley
        {6.f, {-95.f, 62.f, 150.f}, {5.f, 0.f, -10.f}},        // descend
        {12.5f, {-38.f, 24.f, 62.f}, {0.f, 2.f, 0.f}},         // village
        {18.5f, {-8.f, 5.f, 20.f}, {0.f, 2.f, 3.f}},           // path
        {24.5f, {0.f, 1.8f, 9.f}, {0.f, 1.4f, 0.f}},           // at the doors
        {31.f, {0.f, 1.7f, 4.2f}, {0.f, 1.3f, 0.f}},           // doors open
        {37.f, {0.f, 1.65f, 1.5f}, {0.2f, 0.95f, -2.f}},       // inside
        {43.f, {-0.3f, 1.35f, -0.9f}, {0.2f, 0.85f, -2.4f}},   // coal bed
        {49.f, {0.2f, 1.2f, -1.75f}, {0.2f, 0.8f, -2.4f}},     // coal macro
        {55.5f, {-1.7f, 1.25f, -1.1f}, {-1.f, 0.8f, -2.25f}},  // bellows
        {61.5f, {1.7f, 1.6f, 0.9f}, {0.45f, 0.95f, -0.35f}},   // anvil wide
        {67.5f, {0.95f, 1.25f, 0.4f}, {0.43f, 0.95f, -0.34f}}, // anvil
        {74.f, {0.68f, 1.1f, 0.12f}, {0.43f, 0.95f, -0.34f}},  // strike
        {80.f, {-0.2f, 1.8f, 1.6f}, {0.45f, 1.f, -0.5f}},      // pull back
    };
    std::vector<component::Curve<glm::vec3>::Point> positions, rotations;
    for (const Shot &shot : shots) {
        glm::vec3 r = lookEuler(shot.lookAt - shot.pos);
        if (!rotations.empty()) { // keep yaw continuous, no spin the long way
            const float last = rotations.back().second.y;
            while (r.y - last > glm::pi<float>())
                r.y -= glm::two_pi<float>();
            while (r.y - last < -glm::pi<float>())
                r.y += glm::two_pi<float>();
        }
        positions.push_back({shot.time, shot.pos});
        rotations.push_back({shot.time, r});
    }

    auto camNode = scn::Node::create("Camera", positions[0].second);
    camNode->setLocalRotEuler(rotations[0].second);
    scene.activeCamera =
        camNode->add_component<component::Camera>(50.f, 1.f, 1.f, 0.1f, 1000.f);

#ifdef DEBUG
    camNode->add_component<component::UserMovement>(5.f, 0.1f);
#endif // DEBUG

    {
        auto animator = camNode->add_component<component::Animator>();
        for (auto [target, points] :
             {std::pair{component::AnimTarget::Position, &positions},
              std::pair{component::AnimTarget::Rotation, &rotations}}) {
            auto &track = animator->addTrack(
                target, component::Curve<glm::vec3>::smooth(*points),
                component::PlayMode::Single);
#ifndef DEBUG
            track.playback.playing = true;
#else
            track.playback.playing = false;
#endif // !DEBUG
        }
    }

    // ---- Animate Doors -----
    // Same 80 s clock as the camera: open from 27 to 31 s, close from 36 to 40
    // s
    {
        constexpr float duration = 80.f;
        auto animateDoor = [&](const char *name, float openAngle) {
            for (auto &c : forge->children) {
                if (c->name != name)
                    continue;
                const glm::vec3 shut(0.f), open(0.f, openAngle, 0.f);
                auto animator = c->add_component<component::Animator>();
                auto &track = animator->addTrack(
                    component::AnimTarget::Rotation,
                    component::Curve<glm::vec3>::flat({{0.f, shut},
                                                       {27.f, shut},
                                                       {31.f, open},
                                                       {36.f, open},
                                                       {40.f, shut},
                                                       {duration, shut}}),
                    component::PlayMode::Single);
#ifndef DEBUG
                track.playback.playing = true;
#else
                track.playback.playing = false;
#endif
            }
        };
        animateDoor("Door_L", glm::radians(90.f));
        animateDoor("Door_R", glm::radians(-90.f));
    }

    // ---- Assemble ----
    scene.root->add_child(camNode);
    scene.root->add_child(outside);
    scene.root->add_child(forge);
    scene.root->add_child(inside);
    auto skyNode = scn::Node::create("Skybox");
    scene.skybox = skyNode->add_component<component::Skybox>("skybox.png");
    scene.root->add_child(skyNode);

    cJSON_Delete(layout);
    return scene;
}

} // namespace scenes
