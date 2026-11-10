#include "smithy_scene.hpp"
#include "smithy_layout.hpp"

#include "../../components/animator.hpp"
#include "../../components/camera.hpp"
#include "../../components/instances.hpp"
#include "../../components/light.hpp"
#include "../../components/mesh.hpp"
#include "../../components/particle.hpp"
#include "../../components/physicsbody.hpp"
#include "../../components/shadow.hpp"
#include "../../components/skybox.hpp"
#include "../../loaders/model_loader.hpp"
#include "../../loaders/texture_loader.hpp"
#include "../../render/compute_shader.hpp"
#include "../../render/material.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace scenes {

namespace {

using component::AnimTarget;
using component::PlayMode;
using V = glm::vec3;
using Curve = component::Curve<V>;

// Smithy space (smithy_layout.hpp) -> room: its -x side (chimney back,
// workbench, bellows stand) against the room's -x wall, the hearth facing the
// room. The room is the forge building's inside, x -3.5..3.5, z -3.05..3.
const V SMITHY_AT{-2.13f, 0.f, -0.6f};

// Every timed event of the shot, seconds from the start
constexpr float DOORS_SHUT = 3.5f;  // doors close behind the camera
constexpr float GUST = 15.f;        // the big blow that throws coal out
constexpr float GUST_LENGTH = 1.6f;
constexpr float SLOW_IN = 24.f;     // slow motion over the anvil strike
constexpr float SLOW_OUT = 31.f;
constexpr float SLOW_RATE = 0.2f;
constexpr float END = 44.f;

V vec(const float v[3]) { return {v[0], v[1], v[2]}; }

// Euler (pitch, yaw, 0) that points a node's -Z along d
V lookEuler(V d) {
    d = glm::normalize(d);
    return {std::asin(d.y), std::atan2(-d.x, -d.z), 0.f};
}

void makeStatic(component::PhysicsBody &pb) {
    pb.body.mass = pb.body.invMass = 0.f;
    pb.body.invInertiaLocal = pb.body.invInertiaWorld = glm::mat3(0.f);
}

// The fire: the bellows animation writes how hard they push, the timeline
// the big gust. Heat follows the air with a lag (coal takes a moment to flare
// up and to dim) and drives the forge light, the glow of the coal and the
// embers. The air itself is the physics wind over the coal bed.
class Forge : public component::ComponentBase<Forge> {
  public:
    float push = 0.f; // 0..1, the bellows closing
    float gust = 0.f; // 0..1, the big blow
    float heat = 0.f;

    // Room space box the air blows through, and the wind at full push
    V airMin{0.f}, airMax{0.f};
    V airDir{1.f, 0.f, 0.f};
    // Sideways spread of the blast toward the box's z edges, so it fans out
    // of the arch instead of pushing everything straight along x
    float fan = 0.f;
    float pushWind = 0.f, gustWind = 0.f;

    std::weak_ptr<component::Light> light;
    float lightIdle = 0.f, lightHot = 0.f;
    std::vector<std::shared_ptr<render::Material>> glowing;
    float glowIdle = 1.f, glowHot = 1.f;
    std::weak_ptr<component::ParticleSystem> embers;
    float emberIdle = 0.1f, emberHot = 0.01f; // seconds between embers

    void update(float dt) override {
        const float target = std::clamp(std::max(push, 0.f) + gust, 0.f, 1.f);
        // Flares up in ~0.3 s, dies down in ~1.2 s
        const float tau = target > heat ? 0.3f : 1.2f;
        heat += (target - heat) * (1.f - std::exp(-dt / tau));

        if (auto l = light.lock())
            l->intensity = lightIdle + (lightHot - lightIdle) * heat;
        for (auto &m : glowing)
            m->setFloat("uEmissiveStrength",
                        glowIdle + (glowHot - glowIdle) * heat);
        if (auto e = embers.lock())
            e->spawnInterval = emberIdle + (emberHot - emberIdle) * heat;
    }

