#include "smithy_scene.hpp"
#include "forge_layout.hpp"

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
#include <tuple>
#include <vector>

namespace scenes {

namespace {

using component::AnimTarget;
using component::PlayMode;
using V = glm::vec3;
using Curve = component::Curve<V>;

// The room is the high-poly smithy (forge_layout.hpp, room space: its floor
// at y = 0, the door in the +z wall) with its own props. The hammer and the
// bellows move on their own (no smith yet): procedural, see hammerSwing and
// the bellows below.

// Every timed event of the shot, seconds from the start
constexpr float DOORS_CLOSE = 1.6f; // the doors start to close behind the
constexpr float DOORS_SHUT = 3.5f;  // camera, they are shut
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

// The fire: the bellows animation writes how hard they push. Heat follows
// the air with a lag (coal takes a moment to flare up and to dim) and drives
// the forge light, the glow of the coal and the embers. The air swirls the
// coal in the bowl a little: the physics wind, round the bowl's middle.
class Forge : public component::ComponentBase<Forge> {
  public:
    float push = 0.f; // 0..1, the bellows closing
    float heat = 0.f;

    // The bowl: its middle (on the coal), how far out and up the air goes
    V centre{0.f};
    float radius = 0.f, height = 0.f;
    // At full push: how fast the air goes round, and up
    float swirl = 0.f, lift = 0.f;

    std::weak_ptr<component::Light> light;
    float lightIdle = 0.1f, lightHot = 2.f;
    std::vector<std::shared_ptr<render::Material>> glowing;
    float glowIdle = 0.3f, glowHot = 1.f;
    std::weak_ptr<component::ParticleSystem> embers;
    float emberIdle = 0.1f, emberHot = 0.01f; // seconds between embers

    void update(float dt) override {
        const float target = std::clamp(push, 0.f, 1.f);
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
        const V d = p - centre;
        if (glm::length(glm::vec2(d.x, d.z)) > radius || d.y > height)
            return V(0.f);
        // Round the middle (about y), up a little
        const V round = glm::normalize(V(-d.z, 0.f, d.x) + V(1e-6f, 0.f, 0.f));
        return (round * swirl + V(0.f, lift, 0.f)) * std::clamp(push, 0.f, 1.f);
    }

    const char *type_name() const override { return "Forge"; }
};

// The hammer's swing: up about its grip and down onto the sword, heavy
// blows and light taps. x of the value is the angle the head is lifted by
// (radians, 0 = on the sword), y how far the arm lifts the grip (0..1).
// Writes the moments it lands into hits; the loop's length is the curve's.
Curve hammerSwing(std::vector<float> &hits) {
    struct Stroke {
        float lift, up, hold, down; // radians, seconds
    };
    constexpr Stroke heavy{1.3f, 0.55f, 0.08f, 0.13f},
        tap{0.4f, 0.25f, 0.f, 0.1f};
    const Stroke strokes[] = {heavy, tap, tap, heavy, tap, heavy, heavy, tap};
    Curve curve;
    float t = 0.f;
    curve.keys.push_back({t, V(0.f), V(0.f), V(0.f)});
    for (const Stroke &s : strokes) {
        const V top(s.lift, s.lift / heavy.lift, 0.f);
        curve.keys.push_back({t += s.up, top, V(0.f), V(0.f)});
        if (s.hold > 0.f)
            curve.keys.push_back({t += s.hold, top, V(0.f), V(0.f)});
        // Lands fast and bounces a little off the metal
        t += s.down;
        hits.push_back(t);
        const float speed = 2.5f * s.lift / s.down;
        curve.keys.push_back(
            {t, V(0.f), V(-speed, -speed / heavy.lift, 0.f), V(2.f, 0.f, 0.f)});
        curve.keys.push_back(
            {t += 0.1f, V(0.1f * s.lift, 0.f, 0.f), V(0.f), V(0.f)});
        curve.keys.push_back({t += 0.15f, V(0.f), V(0.f), V(0.f)});
    }
    return curve;
}

// Step curve, 1 from each hit for `length` seconds, 0 between, over loop
Curve hitPulses(const std::vector<float> &hits, float loop, float length) {
    std::vector<Curve::Point> keys{{0.f, V(0.f)}};
    for (float hit : hits) {
        keys.push_back({hit, V(0.f)});
        keys.push_back({hit, V(1.f)});
        keys.push_back({hit + length, V(1.f)});
        keys.push_back({hit + length, V(0.f)});
    }
    keys.push_back({loop, V(0.f)});
    return Curve::flat(keys);
}

// The bellows press down and open up again on their own clock, seconds
constexpr float PRESS = 1.1f, BREATH = 3.2f;

// What the parts of the smithy scene share while it's built, in the order
// smithyScene() adds them
struct Builder {
    scn::Scene &scene;
    std::shared_ptr<render::Shader> litShader, instancedShader;
    std::shared_ptr<render::Material> lit;

