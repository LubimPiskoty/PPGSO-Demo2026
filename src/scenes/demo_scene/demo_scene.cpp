#include "demo_scene.hpp"
#include "../smithy/forge_layout.hpp"
#include "city_layout.hpp"
#include "lamp_posts.hpp"
#include "light_props.hpp"

#include "../../components/animator.hpp"
#include "../../components/camera.hpp"
#include "../../components/instances.hpp"
#include "../../components/light.hpp"
#include "../../components/mesh.hpp"
#include "../../components/particle.hpp"
#include "../../components/shadow.hpp"
#include "../../components/skybox.hpp"
#include "../../loaders/model_loader.hpp"
#include "../../loaders/texture_loader.hpp"
#include "../../render/material.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace scenes {

namespace {

using V = glm::vec3;
using Curve = component::Curve<V>;

// The demo's clock: the day flyby (low along the road and over the lake), a
// timelapse into the night seen from high above, the same flyby at night,
// then into the smithy's door, where the demo cuts to its own scene
// (scenes::smithyScene). The flyby's length comes from its path, see flyby().
constexpr float LAPSE = 27.f;    // seconds of timelapse between the flybys
constexpr float APPROACH = 8.5f; // from the night take's end into the door
// SceneManager name of shot 4, see main.cpp
constexpr const char *INTERIOR = "Smithy interior";

// The smithy stands where a house of the demo was (city::smithySite), on the
// south-east side of the market square, its door facing the square
const V FORGE_AT{city::smithySite.x, city::smithySite.y, city::smithySite.z};
const V SQUARE{2.f, -2.45f, -71.f};
// The middle of the smithy's doorway, x in its house's space
constexpr float DOOR_MID = (forge::doorX0 + forge::doorX1) / 2;

V vec(const float v[3]) { return {v[0], v[1], v[2]}; }

V position(const city::Placement &p) { return {p.x, p.y, p.z}; }
glm::quat rotation(const city::Placement &p) {
    return glm::quat(V(p.rx, p.ry, p.rz));
}

std::shared_ptr<render::Model> model(const std::string &name) {
    return std::make_shared<render::Model>(
        loader::loadModel("MC_" + name + ".glb"));
}

// Every copy of a model in one instanced draw, casting shadows. Flat tiles
// are open surfaces: nothing behind their front faces for the shadow pass.
std::shared_ptr<scn::Node>
instanced(const city::Group &group,
          const std::shared_ptr<render::Material> &material,
          bool open = false) {
    std::vector<component::Instance> instances;
    for (int i = 0; i < group.count; i++) {
        const city::Placement &p = group.copies[i];
        component::Instance instance;
        instance.position = position(p);
        instance.rotation = {p.rx, p.ry, p.rz};
        instance.scale = V(p.scale);
        if (p.mirrored)
            instance.scale.y = -p.scale; // flat, so only the normal flips
        instances.push_back(instance);
    }
    auto node = scn::Node::create(group.model);
    node->add_component<component::Mesh>(model(group.model), material);
    node->add_component<component::Instances>(std::move(instances));
    node->add_component<component::Shadow>()->open = open;
    return node;
}

struct Shot {
    float time;
    V pos, lookAt;
};

// The water's waves: direction (radians from +x), wavelength and height,
// metres. Must match the waves in water.vs.
struct Wave {
    float angle, length, height;
};
constexpr Wave WAVES[] = {{0.25f, 37.f, 0.16f},
                          {0.8f, 23.f, 0.1f},
                          {-0.2f, 14.f, 0.06f},
                          {1.3f, 9.f, 0.035f}};

// A boat on the water: every frame it is put on the waves under it, the
// same sum water.vs draws (the surface's height and slope at a point and
// time, in closed form), and rolls from side to side on its own a little.
class Floating : public component::ComponentBase<Floating> {
  public:
    V restPos{0.f}; // on still water
    glm::quat restRot{1.f, 0.f, 0.f, 0.f};
    // The waves are low and long, so a hull leans this much more than the
    // surface under it, or it would hardly show
    float lean = 2.5f;
    float roll = glm::radians(3.f); // its own, around its long (z) axis
    float rollPeriod = 3.f;         // seconds
    float phase = 0.f;              // so the boats don't roll together

    const char *type_name() const override { return "Floating"; }