    V wind(const V &p) const {
        if (glm::any(glm::lessThan(p, airMin)) ||
            glm::any(glm::greaterThan(p, airMax)))
            return V(0.f);
        const float midZ = (airMin.z + airMax.z) / 2,
                    halfZ = (airMax.z - airMin.z) / 2;
        const V dir =
            glm::normalize(airDir + V(0.f, 0.f, fan * (p.z - midZ) / halfZ));
        return dir * (pushWind * std::max(push, 0.f) + gustWind * gust);
    }

    const char *type_name() const override { return "Forge"; }
};

std::vector<Curve::Point> points(const smithy::Track &track) {
    std::vector<Curve::Point> out;
    for (int i = 0; i < track.count; i++)
        out.push_back({track.keys[i].t,
                       {track.keys[i].x, track.keys[i].y, track.keys[i].z}});
    return out;
}

// Step curve, 1 from each hit for `length` seconds, 0 between, over the loop
Curve hitPulses(float length) {
    std::vector<Curve::Point> keys{{0.f, V(0.f)}};
    for (float hit : smithy::hammerHits) {
        if (hit > 0.f)
            keys.push_back({hit, V(0.f)});
        keys.push_back({hit, V(1.f)});
        keys.push_back({hit + length, V(1.f)});
        keys.push_back({hit + length, V(0.f)});
    }
    keys.push_back({smithy::LOOP, V(0.f)});
    return Curve::flat(keys);
}

struct Builder {
    scn::Scene &scene;
    std::shared_ptr<render::Shader> litShader, instancedShader;
    std::shared_ptr<render::Material> lit;

    explicit Builder(scn::Scene &scene) : scene(scene) {
        litShader = std::make_shared<render::Shader>("default.vs", "lit.fs");
        instancedShader = std::make_shared<render::Shader>(
            "default_instanced.vs", "lit.fs", true);
        lit = std::make_shared<render::Material>("Lit", litShader);
    }

    // A node drawing one model of the ironheart pack (textures embedded)
    std::shared_ptr<scn::Node> prop(const std::string &name,
                                    const std::string &model, V pos,
                                    V rot = V(0.f), bool open = false) {
        auto node = scn::Node::create(name, pos);
        node->setLocalRotEuler(rot);
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(loader::loadModel(model + ".glb")),
            lit);
        node->add_component<component::Shadow>()->open = open;
        return node;
    }

    std::shared_ptr<component::ParticleSystem>
    particles(const std::shared_ptr<scn::Node> &node) {
        auto shader =
            std::make_shared<render::Shader>("particle.vs", "particle.fs");
        return node->add_component<component::ParticleSystem>(
            std::make_shared<render::Material>("Particles", shader),
            std::make_shared<render::ComputeShader>("spawnParticle.comp"),
            std::make_shared<render::ComputeShader>("updateParticle.comp"));
    }
};

} // namespace