    // Where the hammer lands, on the sword on the anvil
    const V hit = vec(forge::strike);
    // Animated things, slowed down together for the strike
    std::vector<std::weak_ptr<component::Animator>> slowed;
    std::vector<std::weak_ptr<component::ParticleSystem>> slowedParticles;
    std::shared_ptr<component::Animator> hammerClock, bellowsClock;
    std::vector<float> hammerHits; // when the hammer lands in its loop

    // The coal bed: the bowl in the hearth's top. A little under the coal
    // pile's bumpy top (bowl is its middle height), so the pieces settle
    // into it instead of resting on its highest bumps.
    const float floorY = forge::bowl[1] - 0.025f;
    const V bedCenter{forge::bowl[0], floorY, forge::bowl[2]};
    const float bedRadius = forge::bowlRadius;
    std::shared_ptr<scn::Node> coal;
    std::vector<std::shared_ptr<render::Material>> coalMats; // they glow

    std::shared_ptr<component::Light> fire;
    std::shared_ptr<component::ParticleSystem> embers, strike;

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

// The room: the high-poly smithy's house and its props, the doors shutting
// behind the camera as it comes in
void addRoom(Builder &b) {
    auto room = scn::Node::create("Room");
    room->add_child(b.prop("House", "HPF_House", V(0.f)));
    room->add_child(b.prop("Props", "HPF_Props", V(0.f)));
    // From the other packs: the anvil on its stump, the witcher swords
    room->add_child(b.prop("Anvil", "HPF_Anvil", V(0.f)));
    room->add_child(b.prop("Swords", "HPF_Swords", V(0.f)));

    for (auto [name, model, x, angle] :
         {std::tuple{"Door_L", "HPF_Door_L", forge::doorX0, -75.f},
          std::tuple{"Door_R", "HPF_Door_R", forge::doorX1, 75.f}}) {
        // Hinged on the wall's outer face, open outward
        auto door = b.prop(name, model, {x, 0.f, forge::doorOuter});
        const V open(0.f, glm::radians(angle), 0.f);
        door->setLocalRotEuler(open);
        door->add_component<component::Animator>()->addTrack(
            AnimTarget::Rotation,
            Curve::flat(
                {{0.f, open}, {DOORS_CLOSE, open}, {DOORS_SHUT, V(0.f)}}),
            PlayMode::Single);
        room->add_child(door);
    }
    b.scene.root->add_child(room);
}

// The hammer, swinging on its own clock (hammerSwing)
void addHammer(Builder &b) {
    // Swings in the plane along the anvil, the grip on the +x side: its
    // frame (forge_layout.hpp) turned half round y so the handle points -x
    // at the anvil, and half round the handle so the flat face (+y) looks
    // down and lands on the sword, the peen up
    auto hammer = b.prop("Hammer", "HPF_Hammer", V(0.f));
    const V grip = b.hit + V(forge::hammerReach, forge::hammerFace, 0.f);
    const glm::quat base = glm::angleAxis(glm::pi<float>(), V(0, 1, 0)) *
                           glm::angleAxis(glm::pi<float>(), V(1, 0, 0));
    b.hammerClock = hammer->add_component<component::Animator>();
    auto &track = b.hammerClock->addTrack(
        AnimTarget::Property, hammerSwing(b.hammerHits), PlayMode::Loop);
    track.name = "swing";
    track.setProperty = [n = hammer.get(), grip, base](const V &v) {
        // The head goes up about the grip (local z; turned over, a negative
        // angle lifts it), the arm lifts the grip and pulls it back a little
        n->setLocalRot(base * glm::angleAxis(-v.x, V(0, 0, 1)));
        n->setLocalPos(grip + V(0.06f, 0.18f, 0.f) * v.y);
    };
    b.slowed.push_back(b.hammerClock);
    b.scene.root->add_child(hammer);
}

// The bellows press down and open up again on their own clock; every press
// blows the air over the coal (see Forge, addFire)
void addBellows(Builder &b) {
    auto bellows = b.prop("Bellows", "HPF_Bellows", vec(forge::bellowsBase));
    b.bellowsClock = bellows->add_component<component::Animator>();
    b.bellowsClock->addTrack(
        AnimTarget::Scale,
        Curve::flat(
            {{0.f, V(1.f)}, {PRESS, V(1.f, 0.62f, 1.f)}, {BREATH, V(1.f)}}),
        PlayMode::Loop);
    b.slowed.push_back(b.bellowsClock);
    b.scene.root->add_child(bellows);
}

// The coal bed: static planes for the bowl, its wall, the hearth's ledge
// and the floor, and the pieces of coal dropped into it
void addCoal(Builder &b) {
    b.coal = scn::Node::create("Coal");
    b.scene.root->add_child(b.coal);
    const V bedCenter = b.bedCenter;
    const float bedRadius = b.bedRadius;
    // A static plane, facing its local +Y
    auto collider = [&](const char *name, V pos, glm::quat rot,
                        glm::vec2 half) {
        auto node = scn::Node::create(name, pos);
        node->setLocalRot(rot);
        auto pb = node->add_component<component::PhysicsBody>();
        pb->collider = physics::PlaneDesc{half};
        makeStatic(*pb);
        pb->body.friction = 1.f;
        pb->body.restitution = 0.1f;
        b.coal->add_child(node);
    };
    collider("Bed floor", bedCenter, glm::quat(1.f, 0.f, 0.f, 0.f),
             {bedRadius + 0.02f, bedRadius + 0.02f});
    // The bowl's round wall, in flat pieces facing in, an invisible 8 cm
    // higher than its rim: a full bed would spill over it by itself
    const float rim = forge::bowlRim - b.floorY + 0.08f;
    constexpr int SIDES = 16;
    const float chord =
        2.f * bedRadius * std::tan(glm::pi<float>() / SIDES) + 0.01f;
    for (int i = 0; i < SIDES; i++) {
        const float a = glm::two_pi<float>() * (float)i / SIDES;
        const V out{std::cos(a), 0.f, std::sin(a)};
        const V along{-out.z, 0.f, out.x};
        // Local x along the wall, y facing in, z down
        const glm::quat facing =
            glm::quat_cast(glm::mat3(along, -out, glm::cross(along, -out)));
        collider("Bed wall", bedCenter + out * bedRadius + V(0.f, rim / 2, 0.f),
                 facing, {chord / 2, rim / 2});
    }
    {
        // The hearth's top past the +x rim, sloping a little down to its
        // edge so spilled coal slides off onto the floor
        const glm::vec2 a{bedCenter.x + bedRadius, forge::bowlRim},
            c{forge::hearthMax[0], forge::bowlRim - 0.08f};
        const float slope = std::atan2(a.y - c.y, c.x - a.x);
        collider("Hearth ledge", {(a.x + c.x) / 2, (a.y + c.y) / 2, bedCenter.z},
                 glm::quat(V(0.f, 0.f, -slope)),
                 {glm::length(c - a) / 2, bedRadius + 0.1f});
    }
    {
        // The room's floor, for coal thrown off the hearth
        auto node = scn::Node::create("Floor", V(0.f));
        auto pb = node->add_component<component::PhysicsBody>();
        pb->collider = physics::PlaneDesc{};
        makeStatic(*pb);
        pb->body.friction = 1.f;
        pb->body.restitution = 0.15f;
        b.coal->add_child(node);
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
    // Smaller than the pack's pieces, so twice as many fit the bowl; as
    // heavy as the pack's, so the wind moves them as much
    constexpr float coalSize = 0.7f;
    std::shared_ptr<scn::Node> kindNodes[3];
    for (int k = 0; k < 3; k++) {
        kindNodes[k] = scn::Node::create(kinds[k].model);
        auto m = std::make_shared<render::Material>("Coal", b.instancedShader);
        b.coalMats.push_back(m);
        kindNodes[k]->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::loadModel(std::string(kinds[k].model) + ".glb")),
            m);
        kindNodes[k]->add_component<component::Instances>()->followChildren =
            true;
        kindNodes[k]->add_component<component::Shadow>();
        b.coal->add_child(kindNodes[k]);
    }
    std::mt19937 rng(7);
    auto uniform = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    // Dropped from a grid of layers above the bed (its points inside the
    // round wall), they settle below
    const float spacing = 2.f * 0.0338f * coalSize * 1.1f * colliderScale * 1.1f;
    std::vector<V> cells;
    const int across = (int)(2.f * bedRadius / spacing);
    for (int row = 0; row < across; row++)
        for (int col = 0; col < across; col++) {
            const V at =
                bedCenter + V(spacing * ((float)col + 0.5f - across / 2.f), 0.f,
                              spacing * ((float)row + 0.5f - across / 2.f));
            if (glm::length(at - bedCenter) < bedRadius - spacing / 2)
                cells.push_back(at);
        }
    // About as much coal as the square bed had in 16 layers
    constexpr int LAYERS = 12;
    const int pieces = (int)cells.size() * LAYERS;
    for (int i = 0; i < pieces; i++) {
        const int k = i % 3, layer = i / (int)cells.size();
        const float scale = uniform(0.85f, 1.15f);
        const V pos = cells[i % cells.size()] +
                      V(0.f, spacing * ((float)layer + 0.6f), 0.f);
        auto piece = scn::Node::create("Coal " + std::to_string(i), pos);
        piece->setLocalRotEuler(
            {uniform(0, 6.28f), uniform(0, 6.28f), uniform(0, 6.28f)});
        piece->setLocalScale(V(scale * coalSize));
        auto pb = piece->add_component<component::PhysicsBody>();
        // In world metres: the node's scale isn't applied to it
        const float r = kinds[k].radius * scale * coalSize * colliderScale;
        const float mass = 0.03f * scale * scale * scale;
        pb->collider = physics::SphereDesc{r};
        pb->body.mass = mass;
        pb->body.invMass = 1.f / mass;
        pb->body.restitution = 0.1f;
        pb->body.friction = 1.5f;
        pb->body.invInertiaLocal = glm::mat3(1.f / (0.4f * mass * r * r));
        pb->body.invInertiaWorld = pb->body.invInertiaLocal;
        // Instances are drawn at the child's local transform: the kind node
        // sits at the coal node's origin, so that's room space
        kindNodes[k]->add_child(piece);
    }
}

// The fire, the hot metal, the hammer's shadow, the window and the fills
void addLights(Builder &b) {
    const float windowFill = (forge::windowZ[0] + forge::windowZ[1]) / 2;
    auto lights = scn::Node::create("Lights");
    {
        auto node =
            scn::Node::create("Forge fire", b.bedCenter + V(0.1f, 0.45f, 0));
        auto light =
            component::Light::point({1.f, 0.42f, 0.12f}, 3.f, 0.22f, 0.2f);
        light.ambient = 0.10f;
        light.castsShadow = true;
        light.shadowFar = 15.f;
        b.fire = node->add_component<component::Light>(light);
        lights->add_child(node);
    }
    {
        // Over the hot metal on the anvil, where the hammer lands
        auto node = scn::Node::create("Hot metal", b.hit + V(0.f, 0.12f, 0.f));
        auto light =
            component::Light::point({1.f, 0.5f, 0.18f}, 0.6f, 0.35f, 0.6f);
        node->add_component<component::Light>(light);
        lights->add_child(node);
    }
    {
        // A spot low across the anvil toward the back wall: the swinging
        // hammer throws its shadow, big, on the wall behind it. Square to
        // the swing's plane (it is along x), from the door side, a little
        // lower than the head: the shadow sweeps up the wall from the
        // workbench as the hammer lifts, about twice its size
        const V from = {b.hit.x + 0.2f, b.hit.y - 0.1f,
                        forge::doorInner - 0.2f};
        auto node = scn::Node::create("Hammer spot", from);
        node->setLocalRotEuler(lookEuler(b.hit + V(0.f, 0.35f, 0.f) - from));
        // Fades little with distance: about as bright on the wall as on the
        // anvil, so the shadow shows against the fire's light there
        auto light = component::Light::spot({1.f, 0.62f, 0.32f}, 0.75f, 16.f,
                                            24.f, 0.02f, 0.006f);
        light.castsShadow = true;
        light.shadowNear = 0.2f;
        light.shadowFar = 8.f;
        node->add_component<component::Light>(light);
        lights->add_child(node);
    }
    {
        // Cold daylight through the window in the +x wall, from outside it
        // down into the room
        auto node = scn::Node::create(
            "Window light",
            {forge::wallsMax[0] + 0.9f, forge::windowY[1] + 0.4f, windowFill});
        node->setLocalRotEuler(lookEuler({-0.91192f, -0.41036f, 0.f}));
        auto light = component::Light::spot({0.75f, 0.82f, 1.f}, 4.f, 30.f,
                                            35.f, 0.09f, 0.032f);
        light.ambient = 0.015f;
        light.castsShadow = true;
        light.shadowFar = 20.f;
        node->add_component<component::Light>(light);
        lights->add_child(node);
    }
    {
        // Fill: the fire's and the window's light bouncing off the floor
        // and the walls. Its ambient part lifts the shadows everywhere, its
        // direct part comes from below and only reaches faces turned down.
        auto node = scn::Node::create("Bounce light");
        node->setLocalRotEuler(lookEuler({0.f, 1.f, 0.001f}));
        auto light =
            component::Light::directional({1.f, 0.72f, 0.5f}, 0.2f, 0.3f);
        node->add_component<component::Light>(light);
        lights->add_child(node);
    }
    // Fill lights, no shadows: so the corners away from the fire aren't
    // black. Warm where the fire's light bounces around (over the
    // workbenches along the back wall, by the buckets at the door), cool
    // inside the window.
    for (auto [name, pos, color, intensity] :
         {std::tuple{"Fill workbenches", V(1.6f, 2.3f, -1.6f),
                     V(1.f, 0.7f, 0.45f), 0.9f},
          std::tuple{"Fill buckets", V(-0.6f, 2.2f, 1.7f), V(1.f, 0.65f, 0.4f),
                     0.8f},
          std::tuple{"Fill hearth corner", V(-2.6f, 2.3f, -1.8f),
                     V(1.f, 0.62f, 0.35f), 0.7f},
          std::tuple{"Fill window", V(3.3f, 2.1f, windowFill),
                     V(0.7f, 0.8f, 1.f), 0.6f}}) {
        auto node = scn::Node::create(name, pos);
        auto light =
            component::Light::point(color, intensity * 0.25f, 0.35f, 0.4f);
        light.ambient = 0.01f;
        node->add_component<component::Light>(light);
        lights->add_child(node);
    }
    b.scene.root->add_child(lights);
}

// Particles, drawn last (additive, they don't write depth): embers over the
// coal, sparks where the hammer lands, dust in the window's light
void addEffects(Builder &b) {
    auto effects = scn::Node::create("Effects");
    {
        auto node = scn::Node::create("Embers", b.bedCenter);
        auto &embers = b.embers = b.particles(node);
        embers->minColor = {1.0f, 0.8f, 0.3f, 1.0f};
        embers->maxColor = {0.9f, 0.2f, 0.0f, 0.0f};
        // Over the bowl (a square inside its circle)
        const float half = b.bedRadius * 0.7f;
        embers->minPosition = {-half, 0.f, -half};
        embers->maxPosition = {half, 0.05f, half};
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
        // Where the hammer lands
        auto node = scn::Node::create("Strike sparks", b.hit);
        auto &strike = b.strike = b.particles(node);
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
        auto node = scn::Node::create(
            "Dust", {2.f, 1.4f, (forge::windowZ[0] + forge::windowZ[1]) / 2});
        auto dust = b.particles(node);
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
    b.slowedParticles = {b.embers, b.strike};
    b.scene.root->add_child(effects);
}

// The fire, driven by the bellows: the Forge on the coal, the bellows'
// push on their clock, the strike's sparks on the hammer's
void addFire(Builder &b) {
    auto forge = b.coal->add_component<Forge>();
    forge->centre = b.bedCenter;
    forge->radius = forge::bowlRadius;
    forge->height = 0.3f;
    // A press swirls the coal a bit, it doesn't blow it away
    forge->swirl = 1.6f;
    forge->lift = 1.3f;
    forge->light = b.fire;
    forge->lightIdle = 1.2f;
    forge->lightHot = 5.f;
    forge->glowing = b.coalMats;
    forge->glowIdle = 0.3f;
    forge->glowHot = 2.2f;
    forge->embers = b.embers;
    forge->emberIdle = 0.08f;
    forge->emberHot = 0.006f;
    b.scene.physics_context.windField =
        [f = std::weak_ptr<Forge>(forge)](const V &p) {
            auto forge = f.lock();
            return forge ? forge->wind(p) : V(0.f);
        };
    {
        // The bellows' push while they press down, on their own clock
        auto &track =
            b.bellowsClock->addTrack(AnimTarget::Property,
                                     Curve::flat({{0.f, V(0.f)},
                                                  {0.1f, V(1.f)},
                                                  {PRESS - 0.1f, V(1.f)},
                                                  {PRESS + 0.1f, V(0.f)},
                                                  {BREATH, V(0.f)}}),
                                     PlayMode::Loop);
        track.name = "bellows push";
        track.setProperty = [f = forge.get()](const V &v) { f->push = v.x; };
    }
    {
        // Sparks for a moment after every hammer blow, on the hammer's clock
        auto &track = b.hammerClock->addTrack(
            AnimTarget::Property,
            hitPulses(b.hammerHits,
                      b.hammerClock->tracks.front().curve.getDuration(), 0.08f),
            PlayMode::Loop);
        track.name = "strike sparks";
        track.setProperty = [s = std::weak_ptr(b.strike)](const V &v) {
            if (auto p = s.lock())
                p->spawning = v.x > 0.5f;
        };
    }
}

// The camera: one flight from the door to the anvil, the vertigo on the
// anvil, the fade in, and the slow motion over the strike
void addCamera(Builder &b) {
    struct Shot {
        float time;
        V pos, lookAt;
    };
    // Room space: the door at x 0.8..2.95 in the +z wall, the hearth
    // against the -x wall, its bowl at the coal's top, the bellows on its
    // +z side, the anvil in the middle of the room along x
    const V hit = b.hit;
    const V door{(forge::doorX0 + forge::doorX1) / 2, 0.f, forge::doorInner};
    const V bowl = vec(forge::bowl);
    const V bellows =
        vec(forge::bellowsBase) + V(0.f, forge::bellowsHeight * 0.6f, 0.f);
    const Shot shots[] = {
        // In the door, between the open leaves, and on into the room past
        // their reach (1.07 m from the hinges) before they shut behind
        {0.f, door + V(0.f, 1.65f, 0.35f), {0.3f, 1.1f, -1.2f}},
        {DOORS_CLOSE, door + V(-0.35f, 1.6f, -1.25f), {-0.6f, 1.05f, -0.8f}},
        {DOORS_SHUT, door + V(-0.6f, 1.55f, -1.6f), {-1.6f, 1.f, 0.f}},
        // The bellows
        {7.5f, bellows + V(1.7f, 0.65f, 0.4f), bellows + V(0.f, 0.f, -0.2f)},
        // To the fire, the coal
        {11.f, bowl + V(1.35f, 0.45f, 1.f), bowl + V(0.f, -0.05f, 0.f)},
        {14.f, bowl + V(1.f, 0.5f, 0.35f), bowl + V(-0.05f, -0.02f, 0.f)},
        {18.f, bowl + V(1.15f, 0.45f, 0.5f), bowl + V(0.1f, -0.05f, 0.f)},
        // The dolly zoom on the anvil's face, from the room's side (the
        // sword lies across the view, not toward the lens)
        {21.f, hit + V(0.85f, 0.65f, 1.0f), hit},
        {24.f, hit + V(0.43f, 0.55f, 1.22f), hit},
        {31.f, hit + V(0.23f, 0.29f, 0.65f), hit},
        // Back out, the whole room
        {36.f, hit + V(1.9f, 0.8f, 1.6f), hit + V(-1.f, 0.1f, -0.3f)},
        {END, {3.4f, 2.2f, 1.9f}, {-1.6f, 0.8f, -0.6f}},
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
    b.scene.activeCamera = camera;
    auto timeline = camNode->add_component<component::Animator>();
    timeline->addTrack(AnimTarget::Position, Curve::smooth(positions),
                       PlayMode::Single);
    timeline->addTrack(AnimTarget::Rotation, Curve::smooth(rotations),
                       PlayMode::Single);
    {
        // Vertigo: the camera dollies in on the anvil while the view widens,
        // so the anvil keeps its size and the room stretches behind it
        // (distance * tan(fov / 2) stays the same: 1.4 m at 30, 0.75 m at 53)
        auto &track = timeline->addTrack(AnimTarget::Property,
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
        *b.scene.fade = 0.f;
        auto &track = timeline->addTrack(
            AnimTarget::Property,
            Curve::flat(
                {{0.f, V(0.f)}, {0.3f, V(0.f)}, {2.3f, V(1.f)}, {END, V(1.f)}}),
            PlayMode::Single);
        track.name = "fade";
        track.setProperty = [fade = b.scene.fade](const V &v) { *fade = v.x; };
    }
    {
        // Slow motion over the strike: the smithy's animations and sparks
        // run slower, the camera keeps its pace
        auto &track =
            timeline->addTrack(AnimTarget::Property,
                               Curve::flat({{0.f, V(1.f)},
                                            {SLOW_IN - 1.f, V(1.f)},
                                            {SLOW_IN, V(SLOW_RATE)},
                                            {SLOW_OUT - 0.5f, V(SLOW_RATE)},
                                            {SLOW_OUT + 1.f, V(1.f)},
                                            {END, V(1.f)}}),
                               PlayMode::Single);
        track.name = "slow motion";
        track.setProperty = [slowed = b.slowed,
                             slowedParticles = b.slowedParticles](const V &v) {
            for (auto &a : slowed)
                if (auto animator = a.lock())
                    for (auto &t : animator->tracks)
                        t.playback.speed = v.x;
            for (auto &p : slowedParticles)
                if (auto ps = p.lock())
                    ps->timeScale = v.x;
        };
    }
    b.scene.root->add_child(camNode);
}

} // namespace

scn::Scene smithyScene() {
    scn::Scene scene;
    Builder b(scene);

    // Coal: many small bodies, 120 Hz is enough. High angular drag so pieces
    // settle instead of rolling about.
    scene.physics_context.fixedDt = 1.f / 120.f;
    scene.physics_context.angularDrag = 12.f;

    addRoom(b);
    // What moves: the hammer and the bellows
    addHammer(b);
    addBellows(b);
    addCoal(b);
    addLights(b);
    addEffects(b);
    addFire(b);

    // The coal falls onto the bed and settles before the first frame
    for (int i = 0; i < 360; i++)
        scene.physics_step();

    addCamera(b);
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