    void update(float) override {
        auto n = node.lock();
        if (!n)
            return;
        const float time = render::Material::time;
        float height = 0.f;
        glm::vec2 slope(0.f); // dh/dx, dh/dz
        for (const Wave &wave : WAVES) {
            const glm::vec2 d(std::cos(wave.angle), std::sin(wave.angle));
            const float k = glm::two_pi<float>() / wave.length;
            const float at = k * glm::dot(d, glm::vec2(restPos.x, restPos.z)) -
                             std::sqrt(9.81f * k) * time;
            height += wave.height * std::sin(at);
            slope += wave.height * k * std::cos(at) * d;
        }
        // Deck along the surface: turn up onto the surface's normal
        const V normal =
            glm::normalize(V(-slope.x * lean, 1.f, -slope.y * lean));
        const V axis = glm::cross(V(0.f, 1.f, 0.f), normal);
        glm::quat tilt(1.f, 0.f, 0.f, 0.f);
        if (glm::length(axis) > 1e-5f)
            tilt = glm::angleAxis(std::acos(glm::min(normal.y, 1.f)),
                                  glm::normalize(axis));
        const float rolled =
            roll * std::sin(glm::two_pi<float>() * time / rollPeriod + phase);
        n->setLocalPos(restPos + V(0.f, height, 0.f));
        n->setLocalRot(tilt * restRot *
                       glm::angleAxis(rolled, V(0.f, 0.f, 1.f)));
    }
};

// Catmull-Rom through the shots' positions
Curve path(const std::vector<Shot> &shots) {
    std::vector<Curve::Point> positions;
    for (const Shot &shot : shots)
        positions.push_back({shot.time, shot.pos});
    return Curve::smooth(positions);
}

// The camera's path and the path of what it looks at: Catmull-Rom through
// the positions and through the points looked at, the camera turned to face
// it every frame (see the "look at" track), never through angles, so it
// doesn't overshoot or take the long way round
struct Flight {
    Curve pos, look;
    float end;
};

// Shots that each look at their lookAt
Flight flight(const std::vector<Shot> &shots) {
    std::vector<Curve::Point> looks;
    for (const Shot &shot : shots)
        looks.push_back({shot.time, shot.lookAt});
    return {path(shots), Curve::smooth(looks), shots.back().time};
}

// a, then b: a hard cut where b starts
Curve then(Curve a, const Curve &b) {
    a.keys.insert(a.keys.end(), b.keys.begin(), b.keys.end());
    return a;
}

// One point of a take's path: where, how fast the camera is there (m/s),
// and what it looks at there (nothing: along the path, see fly()). Slower
// than the take's cruise: it eases down to that over the EASE metres before
// the point and back up over the ones after it. Faster: the runs on either
// side of it are that fast.
struct Way {
    V pos;
    float speed;
    std::optional<V> look = std::nullopt;
};
constexpr float EASE = 20.f; // m

// One shot of the demo: the camera flies along its ways, at least cruise m/s
// between them
struct Take {
    const char *what;
    float cruise;
    std::vector<Way> ways;
};

// The story, read off a top-down picture of the city (tools/flyby/), every
// point checked against a height map of everything in it, at least 1 m
// clear of whatever stands within 2.5 m, the streets' people too: move them
// with care. The day: the farmers out in the east field in the morning, the
// ships on the river, the guards at the east gate and the archers on its
// towers, in through the gate along the street to the market, down the main
// street to the south gate. At night: from the river in over the port and the
// north gate, down the north street to the smithy.
const V EAST_GATE{84.f, 4.f, -33.f};
// Looking at the river, sideways to the camera's way along it
V riverView(V pos) { return pos + V{25.f, -1.5f, 70.f}; }
const std::vector<Take> DAY_TAKES = {
    {"Fields",
     3.5f,
     {// Low over the wheat past the farmers, rising slowly, the walls on the
      // island ahead all along
      {{214.f, 3.2f, -57.f}, 3.5f, EAST_GATE},
      {{198.f, 3.2f, -49.f}, 3.5f, EAST_GATE},
      {{180.f, 3.6f, -42.f}, 3.5f, EAST_GATE},
      {{162.f, 5.5f, -40.f}, 3.5f, EAST_GATE}}},
    {"River",
     3.f,
     {// Gliding east low over the water, looking across it: the ships, the
      // port and the walls slide by
      {{-1.f, -1.5f, -224.f}, 3.f, riverView({-1.f, -1.5f, -224.f})},
      {{14.f, -1.4f, -223.5f}, 3.f, riverView({14.f, -1.4f, -223.5f})},
      {{26.f, -1.2f, -223.f}, 3.f, riverView({26.f, -1.2f, -223.f})},
      {{44.f, -1.f, -222.f}, 3.f, riverView({44.f, -1.f, -222.f})}}},
    {"East gate",
     2.f,
     {// Over the bridge toward the two guards at the gate, then straight up
      // beside it, tilting up to the towers and the archers on the wall
      {{112.f, 1.6f, -12.f}, 2.f, V{83.f, -0.6f, -32.5f}},
      {{104.f, 1.6f, -17.5f}, 2.f, V{83.f, -0.6f, -32.5f}},
      {{99.f, 2.5f, -21.f}, 1.5f, V{83.f, 0.f, -32.5f}},
      {{99.f, 8.f, -21.f}, 1.5f, V{83.f, 5.f, -33.f}},
      {{99.f, 13.f, -21.f}, 1.5f, V{83.f, 9.f, -33.f}}}},
    {"Market",
     3.f,
     {// In from the gate, a little above the people, to the stalls
      {{60.f, 1.6f, -42.f}, 3.f},
      {{48.f, 1.7f, -47.f}, 3.f},
      {{35.f, 0.9f, -56.f}, 3.f},
      {{20.f, 2.f, -61.f}, 3.f}}},
    {"Main street",
     3.5f,
     {// South past the church, the south gate and its guards ahead
      {{3.f, 1.3f, -8.f}, 3.5f, V{8.f, 1.f, 57.f}},
      {{4.f, 1.5f, 15.f}, 3.5f, V{8.f, 1.f, 57.f}},
      {{6.f, 2.5f, 36.f}, 3.5f, V{8.f, 1.f, 57.f}}}},
};
// The night: in through the north gate, low down the north street to the
// market square, round the stalls on the west of its tree and back east
// along its south side, where the camera goes on to the smithy's door
// (that last stretch is in the scene, in the smithy's space)
const Take NIGHT_TAKE = {"Night",
                         3.f,
                         {{{6.f, 1.5f, -117.f}, 3.f},
                          {{8.5f, 1.6f, -104.f}, 3.f},
                          {{10.5f, 1.7f, -91.f}, 3.f},
                          {{9.f, 1.6f, -82.f}, 3.f},
                          {{0.f, 1.6f, -80.f}, 3.f},
                          {{-6.f, 1.6f, -70.f}, 3.f},
                          {{-3.f, 1.6f, -59.f}, 3.f},
                          {{5.f, 1.8f, -55.f}, 3.f}}};

// A key of a take's path: when, where, and how far between its ways (2.5:
// half way from the third to the fourth)
struct Mark {
    float time;
    V pos;
    float way;
};

// The take's path from a start time. Its shape: Catmull-Rom through the
// ways. Its speed: along the path, from the ways' speeds (see Way), keys
// every half second.
std::vector<Mark> flyby(const Take &take, float start) {
    const auto &ways = take.ways;
    std::vector<Curve::Point> byDistance;
    float total = 0.f;
    for (size_t i = 0; i < ways.size(); i++) {
        if (i > 0)
            total += glm::length(ways[i].pos - ways[i - 1].pos);
        byDistance.push_back({total, ways[i].pos});
    }
    const Curve shape = Curve::smooth(byDistance);
    // Finely along it: the distance flown up to each sample
    constexpr float step = 0.25f;
    const int samples = (int)std::ceil(total / step);
    std::vector<V> points;
    std::vector<float> flown;
    for (int k = 0; k <= samples; k++) {
        points.push_back(shape.sample(std::min(k * step, total)));
        flown.push_back(k == 0 ? 0.f
                               : flown.back() +
                                     glm::length(points[k] - points[k - 1]));
    }
    // Where along it each way is
    std::vector<float> at;
    for (const auto &[d, pos] : byDistance)
        at.push_back(flown[std::min((int)std::lround(d / step), samples)]);
    // Between which ways a distance is: the first, how far to the next
    auto between = [&](float s) {
        size_t i = 0;
        while (i + 2 < at.size() && at[i + 1] < s)
            i++;
        return std::pair{
            i, glm::clamp((s - at[i]) / (at[i + 1] - at[i]), 0.f, 1.f)};
    };
    auto speed = [&](float s) {
        // The runs between the ways: at least cruise
        const auto [i, u] = between(s);
        float v = glm::mix(std::max(ways[i].speed, take.cruise),
                           std::max(ways[i + 1].speed, take.cruise), u);
        // Easing down to the slow ways
        const float base = v;
        for (size_t j = 0; j < ways.size(); j++)
            if (ways[j].speed < take.cruise)
                v = std::min(v, glm::mix(ways[j].speed, base,
                                         glm::smoothstep(0.f, EASE,
                                                         std::abs(s - at[j]))));
        return v;
    };
    // The time at each sample
    std::vector<float> times{0.f};
    for (int k = 1; k <= samples; k++)
        times.push_back(times.back() +
                        (flown[k] - flown[k - 1]) /
                            ((speed(flown[k]) + speed(flown[k - 1])) * 0.5f));
    std::vector<Mark> marks;
    const float duration = times.back();
    int k = 0;
    for (float t = 0.f;; t += 0.5f) {
        if (duration - t < 0.25f) // the last key at the very end
            t = duration;
        while (k + 1 < samples && times[k + 1] < t)
            k++;
        const float u =
            glm::clamp((t - times[k]) / (times[k + 1] - times[k]), 0.f, 1.f);
        const auto [i, w] = between(glm::mix(flown[k], flown[k + 1], u));
        marks.push_back(
            {start + t, glm::mix(points[k], points[k + 1], u), i + w});
        if (t >= duration)
            break;
    }
    return marks;
}

// How long a take is, seconds
float length(const Take &take) {
    const auto marks = flyby(take, 0.f);
    return marks.back().time;
}

// The take from a start time, then the shots of tail (each looking at its
// lookAt). What the camera looks at moves smoothly from what one way looks
// at to the next's, through them as the camera passes the ways. A way with
// nothing to look at looks AHEAD metres along the path, a little down.
constexpr float AHEAD = 25.f;
Flight fly(const Take &take, float start, const std::vector<Shot> &tail = {}) {
    const std::vector<Mark> marks = flyby(take, start);
    std::vector<Shot> shots;
    for (const Mark &mark : marks)
        shots.push_back({mark.time, mark.pos, mark.pos});
    shots.insert(shots.end(), tail.begin(), tail.end());
    const Curve positions = path(shots);
    const float end = marks.back().time;

    const auto &ways = take.ways;
    std::vector<Curve::Point> looks;
    for (size_t j = 0; j < ways.size(); j++) {
        // When the camera passes the way
        size_t k = 0;
        while (k + 1 < marks.size() && marks[k].way < (float)j)
            k++;
        float time = marks[k].time;
        if (k > 0 && marks[k].way > marks[k - 1].way) {
            const float u = ((float)j - marks[k - 1].way) /
                            (marks[k].way - marks[k - 1].way);
            time = glm::mix(marks[k - 1].time, marks[k].time,
                            glm::clamp(u, 0.f, 1.f));
        }
        V target;
        if (ways[j].look) {
            target = *ways[j].look;
        } else {
            V along = positions.sample(std::min(time + 1.f, end)) -
                      positions.sample(std::max(time - 1.f, start));
            along.y = 0.f;
            target = ways[j].pos + glm::normalize(along) * AHEAD +
                     V{0.f, -0.15f * AHEAD, 0.f};
        }
        looks.push_back({time, target});
    }
    for (const Shot &shot : tail)
        looks.push_back({shot.time, shot.lookAt});
    return {positions, Curve::smooth(looks), end};
}

} // namespace