scn::Scene smithyScene() {
    scn::Scene scene;
    Builder b(scene);

    // Coal: many small bodies, 120 Hz is enough. High angular drag so pieces
    // settle instead of rolling about.
    auto &phys = scene.physics_context;
    phys.fixedDt = 1.f / 120.f;
    phys.angularDrag = 12.f;

    // Animated things, slowed down together for the strike
    std::vector<std::weak_ptr<component::Animator>> slowed;
    std::vector<std::weak_ptr<component::ParticleSystem>> slowedParticles;

    // ---- The room: the forge building of the city, from the inside ----
    auto room = scn::Node::create("Room");
    // Seen from inside, its walls are its open side
    room->add_child(b.prop("Building", "SM_Forge_Building", V(0.f), V(0.f),
                           true));
    room->add_child(
        b.prop("Roof", "SM_Forge_Roof", V(0.f), V(0.f), true));
    room->add_child(b.prop("Shutters", "SM_Window_Shutters", {3.92f, 0, 0}));
    std::shared_ptr<scn::Node> doors[2] = {
        b.prop("Door_L", "SM_Door_L", {-1.1f, 0, 3.045f}),
        b.prop("Door_R", "SM_Door_R", {1.1f, 0, 3.045f})};
    for (int i = 0; i < 2; i++) {
        // Open inward (L negative, R positive about +Y), shut behind the
        // camera as it comes in
        const V open(0.f, glm::radians(i == 0 ? -75.f : 75.f), 0.f);
        doors[i]->setLocalRotEuler(open);
        doors[i]->add_component<component::Animator>()->addTrack(
            AnimTarget::Rotation,
            Curve::flat({{0.f, open}, {0.8f, open}, {DOORS_SHUT, V(0.f)}}),
            PlayMode::Single);
        room->add_child(doors[i]);
    }

    // Dressing from the same pack as the building, around the smithy
    room->add_child(b.prop("Workbench", "SM_Workbench", {3.15f, 0, 1.4f},
                           {0, glm::half_pi<float>(), 0}));
    room->add_child(b.prop("Spare hammer", "SM_Hammer", {3.1f, 0.868f, 1.1f},
                           {glm::half_pi<float>(), 1.745f, 0}));
    room->add_child(b.prop("Tool rack", "SM_ToolRack", {1.2f, 1.55f, -2.97f}));
    for (int i = 0; i < 3; i++)
        room->add_child(b.prop("Tongs", "SM_Tongs",
                               {0.8f + i * 0.25f, 1.5f, -2.88f},
                               {-glm::half_pi<float>(), 0,
                                -glm::half_pi<float>()}));
    room->add_child(b.prop("Quench tub", "SM_QuenchTub", {-0.35f, 0, -2.45f}));
    room->add_child(
        b.prop("Quench water", "SM_QuenchWater", {-0.35f, 0.43f, -2.45f}));
    room->add_child(b.prop("Barrel", "SM_Barrel", {-3.1f, 0, 2.55f}));
    room->add_child(b.prop("Barrel", "SM_Barrel", {-3.05f, 0, 1.85f},
                           {0, 1.f, 0}));
    room->add_child(b.prop("Wood pile", "SM_WoodPile", {3.1f, 0, 0.3f}));
    room->add_child(b.prop("Pelt", "SM_Pelt", {-3.47f, 2.5f, 2.3f},
                           {0, glm::half_pi<float>(), 0}));
    scene.root->add_child(room);

    // ---- The old-smithy pack ----
    // Its atlas: albedo, normal, roughness and emissive come with the models,
    // the specular strength (from the metal map) from the material
    auto specular = loader::loadTexture("SMY_Specular.jpg");
    auto smithyMat = [&](const char *name, float glow) {
        auto m = std::make_shared<render::Material>(name, b.litShader);
        m->setTexture(render::TextureSlot::Specular, specular);
        m->specular = V(1.f);
        m->setFloat("uEmissiveStrength", glow);
        return m;
    };
    auto propsMat = smithyMat("Smithy", 1.f);
    auto coalBedMat = smithyMat("Smithy coal bed", 1.f);

    auto smithy = scn::Node::create("Smithy", SMITHY_AT);
    std::vector<std::shared_ptr<scn::Node>> parts;
    std::map<std::string, std::shared_ptr<scn::Node>> byName;
    for (const smithy::Part &p : smithy::parts) {
        auto node = scn::Node::create(p.name, vec(p.pos));
        node->setLocalRotEuler(vec(p.rot));
        node->setLocalScale(vec(p.scale));
        const std::string name = p.name;
        if (p.model) {
            auto material = name == "CoalBed" ? coalBedMat : propsMat;
            node->add_component<component::Mesh>(
                std::make_shared<render::Model>(
                    loader::loadModel(std::string(p.model) + ".glb")),
                material);
            node->add_component<component::Shadow>();
        }
        // The pack's keyframes, all looping together
        const smithy::Track *tracks[] = {&p.positionKeys, &p.rotationKeys,
                                         &p.scaleKeys};
        const AnimTarget targets[] = {AnimTarget::Position,
                                      AnimTarget::Rotation, AnimTarget::Scale};
        std::shared_ptr<component::Animator> animator;
        for (int k = 0; k < 3; k++) {
            if (!tracks[k]->keys)
                continue;
            if (!animator)
                animator = node->add_component<component::Animator>();
            animator->addTrack(targets[k], Curve::smooth(points(*tracks[k])),
                               PlayMode::Loop);
        }
        if (animator)
            slowed.push_back(animator);
        (p.parent < 0 ? smithy : parts[p.parent])->add_child(node);
        parts.push_back(node);
        byName[name] = node;
    }
    scene.root->add_child(smithy);

    // ---- The coal bed ----
    const V bedMin = vec(smithy::coalBedMin), bedMax = vec(smithy::coalBedMax);
    const float floorY = bedMax.y;
    const V bedCenter{(bedMin.x + bedMax.x) / 2, floorY,
                      (bedMin.z + bedMax.z) / 2};
    const float hx = (bedMax.x - bedMin.x) / 2, hz = (bedMax.z - bedMin.z) / 2;
    const float halfPi = glm::half_pi<float>();

    auto coal = scn::Node::create("Coal");
    smithy->add_child(coal);
    auto collider = [&](const char *name, V pos, V rot, glm::vec2 half) {
        auto node = scn::Node::create(name, pos);
        node->setLocalRotEuler(rot);
        auto pb = node->add_component<component::PhysicsBody>();
        pb->collider = physics::PlaneDesc{half};
        makeStatic(*pb);
        pb->body.friction = 1.f;
        pb->body.restitution = 0.1f;
        coal->add_child(node);
    };
    // Planes face their local +Y: the walls are turned to face the bed.
    // Every side is as high as the tray's rim there. The -x end lies under
    // the chimney arch, its back wall keeps the coal in (as high as the
    // arch). The +x rim is the low one, the air blows that way.
    collider("Bed floor", bedCenter, V(0.f), {hx + 0.02f, hz + 0.02f});
    // Higher than the tray's rim (3.4 cm) by an invisible 5 cm: a full bed
    // would spill over it by itself, only the gust throws coal out
    const float rimX = smithy::trayRim[1] - floorY + 0.05f;
    const float rimZn = smithy::trayRim[2] - floorY;
    const float rimZp = smithy::trayRim[3] - floorY;
    constexpr float arch = 0.5f;
    collider("Bed wall -x", {bedMin.x, floorY + arch / 2, bedCenter.z},
             {0, 0, -halfPi}, {arch / 2, hz});
    collider("Bed wall +x", {bedMax.x, floorY + rimX / 2, bedCenter.z},
             {0, 0, halfPi}, {rimX / 2, hz});
    collider("Bed wall -z", {bedCenter.x, floorY + rimZn / 2, bedMin.z},
             {halfPi, 0, 0}, {hx, rimZn / 2});
    collider("Bed wall +z", {bedCenter.x, floorY + rimZp / 2, bedMax.z},
             {-halfPi, 0, 0}, {hx, rimZp / 2});
    {
        // The tray's top past the +x rim slopes down to the hearth's edge
        const glm::vec2 a{bedMax.x, smithy::trayRim[1]},
            c{smithy::ledgeEnd, smithy::ledgeHeight};
        const float slope = std::atan2(a.y - c.y, c.x - a.x);
        collider("Hearth ledge",
                 {(a.x + c.x) / 2, (a.y + c.y) / 2, bedCenter.z},
                 {0, 0, -slope}, {glm::length(c - a) / 2, hz});
    }
    // The room's floor, for coal thrown off the hearth
    {
        auto node = scn::Node::create("Floor", -SMITHY_AT);
        auto pb = node->add_component<component::PhysicsBody>();
        pb->collider = physics::PlaneDesc{};
        makeStatic(*pb);
        pb->body.friction = 1.f;
        pb->body.restitution = 0.15f;
        coal->add_child(node);
    }

    // Pieces of the ironheart pack's coal (their emissive map is the ember
    // cracks), one instanced draw per kind, the bodies are its children
    struct Kind {
        const char *model;
        float radius; // the pack's collider
    };
    const Kind kinds[] = {
        {"SM_Coal_A", 0.0259f}, {"SM_Coal_B", 0.0228f}, {"SM_Coal_C", 0.0338f}};
    // A bit bigger than the pack's spheres (85 % of the mesh), so pieces
    // don't sink into each other
    constexpr float colliderScale = 1.25f;
    constexpr int pieces = 120; // about one layer over the bed
    std::vector<std::shared_ptr<render::Material>> coalMats;
    std::shared_ptr<scn::Node> kindNodes[3];
    for (int k = 0; k < 3; k++) {
        kindNodes[k] = scn::Node::create(kinds[k].model);
        auto m = std::make_shared<render::Material>("Coal", b.instancedShader);
        coalMats.push_back(m);
        kindNodes[k]->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::loadModel(std::string(kinds[k].model) + ".glb")),
            m);
        kindNodes[k]->add_component<component::Instances>()->followChildren =
            true;
        kindNodes[k]->add_component<component::Shadow>();
        coal->add_child(kindNodes[k]);
    }
    std::mt19937 rng(7);
    auto uniform = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    // Dropped from a grid of layers above the bed, they settle below
    const float spacing = 2.f * 0.0338f * 1.1f * colliderScale * 1.1f;
    const int cols = (int)(2 * hx / spacing), rows = (int)(2 * hz / spacing);
    for (int i = 0; i < pieces; i++) {
        const int k = i % 3, layer = i / (cols * rows), cell = i % (cols * rows);
        const float col = (float)(cell % cols), row = (float)(cell / cols);
        const float scale = uniform(0.85f, 1.15f);
        const V pos{bedMin.x + spacing * (col + 0.5f) +
                        (2 * hx - cols * spacing) / 2,
                    floorY + spacing * ((float)layer + 0.6f),
                    bedMin.z + spacing * (row + 0.5f) +
                        (2 * hz - rows * spacing) / 2};
        auto piece = scn::Node::create("Coal " + std::to_string(i), pos);
        piece->setLocalRotEuler(
            {uniform(0, 6.28f), uniform(0, 6.28f), uniform(0, 6.28f)});
        piece->setLocalScale(V(scale));
        auto pb = piece->add_component<component::PhysicsBody>();
        const float r = kinds[k].radius * scale * colliderScale;
        const float mass = 0.03f * scale * scale * scale;
        pb->collider = physics::SphereDesc{r};
        pb->body.mass = mass;
        pb->body.invMass = 1.f / mass;
        pb->body.restitution = 0.1f;
        pb->body.friction = 1.5f;
        pb->body.invInertiaLocal = glm::mat3(1.f / (0.4f * mass * r * r));
        pb->body.invInertiaWorld = pb->body.invInertiaLocal;
        // Instances are drawn at the child's local transform: the kind
        // node sits at the coal node's origin, so that's smithy space
        kindNodes[k]->add_child(piece);
    }

    // ---- Lights: only the fire, the hot metal and the window ----
    auto lights = scn::Node::create("Lights");
    std::shared_ptr<component::Light> fire;
    {
        auto node = scn::Node::create("Forge fire",
                                      SMITHY_AT + bedCenter + V(0.1f, 0.45f, 0));
        auto light = component::Light::point({1.f, 0.42f, 0.12f}, 3.f, 0.22f,
                                             0.2f);
        light.ambient = 0.04f;
        light.castsShadow = true;
        light.shadowFar = 15.f;
        fire = node->add_component<component::Light>(light);
        lights->add_child(node);
    }
    {
        // Over the sword's glowing end on the anvil
        auto sword = byName.at("Sword");
        auto node = scn::Node::create("Hot metal",
                                      SMITHY_AT + sword->localPos() +
                                          V(0.f, 0.12f, 0.f));
        auto light = component::Light::point({1.f, 0.5f, 0.18f}, 0.6f, 0.35f,
                                             0.6f);
        node->add_component<component::Light>(light);
        lights->add_child(node);
    }
    {
        // Cold daylight through the window in the +x wall
        auto node = scn::Node::create("Window light", {5.f, 2.6f, -1.f});
        node->setLocalRotEuler(lookEuler({-0.91192f, -0.41036f, 0.f}));
        auto light = component::Light::spot({0.75f, 0.82f, 1.f}, 4.f, 30.f,
                                             35.f, 0.09f, 0.032f);
        light.ambient = 0.03f;
        light.castsShadow = true;
        light.shadowFar = 20.f;
        node->add_component<component::Light>(light);
        lights->add_child(node);
    }
    {
        // Fill: the fire's and the window's light bouncing off the floor and
        // the walls. Its ambient part lifts the shadows everywhere, its
        // direct part comes from below and only reaches faces turned down.
        auto node = scn::Node::create("Bounce light");
        node->setLocalRotEuler(lookEuler({0.f, 1.f, 0.001f}));
        auto light =
            component::Light::directional({1.f, 0.72f, 0.5f}, 0.3f, 0.55f);
        node->add_component<component::Light>(light);
        lights->add_child(node);
    }
    scene.root->add_child(lights);

    // ---- Effects, drawn last: additive, they don't write depth ----
    auto effects = scn::Node::create("Effects");
    std::shared_ptr<component::ParticleSystem> embers, strike, dust;
    {
        auto node = scn::Node::create("Embers", SMITHY_AT + bedCenter);
        embers = b.particles(node);
        embers->minColor = {1.0f, 0.8f, 0.3f, 1.0f};
        embers->maxColor = {0.9f, 0.2f, 0.0f, 0.0f};
        embers->minPosition = {-hx, 0.f, -hz};
        embers->maxPosition = {hx, 0.05f, hz};
        embers->minVelocity = {-0.15f, 0.3f, -0.15f};
        embers->maxVelocity = {0.25f, 1.0f, 0.15f};
        embers->minAccel = {0, -0.25f, 0};
        embers->maxAccel = {0.1f, 0.1f, 0};
        embers->minLife = 0.6f;
        embers->maxLife = 2.2f;
        embers->size = 0.012f;
        embers->stretch = 0.03f;
        embers->maxParticles = 4096;
        effects->add_child(node);
    }
    {
        // Where the pack's hammer lands (its spark emitter)
        auto node = scn::Node::create("Strike sparks",
                                      SMITHY_AT + vec(smithy::strikePoint));
        strike = b.particles(node);
        strike->minColor = {1.0f, 0.9f, 0.5f, 1.0f};
        strike->maxColor = {1.0f, 0.35f, 0.05f, 0.0f};
        strike->minPosition = V(-0.02f, 0.f, -0.02f);
        strike->maxPosition = V(0.02f, 0.02f, 0.02f);
        strike->minVelocity = {-1.6f, 0.6f, -1.6f};
        strike->maxVelocity = {1.6f, 2.6f, 1.6f};
        strike->minAccel = strike->maxAccel = {0.f, -9.8f, 0.f};
        strike->minLife = 0.25f;
        strike->maxLife = 0.9f;
        strike->size = 0.006f;
        strike->stretch = 0.025f;
        strike->spawnInterval = 0.0008f;
        strike->spawning = false;
        strike->maxParticles = 4096;
        effects->add_child(node);
    }
    {
        // Dust hanging in the window's light, drifting slowly
        auto node = scn::Node::create("Dust", {1.2f, 1.6f, -1.f});
        dust = b.particles(node);
        dust->minColor = {0.9f, 0.85f, 0.7f, 0.35f};
        dust->maxColor = {0.8f, 0.8f, 0.75f, 0.0f};
        dust->minPosition = {-2.2f, -1.4f, -1.4f};
        dust->maxPosition = {2.2f, 1.2f, 1.4f};
        dust->minVelocity = V(-0.03f, -0.02f, -0.03f);
        dust->maxVelocity = V(0.03f, 0.03f, 0.03f);
        dust->minAccel = V(-0.005f);
        dust->maxAccel = V(0.005f);
        dust->minLife = 6.f;
        dust->maxLife = 12.f;
        dust->size = 0.004f;
        dust->stretch = 0.f;
        dust->spawnInterval = 0.004f;
        dust->maxParticles = 4096;
        effects->add_child(node);
    }
    slowedParticles = {embers, strike};

    // ---- The fire, driven by the bellows ----
    auto forge = smithy->add_component<Forge>();
    forge->airMin = SMITHY_AT + V(bedMin.x - 0.05f, floorY, bedMin.z);
    forge->airMax = SMITHY_AT + V(bedMax.x + 0.05f, floorY + 0.35f, bedMax.z);
    // Out from under the arch, over the bed and up a bit
    forge->airDir = glm::normalize(V(1.f, 0.35f, 0.f));
    forge->fan = 0.6f;
    forge->pushWind = 1.5f;
    // Throws only the top pieces over the rim
    forge->gustWind = 3.f;
    forge->light = fire;
    forge->lightIdle = 2.2f;
    forge->lightHot = 5.f;
    forge->glowing = coalMats;
    forge->glowing.push_back(coalBedMat);
    forge->glowIdle = 1.f;
    forge->glowHot = 2.2f;
    forge->embers = embers;
    forge->emberIdle = 0.08f;
    forge->emberHot = 0.006f;
    phys.windField = [f = std::weak_ptr<Forge>(forge)](const V &p) {
        auto forge = f.lock();
        return forge ? forge->wind(p) : V(0.f);
    };
    {
        // The bellows' push, on the bellows' own clock (slowed with it)
        auto animator = byName.at("Bellows_Body")
                            ->get_component<component::Animator>();
        auto &track = animator->addTrack(
            AnimTarget::Property,
            Curve::smooth(points({smithy::bellowsPush,
                                  (int)std::size(smithy::bellowsPush)})),
            PlayMode::Loop);
        track.name = "bellows push";
        track.setProperty = [f = forge.get()](const V &v) { f->push = v.x; };
    }
    {
        // Sparks for a moment after every hammer blow, on the hammer's clock
        auto animator =
            byName.at("Hammer")->get_component<component::Animator>();
        auto &track = animator->addTrack(AnimTarget::Property, hitPulses(0.08f),
                                         PlayMode::Loop);
        track.name = "strike sparks";
        track.setProperty = [s = std::weak_ptr(strike)](const V &v) {
            if (auto p = s.lock())
                p->spawning = v.x > 0.5f;
        };
    }
    scene.root->add_child(effects);

    // The coal falls onto the bed and settles before the first frame
    for (int i = 0; i < 360; i++)
        scene.physics_step();

    // ---- Camera: one flight from the door to the anvil ----
    struct Shot {
        float time;
        V pos, lookAt;
    };
    // Room space. Bed (-2.49, 0.56, -0.83), bellows (-3.05, 0.73, 0.22),
    // anvil face (-1.32, 0.8, -1.81).
    const Shot shots[] = {
        {0.f, {0.f, 1.65f, 2.55f}, {-0.9f, 1.2f, -1.f}},     // in the door
        {3.5f, {0.f, 1.55f, 1.4f}, {-1.6f, 1.f, -0.8f}},     // doors shut
        {7.5f, {-1.55f, 1.35f, 1.55f}, {-3.f, 0.85f, 0.2f}}, // bellows
        {11.f, {-1.9f, 1.15f, 0.7f}, {-2.8f, 0.75f, -0.3f}}, // to the fire
        {14.f, {-1.55f, 1.05f, -0.3f}, {-2.55f, 0.6f, -0.85f}}, // coal
        {18.f, {-1.4f, 1.1f, -0.15f}, {-2.3f, 0.55f, -0.85f}},  // gust
        // The dolly zoom on the anvil's face, from the room's side (the
        // sword lies across the view, not toward the lens)
        {21.f, {-0.6f, 1.45f, -0.3f}, {-1.32f, 0.8f, -1.81f}}, // to anvil
        {24.f, {-0.89f, 1.35f, -0.59f}, {-1.32f, 0.8f, -1.81f}},
        {31.f, {-1.09f, 1.09f, -1.16f}, {-1.32f, 0.8f, -1.81f}},
        {36.f, {0.3f, 1.55f, -1.0f}, {-1.4f, 0.85f, -1.6f}}, // back out
        {END, {1.6f, 2.2f, 1.6f}, {-1.6f, 0.8f, -0.9f}},        // the room
    };
    std::vector<Curve::Point> positions, rotations;
    for (const Shot &shot : shots) {
        V r = lookEuler(shot.lookAt - shot.pos);
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
    auto camera =
        camNode->add_component<component::Camera>(50.f, 1.f, 1.f, 0.05f, 60.f);
    scene.activeCamera = camera;
    auto timeline = camNode->add_component<component::Animator>();
    timeline->addTrack(AnimTarget::Position, Curve::smooth(positions),
                       PlayMode::Single);
    timeline->addTrack(AnimTarget::Rotation, Curve::smooth(rotations),
                       PlayMode::Single);
    {
        // Vertigo: the camera dollies in on the anvil while the view widens,
        // so the anvil keeps its size and the room stretches behind it
        // (distance * tan(fov / 2) stays the same: 1.4 m at 30, 0.75 m at 53)
        auto &track = timeline->addTrack(
            AnimTarget::Property,
            Curve::flat({{0.f, V(50.f)},
                         {22.f, V(50.f)},
                         {24.f, V(30.f)},
                         {31.f, V(53.f)},
                         {34.f, V(50.f)},
                         {END, V(50.f)}}),
            PlayMode::Single);
        track.name = "fov";
        track.setProperty = [c = camera.get()](const V &v) { c->setFov(v.x); };
    }
    {
        // Fades in from the city shot's black doorway
        *scene.fade = 0.f;
        auto &track = timeline->addTrack(
            AnimTarget::Property,
            Curve::flat({{0.f, V(0.f)}, {0.3f, V(0.f)}, {2.3f, V(1.f)},
                         {END, V(1.f)}}),
            PlayMode::Single);
        track.name = "fade";
        track.setProperty = [fade = scene.fade](const V &v) { *fade = v.x; };
    }
    {
        // The big blow: the air throws the top coal over the low rim
        auto &track = timeline->addTrack(
            AnimTarget::Property,
            Curve::flat({{0.f, V(0.f)},
                         {GUST, V(0.f)},
                         {GUST + 0.3f, V(1.f)},
                         {GUST + GUST_LENGTH, V(0.f)},
                         {END, V(0.f)}}),
            PlayMode::Single);
        track.name = "gust";
        track.setProperty = [f = forge.get()](const V &v) { f->gust = v.x; };
    }
    {
        // Slow motion over the strike: the smithy's animations and sparks
        // run slower, the camera keeps its pace
        auto &track = timeline->addTrack(
            AnimTarget::Property,
            Curve::flat({{0.f, V(1.f)},
                         {SLOW_IN - 1.f, V(1.f)},
                         {SLOW_IN, V(SLOW_RATE)},
                         {SLOW_OUT - 0.5f, V(SLOW_RATE)},
                         {SLOW_OUT + 1.f, V(1.f)},
                         {END, V(1.f)}}),
            PlayMode::Single);
        track.name = "slow motion";
        track.setProperty = [slowed, slowedParticles](const V &v) {
            for (auto &a : slowed)
                if (auto animator = a.lock())
                    for (auto &t : animator->tracks)
                        t.playback.speed = v.x;
            for (auto &p : slowedParticles)
                if (auto ps = p.lock())
                    ps->timeScale = v.x;
        };
    }
    scene.root->add_child(camNode);

    // Every animation starts with the scene
    for (auto &animator : scene.getAllComponents<component::Animator>())
        animator->play();

    // Daylight sky through the door while it's still open
    auto sky = scn::Node::create("Skybox");
    scene.skybox = sky->add_component<component::Skybox>("skybox.png");
    scene.root->add_child(sky);

    return scene;
}

} // namespace scenes