scn::Scene demoScene() {
    scn::Scene scene;

    // The clock's marks, seconds: the day's takes from 0 (each cuts to the
    // next where it ends), the timelapse, the night take, the cut to the
    // smithy's inside
    std::vector<float> dayCuts;
    float LAPSE_AT = 0.f;
    for (const Take &take : DAY_TAKES) {
        if (LAPSE_AT > 0.f)
            dayCuts.push_back(LAPSE_AT);
        LAPSE_AT += length(take);
    }
    const float NIGHT_AT = LAPSE_AT + LAPSE;
    const float CUT = NIGHT_AT + length(NIGHT_TAKE) + APPROACH;

    auto lit = std::make_shared<render::Material>(
        "Lit", std::make_shared<render::Shader>("default.vs", "lit.fs"));
    // Gerstner waves in water.vs, lit.fs adds the sky reflection
    auto water_material = std::make_shared<render::Material>(
        "Water", std::make_shared<render::Shader>("water.vs", "lit.fs"));
    water_material->setFloat("uReflectivity", 0.3f);
    water_material->setFloat("uRoughness", 0.1f);
    water_material->specular = glm::vec3(0.8f);
    water_material->shininess = 96.f;
    auto litInstanced = std::make_shared<render::Material>(
        "Lit instanced", std::make_shared<render::Shader>(
                             "default_instanced.vs", "lit.fs", true));
    // Trees: bend in the wind (foliage_instanced.vs), leaves seen from both
    // sides. Same wind direction as the water's swells.
    auto foliage = std::make_shared<render::Material>(
        "Foliage", std::make_shared<render::Shader>("foliage_instanced.vs",
                                                    "lit.fs", true));
    foliage->doubleSided = true;
    foliage->setVec3("uWindDir", glm::vec3(1.f, 0.f, 0.25f));
    foliage->setFloat("uWindStrength", 1.75f);
    foliage->setFloat("uPlantHeight", 11.f); // the tallest tree models

    // ---- The city (city_layout.hpp, sorted beforehand) ----
    auto cityNode = scn::Node::create("City");
    auto add = [&](const auto &groups,
                   const std::shared_ptr<render::Material> &material,
                   bool open = false) {
        for (const city::Group &group : groups)
            cityNode->add_child(instanced(group, material, open));
    };
    add(city::ground, litInstanced, true);
    add(city::houses, litInstanced);
    add(city::buildings, litInstanced);
    add(city::people, litInstanced);
    add(city::props, litInstanced);
    add(city::trees, foliage);

    // ---- Lamps: on from dusk (the "dusk" track on the clock below) ----
    // Each light with the intensity it has when on, and the materials whose
    // glass glows along. Models from tools/export_lights.py.
    std::vector<std::pair<std::weak_ptr<component::Light>, float>> lamps;
    std::vector<std::shared_ptr<render::Material>> glowing;
    auto lamp = [&](const std::shared_ptr<scn::Node> &node,
                    const component::Light &light,
                    const std::shared_ptr<scn::Node> &parent) {
        lamps.push_back(
            {node->add_component<component::Light>(light), light.intensity});
        parent->add_child(node);
    };
    auto glow = [&](const char *name) {
        auto material =
            std::make_shared<render::Material>(name, litInstanced->getShader());
        glowing.push_back(material);
        return material;
    };
    auto instancedModel = [&](const char *name, const char *file,
                              const std::shared_ptr<render::Material> &material,
                              std::vector<component::Instance> instances,
                              bool casts = true) {
        auto node = scn::Node::create(name);
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(loader::loadModel(file)), material);
        node->add_component<component::Instances>(std::move(instances));
        if (casts)
            node->add_component<component::Shadow>();
        cityNode->add_child(node);
    };

    // Soldiers carry a railroad lantern, hung on its bail from a hand, its
    // lens to the front, a spot shining ahead and down onto the road: the
    // gate guards in the spear hand (just outside the shaft), the soldiers
    // walking about the middle of the city in their free hand
    {
        // A soldier's own space: y up, facing +z
        struct Bearers {
            const city::Placement *copies;
            int count;
            V hand;           // where the bail hangs
            float onlyWithin; // metres of the market square, 0: all
        };
        const V spearHand{0.30f, 1.10f, 0.05f}, freeHand{-0.28f, 0.80f, 0.04f};
        constexpr float CENTRE = 30.f;
        const Bearers bearers[] = {
            {city::people_Armor_2_Guard,
             (int)std::size(city::people_Armor_2_Guard), spearHand, 0.f},
            {city::people_Armor_1_Walking,
             (int)std::size(city::people_Armor_1_Walking), freeHand, CENTRE},
            {city::people_Armor_2_Walking,
             (int)std::size(city::people_Armor_2_Walking), freeHand, CENTRE},
            {city::people_Warrior_3, (int)std::size(city::people_Warrior_3),
             freeHand, CENTRE},
        };
        const V lens = vec(lights::lanternLens);
        const float down = glm::radians(20.f);
        const V ahead{0.f, -std::sin(down), std::cos(down)};
        std::vector<component::Instance> instances;
        for (const Bearers &group : bearers)
            for (int i = 0; i < group.count; i++) {
                const city::Placement &man = group.copies[i];
                const float fromSquare =
                    glm::length(glm::vec2(man.x - SQUARE.x, man.z - SQUARE.z));
                if (group.onlyWithin > 0.f && fromSquare > group.onlyWithin)
                    continue;
                const glm::quat turn = rotation(man);
                const V at = position(man) + turn * (group.hand * man.scale);
                component::Instance instance;
                instance.position = at;
                instance.rotation = {man.rx, man.ry, man.rz};
                instance.scale = V(man.scale);
                instances.push_back(instance);

                auto node = scn::Node::create("Lantern light",
                                              at + turn * (lens * man.scale));
                node->setLocalRot(
                    glm::quatLookAt(turn * ahead, V(0.f, 1.f, 0.f)));
                auto light = component::Light::spot({1.f, 0.72f, 0.4f}, 3.f,
                                                    22.f, 34.f, 0.09f, 0.032f);
                // Everyone stands still: drawn once
                light.castsShadow = true;
                light.bakedShadow = true;
                light.shadowNear = 0.05f;
                light.shadowFar = 30.f;
                lamp(node, light, cityNode);
            }
        instancedModel("Lanterns", "LGT_Lantern.glb", glow("Lantern"),
                       std::move(instances));
    }
    // Street lamps along the streets inside the walls (lamp_posts.hpp, from
    // tools/flyby/lamps.py): the post, its lamp hung on the chain, a point
    // light in the lamp
    const auto lampMaterial = glow("Lamp");
    const auto streetLight = [] {
        return component::Light::point({1.f, 0.7f, 0.4f}, 2.5f, 0.14f, 0.07f);
    };
    {
        const V hook = vec(lights::postHook);
        const V light = vec(lights::lampLight);
        // The lamp itself casts no shadow (its glass and frame would shut its
        // own light in): a hidden disc in its base, under the light, casts
        // the dark spot under the lamp instead
        const V base = light - V(0.f, 0.15f, 0.f);
        constexpr float BASE_RADIUS = 0.085f, THIN = 0.08f;
        std::vector<component::Instance> posts, hung, discs;
        for (const lights::LampPost &post : lights::lampPosts) {
            component::Instance instance;
            instance.position = {post.x, post.y, post.z};
            instance.rotation = {0.f, post.yaw, 0.f};
            posts.push_back(instance);
            const glm::quat turn = glm::angleAxis(post.yaw, V(0.f, 1.f, 0.f));
            instance.position += turn * hook;
            hung.push_back(instance);
            component::Instance disc = instance;
            disc.position += turn * base;
            disc.scale = {1.f, THIN, 1.f};
            discs.push_back(disc);
            // Those around the market square cast shadows (6 maps each,
            // baked: nothing there moves)
            auto lit = streetLight();
            if (glm::length(glm::vec2(post.x - SQUARE.x, post.z - SQUARE.z)) <
                30.f) {
                lit.castsShadow = true;
                lit.bakedShadow = true;
                lit.shadowNear = 0.1f;
                lit.shadowFar = 25.f;
            }
            lamp(scn::Node::create("Street lamp light",
                                   instance.position + turn * light),
                 lit, cityNode);
        }
        instancedModel("Lamp posts", "LGT_LampPost.glb", glow("Lamp post"),
                       std::move(posts));
        instancedModel("Street lamps", "LGT_Lamp.glb", lampMaterial,
                       std::move(hung), false);
        // Only the shadow pass draws the discs (the Mesh is off, the Shadow
        // still uses it)
        auto node = scn::Node::create("Street lamp shadow discs");
        node->add_component<component::Mesh>(
                std::make_shared<render::Model>(
                    loader::createSphere(BASE_RADIUS)),
                litInstanced)
            ->enabled = false;
        node->add_component<component::Instances>(std::move(discs));
        node->add_component<component::Shadow>();
        cityNode->add_child(node);
    }
    // The ships' and boats' lamps, on board (hook positions in the hull's
    // space): standing on the ships' stern post and hung on the front of the
    // mast, on the boats' stern
    const auto shipLamps =
        std::make_shared<render::Material>("Ship lamp", lit->getShader());
    glowing.push_back(shipLamps);
    const auto lampModel =
        std::make_shared<render::Model>(loader::loadModel("LGT_Lamp.glb"));
    auto onBoard = [&](const std::shared_ptr<scn::Node> &hull,
                       const std::string &model, float scale) {
        std::vector<V> hooks;
        if (model == "Boat")
            hooks = {{0.f, 0.3f + lights::lampHeight, -0.85f}};
        else
            hooks = {{0.f, 3.47f + lights::lampHeight, -4.45f},
                     {0.f, 3.2f, 0.4f}}; // on the mast's bow side
        for (const V &at : hooks) {
            auto node = scn::Node::create("Ship lamp", at / scale);
            node->setLocalScale(V(1.f / scale));
            node->add_component<component::Mesh>(lampModel, shipLamps);
            node->add_component<component::Shadow>();
            auto light = streetLight();
            light.intensity = 2.f;
            lamp(scn::Node::create("Ship lamp light", vec(lights::lampLight)),
                 light, node);
            hull->add_child(node);
        }
    };
    // Grass and dirt patches: every copy laid onto the ground in one mesh
    // (tools/drape_ground.py), the flat quads hung over the slopes. They
    // don't cast: just above the ground, their shadows fell a little off
    // them and lifted them off it.
    for (const city::Group &group : city::decals) {
        auto node = scn::Node::create(group.model);
        node->add_component<component::Mesh>(
            model(std::string(group.model) + "_Draped"), lit);
        cityNode->add_child(node);
    }
    // Boats and ships ride the waves, each its own node, what stands on
    // their deck (crews, cargo) their children
    for (size_t i = 0; i < std::size(city::boats); i++) {
        const city::Boat &boat = city::boats[i];
        const city::Placement &p = boat.at;
        auto node = scn::Node::create(p.model, position(p));
        node->setLocalScale(V(p.scale));
        node->add_component<component::Mesh>(model(p.model), lit);
        node->add_component<component::Shadow>();
        auto floating = node->add_component<Floating>();
        floating->restPos = position(p);
        floating->restRot = rotation(p);
        floating->phase = 1.7f * (float)i;
        // The small boats rock more and quicker than the ships
        if (std::string(p.model) == "Boat") {
            floating->roll = glm::radians(6.f);
            floating->rollPeriod = 2.f;
        }
        onBoard(node, p.model, p.scale);
        const glm::quat toBoat = glm::inverse(rotation(p));
        for (int k = 0; k < boat.deckCount; k++) {
            const city::Placement &on = boat.deck[k];
            auto child = scn::Node::create(
                on.model, toBoat * (position(on) - position(p)) / p.scale);
            child->setLocalRot(toBoat * rotation(on));
            child->setLocalScale(V(on.scale / p.scale));
            child->add_component<component::Mesh>(model(on.model), lit);
            child->add_component<component::Shadow>();
            node->add_child(child);
        }
        cityNode->add_child(node);
    }
    // What the objects don't cover (the ground), where the demo has it
    for (const char *model : city::rest) {
        // Drawn by the water below instead
        if (std::string(model) == "Rest_Environment_Water")
            continue;
        if (std::string(model) == "Rest_Environment") {
            // The terrain, split in two (tools/drape_ground.py): the ground,
            // one open surface that casts, and the round grass and dirt
            // patches lying on it, which don't (see the decals above)
            auto ground = scn::Node::create("Rest_Ground");
            ground->add_component<component::Mesh>(
                std::make_shared<render::Model>(
                    loader::loadModel("MC_Rest_Ground.glb")),
                lit);
            ground->add_component<component::Shadow>()->open = true;
            cityNode->add_child(ground);
            auto patches = scn::Node::create("Rest_Patches");
            patches->add_component<component::Mesh>(
                std::make_shared<render::Model>(
                    loader::loadModel("MC_Rest_Patches.glb")),
                lit);
            cityNode->add_child(patches);
            continue;
        }
        auto node = scn::Node::create(model);
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::loadModel(std::string("MC_") + model + ".glb")),
            lit);
        // Houses and people pieces: closed
        node->add_component<component::Shadow>();
        cityNode->add_child(node);
    }
    // The demo's water, split into small triangles for the waves
    // (tools/subdivide_water.py)
    {
        auto node = scn::Node::create("Water", glm::vec3(0, -1, 0));
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(loader::loadModel("MC_Water.glb")),
            water_material);
        cityNode->add_child(node);
    }

    // ---- The smithy: the high-poly smithy's house (forge_layout.hpp) ----
    // Door toward the square (the house's door is in its +z wall)
    const V toSquare = SQUARE - FORGE_AT;
    const float forgeYaw = std::atan2(toSquare.x, toSquare.z);
    auto forge = scn::Node::create("Smithy", FORGE_AT);
    forge->setLocalRotEuler({0.f, forgeYaw, 0.f});
    // Building space (x right, y up, +z out of the door) -> world
    const glm::mat4 forgeToWorld = forge->localTransform();
    auto inForge = [&](V p) { return V(forgeToWorld * glm::vec4(p, 1.f)); };
    auto forgeProp = [&](const char *name, const char *model, V pos) {
        auto node = scn::Node::create(name, pos);
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::loadModel(std::string(model) + ".glb")),
            lit);
        node->add_component<component::Shadow>();
        forge->add_child(node);
        return node;
    };
    forgeProp("House", "HPF_House", V(0.f));
    // The doors swing out as the camera comes up to them, hinged on the
    // wall's outer face
    for (auto [name, model, x, angle] :
         {std::tuple{"Door_L", "HPF_Door_L", forge::doorX0, -80.f},
          std::tuple{"Door_R", "HPF_Door_R", forge::doorX1, 80.f}}) {
        auto door = forgeProp(name, model, {x, 0.f, forge::doorOuter});
        const V shut(0.f), open(0.f, glm::radians(angle), 0.f);
        door->add_component<component::Animator>()->addTrack(
            component::AnimTarget::Rotation,
            Curve::flat({{0.f, shut},
                         {CUT - 5.f, shut},
                         {CUT - 2.5f, open},
                         {CUT, open}}),
            component::PlayMode::Single);
    }
    {
        // Behind the doors it's dark: a black card a little inside, it fills
        // the view as the camera flies in, then the demo cuts to the inside
        auto black =
            std::make_shared<render::Material>("Darkness", lit->getShader());
        const unsigned char pixel[4] = {0, 0, 0, 255};
        black->setTexture(render::TextureSlot::Albedo,
                          std::make_shared<render::Texture>(render::Texture{
                              loader::createTexture(pixel, 1, 1), "black"}));
        black->specular = V(0.f);
        auto card = scn::Node::create("Darkness", {DOOR_MID, forge::doorTop / 2,
                                                   forge::doorInner - 0.4f});
        // The rect lies in XZ facing +Y, stand it up to face out of the door
        card->setLocalRotEuler({glm::half_pi<float>(), 0.f, 0.f});
        card->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::createRect({(forge::doorX1 - forge::doorX0) / 2 + 0.3f,
                                    forge::doorTop / 2 + 0.2f})),
            black);
        forge->add_child(card);
    }
    cityNode->add_child(forge);

    {
        // A shadow-only floor 5 m under the city's ground. The terrain is an
        // open surface, only drawn into the sun's map seen from above, so
        // once the sun went under the horizon its light came up through the
        // ground onto the houses. This one faces down and is open too: drawn
        // into the map only while the sun shines on it from below, so it
        // never shadows anything by day. Not drawn itself (its Mesh is off,
        // the Shadow still uses it), so it can reach past the island.
        const V middle = (vec(city::cityMin) + vec(city::cityMax)) * 0.5f;
        auto node =
            scn::Node::create("Shadow floor", {middle.x, -7.5f, middle.z});
        node->setLocalRotEuler({glm::pi<float>(), 0.f, 0.f}); // face down
        // Only its double sided flag is read, by the shadow pass
        auto black = std::make_shared<render::Material>("Shadow floor",
                                                        lit->getShader());
        node->add_component<component::Mesh>(
                std::make_shared<render::Model>(
                    loader::createRect({600.f, 600.f})),
                black)
            ->enabled = false;
        node->add_component<component::Shadow>()->open = true;
        cityNode->add_child(node);
    }
    scene.root->add_child(cityNode);
    const V center = (vec(city::cityMin) + vec(city::cityMax)) * 0.5f;
    const float radius =
        glm::length(vec(city::cityMax) - vec(city::cityMin)) * 0.5f;

    auto sky = scn::Node::create("Skybox");
    auto skybox = sky->add_component<component::Skybox>("skybox.png");
    scene.skybox = skybox;
    scene.root->add_child(sky);

    // ---- Day and night: the sun turns once around the city per DAY ----
    // Pitch 0 is sunrise, -90 noon, -180 sunset, below that the sun shines
    // up from under the ground, so it is dark then. The pitch turns it inside
    // the Sun path node, which tips that circle off the vertical.
    using component::AnimTarget;
    constexpr float DAY = 120.f; // seconds
    // Not played: the demo's clock below moves them through the day
    std::shared_ptr<component::Animator> sunAnimator, moonClock;
    std::shared_ptr<component::Light> sunLight;
    const float tilt = glm::radians(35.f);
    const float yaw = glm::radians(70.f);
    // (fraction of the day, value) -> keys in seconds
    auto day = [&](std::vector<std::pair<float, V>> points) {
        for (auto &point : points)
            point.first *= DAY;
        return points;
    };
    {
        auto path = scn::Node::create("Sun path", center);
        path->setLocalRot(glm::angleAxis(yaw, V(0.f, 1.f, 0.f)) *
                          glm::angleAxis(tilt, V(0.f, 0.f, 1.f)));
        scene.root->add_child(path);
        auto node = scn::Node::create("Sun");
        auto sun =
            component::Light::directional({1.f, 0.9f, 0.75f}, 1.5f, 0.2f);
        // Moves all the time, so its map is drawn every frame
        sun.castsShadow = true;
        sun.shadowExtent = radius / 1.65f + 20.f;
        sun.shadowNear = 0.f;
        sun.shadowFar = 2.f * radius + 100.f;
        sunLight = node->add_component<component::Light>(sun);
        path->add_child(node);

        auto animator = node->add_component<component::Animator>();
        // Evenly spaced points on a line: Catmull-Rom keeps the speed even,
        // and -360 deg is the same as 0, so the loop doesn't jump
        std::vector<V> pitch;
        for (float deg : {0.f, -90.f, -180.f, -270.f, -360.f})
            pitch.push_back({glm::radians(deg), 0.f, 0.f});
        animator->addTrack(AnimTarget::Rotation,
                           component::Curve<V>::smoothEven(pitch, DAY));
        // Orange low over the horizon, white at noon, gone at night. Flat
        // keys so it never overshoots below black.
        const V low{1.f, 0.45f, 0.2f}, mid{1.f, 0.8f, 0.6f},
            noon{1.f, 0.95f, 0.85f}, none{0.f};
        animator->addTrack(AnimTarget::Color,
                           component::Curve<V>::flat(day({{0.f, low},
                                                          {0.08f, mid},
                                                          {0.25f, noon},
                                                          {0.42f, mid},
                                                          {0.5f, low},
                                                          {0.56f, none},
                                                          {0.94f, none},
                                                          {1.f, low}})));
        // The sky follows
        const V dusk{1.f, 0.6f, 0.45f}, night{0.06f, 0.08f, 0.15f};
        animator->addTrack(skybox->tint,
                           component::Curve<V>::flat(day({{0.f, dusk},
                                                          {0.1f, V(1.f)},
                                                          {0.4f, V(1.f)},
                                                          {0.5f, dusk},
                                                          {0.58f, night},
                                                          {0.92f, night},
                                                          {1.f, dusk}})));
        sunAnimator = animator;

        // Moon: opposite the sun, so it is up while the sun is down. Dim and
        // bluish, no shadow; its ambient keeps the night from going black.
        auto moonNode = scn::Node::create("Moon");
        moonNode->setLocalRotEuler({glm::pi<float>(), 0.f, 0.f});
        node->add_child(moonNode);
        moonNode->add_component<component::Light>(
            component::Light::directional(V(0.f), 1.f, 0.15f));
        const V moon{0.16f, 0.19f, 0.3f};
        auto moonAnimator = moonNode->add_component<component::Animator>();
        moonAnimator->addTrack(AnimTarget::Color,
                               component::Curve<V>::flat(day({{0.f, none},
                                                              {0.5f, none},
                                                              {0.58f, moon},
                                                              {0.92f, moon},
                                                              {1.f, none}})));
        moonClock = moonAnimator;
    }

    // ---- Camera: the flybys and the timelapse on one clock ----
    auto camNode = scn::Node::create("Camera");
    // Near plane not too close: the ground decals sit only 2-7 cm above
    // each other, a 24 bit depth buffer needs the precision far away
    auto camera =
        camNode->add_component<component::Camera>(50.f, 1.f, 1.f, 0.5f, 800.f);
    scene.activeCamera = camera;
    auto clock = camNode->add_component<component::Animator>();
    {
        // The day's takes one after another, a cut between each two
        Curve posDay, lookDay;
        float at = 0.f;
        for (const Take &take : DAY_TAKES) {
            const Flight flown = fly(take, at);
            posDay = then(posDay, flown.pos);
            lookDay = then(lookDay, flown.look);
            at = flown.end;
        }
        // The timelapse: high above the city, looking down on it like on a
        // model (the tilt-shift blur is a post-processing pass), a slow
        // drift while the day goes by. The sun crosses the sky along its
        // path's z axis, (0.94, 0.34) on the ground: the camera looks along
        // that and drifts across it, (0.34, -0.94), so the shadows don't
        // sweep along with the camera.
        const Flight lapse = flight({
            {LAPSE_AT, {-200.6f, 120.f, -87.6f}, {-22.f, -2.f, -23.f}},
            {NIGHT_AT, {-166.6f, 120.f, -181.6f}, {12.f, -2.f, -117.f}},
        });
        // The night take, then into the smithy: at the end of the market
        // it turns to the doorway, comes onto the door's axis over the
        // stalls in front of it (2.9 m tall, 14 to 21 m out), sinks to eye
        // height and pushes in slowly while the doors open. These points are
        // in the smithy's own space.
        const V doorway = inForge({DOOR_MID, 1.4f, forge::doorInner});
        Take nightTake = NIGHT_TAKE;
        nightTake.ways.back().look = doorway;
        const Flight night =
            fly(nightTake, NIGHT_AT,
                {{CUT - 5.f, inForge({DOOR_MID, 4.f, 13.f}), doorway},
                 {CUT - 2.5f, inForge({DOOR_MID, 2.f, 7.f}), doorway},
                 {CUT, inForge({DOOR_MID, 1.65f, forge::doorOuter + 0.15f}),
                  inForge({DOOR_MID, 1.6f, forge::doorInner - 2.f})}});
        clock->addTrack(component::AnimTarget::Position,
                        then(then(posDay, lapse.pos), night.pos),
                        component::PlayMode::Single);
        // After the position: turned to face what it looks at
        auto &look =
            clock->addTrack(component::AnimTarget::Property,
                            then(then(lookDay, lapse.look), night.look),
                            component::PlayMode::Single);
        look.name = "look at";
        look.setProperty = [node = camNode.get()](const V &target) {
            node->setLocalRot(glm::quatLookAt(
                glm::normalize(target - node->localPos()), V{0.f, 1.f, 0.f}));
        };
        camNode->setLocalPos(posDay.keys.front().value);
        look.setProperty(lookDay.keys.front().value);
    }
    {
        // Narrow view for the timelapse's miniature look, normal for the
        // flybys
        auto &track = clock->addTrack(component::AnimTarget::Property,
                                      Curve::flat({{0.f, V(50.f)},
                                                   {LAPSE_AT, V(50.f)},
                                                   {LAPSE_AT, V(30.f)},
                                                   {NIGHT_AT, V(30.f)},
                                                   {NIGHT_AT, V(50.f)},
                                                   {CUT, V(50.f)}}),
                                      component::PlayMode::Single);
        track.name = "fov";
        track.setProperty = [c = camera.get()](const V &v) { c->setFov(v.x); };
    }
    {
        // Time of day, seconds into the sun's DAY: the sun stands still in
        // the late morning for the day flyby, the timelapse runs at an even
        // speed from there to the middle of the night, where it stands
        // still again. Keys with their speed (day seconds per second), equal
        // at both ends of the timelapse, so it's a straight line.
        const float morning = 0.2f * DAY, midnight = 0.75f * DAY;
        const float speed = (midnight - morning) / LAPSE;
        Curve dayTime;
        dayTime.keys = {{0.f, V(morning), V(0.f), V(0.f)},
                        {LAPSE_AT, V(morning), V(0.f), V(0.f)},
                        {LAPSE_AT, V(morning), V(speed), V(speed)},
                        {NIGHT_AT, V(midnight), V(speed), V(speed)},
                        {NIGHT_AT, V(midnight), V(0.f), V(0.f)},
                        {CUT, V(midnight), V(0.f), V(0.f)}};
        auto &track = clock->addTrack(component::AnimTarget::Property, dayTime,
                                      component::PlayMode::Single);
        track.name = "time of day";
        track.setProperty = [sun = std::weak_ptr(sunAnimator),
                             moon = std::weak_ptr(moonClock)](const V &v) {
            for (auto &w : {sun, moon})
                if (auto a = w.lock())
                    a->seek(v.x);
        };
    }
    {
        // While the sun stands still (the flybys) its map is baked, drawn
        // once. One box over everything: the flyby goes around all of it.
        auto &track = clock->addTrack(component::AnimTarget::Property,
                                      Curve::flat({{0.f, V(1.f)},
                                                   {LAPSE_AT, V(1.f)},
                                                   {LAPSE_AT, V(0.f)},
                                                   {NIGHT_AT, V(0.f)},
                                                   {NIGHT_AT, V(1.f)},
                                                   {CUT, V(1.f)}}),
                                      component::PlayMode::Single);
        track.name = "sun baked";
        track.setProperty = [light = std::weak_ptr(sunLight)](const V &v) {
            auto l = light.lock();
            const bool baked = v.x > 0.5f;
            if (!l || l->bakedShadow == baked)
                return; // only on the switch
            l->bakedShadow = baked;
            l->shadowDirty = true;
        };
    }
    {
        // In from black, through black between the shots (the camera jumps
        // while it's dark): a short dip between the day's takes, a longer
        // one into and out of the timelapse. Before the smithy: fades to
        // black from just before the doors open, over the front of the
        // smithy (the doorway only fills the view in the last second), its
        // inside fades in from it.
        constexpr float dip = 1.5f, cut = 0.5f;
        std::vector<Curve::Point> fades = {{0.f, V(0.f)}, {dip, V(1.f)}};
        auto fade = [&](float time, float value) {
            fades.push_back({time, V(value)});
        };
        for (float at : dayCuts) {
            fade(at - cut, 1.f);
            fade(at, 0.f);
            fade(at + cut, 1.f);
        }
        fade(LAPSE_AT - dip, 1.f);
        fade(LAPSE_AT, 0.f);
        fade(LAPSE_AT + dip, 1.f);
        fade(NIGHT_AT - dip, 1.f);
        fade(NIGHT_AT, 0.f);
        fade(NIGHT_AT + dip, 1.f);
        fade(CUT - 1.2f, 1.f);
        fade(CUT, 0.f);
        auto &track =
            clock->addTrack(component::AnimTarget::Property, Curve::flat(fades),
                            component::PlayMode::Single);
        track.name = "fade";
        track.setProperty = [fade = scene.fade](const V &v) { *fade = v.x; };
    }
    {
        // Through the door: on to shot 4
        auto &track = clock->addTrack(
            component::AnimTarget::Property,
            Curve::flat({{0.f, V(0.f)}, {CUT, V(0.f)}, {CUT, V(1.f)}}),
            component::PlayMode::Single);
        track.name = "cut to shot 4";
        track.setProperty = [cut = scene.cutTo](const V &v) {
            if (v.x > 0.5f && cut->empty())
                *cut = INTERIOR;
        };
    }
    scene.root->add_child(camNode);

    // ---- Chimney smoke, at night. Drawn last: it's blended ----
    std::vector<std::weak_ptr<component::ParticleSystem>> emitters;
    {
        auto smoke = scn::Node::create("Smoke");
        auto material = std::make_shared<render::Material>(
            "Smoke",
            std::make_shared<render::Shader>("particle.vs", "particle.fs"));
        auto spawn =
            std::make_shared<render::ComputeShader>("spawnParticle.comp");
        auto update =
            std::make_shared<render::ComputeShader>("updateParticle.comp");
        // About half the houses have a fire going, the smithy always
        std::vector<V> chimneys;
        std::mt19937 pick(11);
        for (const auto &top : city::chimneys)
            if (std::bernoulli_distribution(0.3)(pick))
                chimneys.push_back(vec(top));
        chimneys.push_back(inForge(vec(forge::chimneyTop)));
        // Every house's fire its own: how much smoke, how dark, how fast it
        // rises, how big the puffs, how far the wind takes them
        std::mt19937 random(7);
        auto between = [&](float lo, float hi) {
            return std::uniform_real_distribution<float>(lo, hi)(random);
        };
        for (const V &top : chimneys) {
            auto node = scn::Node::create("Chimney smoke", top);
            auto puffs = node->add_component<component::ParticleSystem>(
                material, spawn, update);
            puffs->seed = (uint32_t)emitters.size() + 1;
            const float rise = between(0.6f, 1.5f);
            const float shade = between(0.75f, 1.25f);
            const float drift = between(0.5f, 1.6f);
            const float life = between(0.7f, 1.3f);
            // Grey, moonlit, thinning out as it rises and drifts with the
            // wind (the trees' and the water's)
            puffs->additive = false;
            puffs->minColor = {0.3f * shade, 0.32f * shade, 0.38f * shade,
                               between(0.2f, 0.45f)};
            puffs->maxColor = {0.2f * shade, 0.22f * shade, 0.27f * shade, 0.f};
            puffs->minPosition = V(-0.15f, 0.f, -0.15f);
            puffs->maxPosition = V(0.15f, 0.1f, 0.15f);
            puffs->minVelocity = {-0.12f, 0.6f * rise, -0.12f};
            puffs->maxVelocity = {0.15f, 1.2f * rise, 0.12f};
            puffs->minAccel = V(0.08f, -0.03f, 0.f) * drift;
            puffs->maxAccel = V(0.2f, 0.02f, 0.06f) * drift;
            puffs->minLife = 4.f * life;
            puffs->maxLife = 16.f * life;
            puffs->size = between(0.4f, 0.75f);
            puffs->stretch = 0.f;
            puffs->spawnInterval = between(0.04f, 0.12f);
            // Enough for every puff alive at once
            puffs->maxParticles =
                (uint32_t)std::ceil(puffs->maxLife / puffs->spawnInterval) + 16;
            puffs->spawning = false;
            emitters.push_back(puffs);
            smoke->add_child(node);
        }
        scene.root->add_child(smoke);
    }

    // ---- Dusk: the lamps, lanterns and fires come on while the sun goes
    // down in the timelapse (day fraction 0.5 is sunset), each at its own
    // random moment over a few seconds, so not all in one frame ----
    {
        const float from = LAPSE_AT + LAPSE * 0.4f,
                    to = LAPSE_AT + LAPSE * 0.7f;
        // One random moment in [0, 1) of that time for every lamp and fire
        std::mt19937 order(5);
        std::uniform_real_distribution<float> moment(0.f, 1.f);
        struct Lamp {
            std::weak_ptr<component::Light> light;
            float intensity, at;
        };
        struct Fire {
            std::weak_ptr<component::ParticleSystem> smoke;
            float at;
        };
        std::vector<Lamp> lamping;
        std::vector<Fire> fires;
        for (const auto &[light, intensity] : lamps)
            lamping.push_back({light, intensity, moment(order)});
        for (const auto &smoke : emitters)
            fires.push_back({smoke, moment(order)});
        // A lamp fades in over this much of it
        constexpr float FADE = 0.04f;
        auto &track = clock->addTrack(
            component::AnimTarget::Property,
            Curve::flat(
                {{0.f, V(0.f)}, {from, V(0.f)}, {to, V(1.f)}, {CUT, V(1.f)}}),
            component::PlayMode::Single);
        track.name = "dusk";
        track.setProperty = [lamping, fires, glowing](const V &v) {
            const float now = glm::clamp(v.x, 0.f, 1.f) * (1.f + FADE);
            for (const Lamp &lamp : lamping)
                if (auto l = lamp.light.lock()) {
                    const float lit =
                        glm::clamp((now - lamp.at) / FADE, 0.f, 1.f);
                    // Shadow casters stay on (at nothing): their maps are
                    // baked once at the start, not each time one comes on
                    // (that regrows the shadow array and redraws them all)
                    l->enabled = lit > 0.f || l->castsShadow;
                    l->intensity = lamp.intensity * lit;
                }
            for (const Fire &fire : fires)
                if (auto e = fire.smoke.lock())
                    e->spawning = now >= fire.at;
            for (const auto &m : glowing)
                m->setFloat("uEmissiveStrength", v.x);
        };
    }

    // The clock, the doors
    for (auto &animator : scene.getAllComponents<component::Animator>())
        if (animator != sunAnimator && animator != moonClock)
            animator->play();
    // Where the clock starts
    clock->seek(0.f);

    return scene;
}

} // namespace scenes
