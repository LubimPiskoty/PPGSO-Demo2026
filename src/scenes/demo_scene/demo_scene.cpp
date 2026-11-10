#include "demo_scene.hpp"
#include "city_layout.hpp"

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
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace scenes {

// Flat ground tiles and decals: one-sided quads
bool isFlat(const std::string &model) {
    for (const char *prefix :
         {"City_Ground", "Ground_", "Grass_1_Alpha", "Grass_2_Alpha"})
        if (model.rfind(prefix, 0) == 0)
            return true;
    return false;
}

// Leaf cards, seen from both sides
bool isFoliage(const std::string &model) {
    return model.rfind("Tree_", 0) == 0;
}

namespace {

using V = glm::vec3;
using Curve = component::Curve<V>;

// The demo's clock: the day flyby (low along the road and over the lake), a
// timelapse into the night seen from high above, the same flyby at night,
// then into the smithy's door, where the demo cuts to its own scene
// (scenes::smithyScene). The flyby's length comes from its path, see flyby().
constexpr float LAPSE = 14.f;   // seconds of timelapse between the flybys
constexpr float APPROACH = 7.f; // from the flyby's end into the door
// SceneManager name of shot 4, see main.cpp
constexpr const char *INTERIOR = "Smithy interior";

// The smithy stands where this house of the demo was, on the south-east side
// of the market square, its door facing the square
constexpr const char *REPLACED = "House_7_1";
const V FORGE_AT{25.7f, -2.45f, -48.9f};
const V SQUARE{2.f, -2.45f, -71.f};

// Euler (pitch, yaw, 0) that points a node's -Z along d
V lookEuler(V d) {
    d = glm::normalize(d);
    return {std::asin(d.y), std::atan2(-d.x, -d.z), 0.f};
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

// People that can hop: everybody standing or walking
bool isPerson(const std::string &model) {
    if (model.find("Sitting") != std::string::npos)
        return false;
    for (const char *prefix : {"Man_", "Woman_", "Armor_", "Warrior_",
                               "Archer_", "Farmer_", "General"})
        if (model.rfind(prefix, 0) == 0)
            return true;
    return false;
}

// Where smoke comes out of a house: the top of its chimney, in the model
bool chimney(const std::string &model, V &top) {
    if (model.rfind("House_1_", 0) == 0)
        top = {0.f, 7.7f, -1.3f};
    else if (model.rfind("House_2_", 0) == 0)
        top = {0.4f, 4.8f, -0.9f};
    else if (model.rfind("House_4_", 0) == 0)
        top = {0.4f, 8.2f, -0.95f};
    else
        return false;
    return true;
}

// Distance from p to the segment a-b, seen from above
float distance(V p, V a, V b) {
    p.y = a.y = b.y = 0.f;
    const V ab = b - a;
    const float t =
        glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.f, 1.f);
    return glm::length(p - (a + ab * t));
}

// (time, view direction) -> rotation curve, Catmull-Rom. The yaw stays
// continuous, so the camera never spins the long way round.
using Views = std::vector<std::pair<float, V>>;
Curve turns(const Views &views) {
    std::vector<Curve::Point> rotations;
    for (const auto &[time, direction] : views) {
        V r = lookEuler(direction);
        if (!rotations.empty()) {
            const float last = rotations.back().second.y;
            while (r.y - last > glm::pi<float>())
                r.y -= glm::two_pi<float>();
            while (r.y - last < -glm::pi<float>())
                r.y += glm::two_pi<float>();
        }
        rotations.push_back({time, r});
    }
    return Curve::smooth(rotations);
}

// Catmull-Rom through the shots' positions
Curve path(const std::vector<Shot> &shots) {
    std::vector<Curve::Point> positions;
    for (const Shot &shot : shots)
        positions.push_back({shot.time, shot.pos});
    return Curve::smooth(positions);
}

// Positions and view directions of the shots, each looking at its lookAt
std::pair<Curve, Curve> flight(const std::vector<Shot> &shots) {
    Views views;
    for (const Shot &shot : shots)
        views.push_back({shot.time, shot.lookAt - shot.pos});
    return {path(shots), turns(views)};
}

// View directions for flying along a path between two times: the camera
// looks from where it was a second ago to where it will be in three, a
// little down. Mostly ahead, so it turns into a bend before the path does
// instead of trailing behind it, and over four seconds of the path, so the
// path's small wiggles between its points don't sway it.
Views lookingAhead(const Curve &positions, float from, float to) {
    constexpr float step = 0.5f, back = 1.f, lead = 3.f;
    Views views;
    for (float time = from; time < to + step * 0.5f; time += step) {
        // At the path's ends: the direction it leaves / arrives with
        const float ahead = std::clamp(time + lead, from + back + lead,
                                       positions.getDuration());
        V direction =
            positions.sample(ahead) - positions.sample(ahead - back - lead);
        direction.y = 0.f;
        direction = glm::normalize(direction);
        direction.y = -0.15f;
        views.push_back({std::min(time, to), direction});
    }
    return views;
}

// a, then b: a hard cut where b starts
Curve then(Curve a, const Curve &b) {
    a.keys.insert(a.keys.end(), b.keys.begin(), b.keys.end());
    return a;
}

// One point of the flyby: where, and how fast the camera is there (m/s)
struct Way {
    V pos;
    float speed;
};

// The flyby: in over the east field and bridge, through the east gate to the
// market square, down the main street and out of the south gate, along the
// road through the south village, around the west of the island over the
// lake, past the ships to the port, over the north gate back to the square.
// Read off a top-down picture of the city, every point checked against a
// height map of everything in it (at least 1.5 m clear of whatever stands
// within 2.5 m), so move them with care: trees and towers are close. Turns
// are wide (the path never turns faster than about 45 deg/s): two points
// close together around a corner whip the camera around.
const std::vector<Way> FLYBY = {
    // East field
    {{222.f, 12.f, -62.f}, 5.f},
    {{170.f, 8.f, -28.f}, 25.f},
    // East village
    {{128.f, 6.f, -8.f}, 24.f},
    // East bridge
    {{98.f, 6.5f, -21.f}, 23.f},
    // East gate
    {{76.f, 10.5f, -31.f}, 12.f},
    {{52.f, 8.f, -44.f}, 12.f},
    // Past the smithy
    {{35.f, 6.5f, -56.f}, 11.f},
    // Market square, a wide turn south
    {{18.f, 5.5f, -61.f}, 7.f},
    {{5.f, 5.f, -53.f}, 7.f},
    // Main street
    {{2.f, 5.f, -36.f}, 10.f},
    {{4.f, 4.f, 5.f}, 10.f},
    {{6.f, 5.f, 35.f}, 10.f},
    // South gate
    {{8.f, 9.5f, 52.f}, 12.f},
    // South bridge
    {{8.f, 4.f, 70.f}, 12.f},
    // Junction, a wide turn west
    {{2.f, 5.f, 87.f}, 11.f},
    {{-14.f, 5.5f, 96.f}, 11.f},
    // South village
    {{-45.f, 6.f, 95.f}, 13.f},
    {{-95.f, 6.5f, 88.f}, 14.f},
    // Wheat
    {{-142.f, 9.f, 73.f}, 14.f},
    {{-160.f, 9.f, 53.f}, 13.f},
    {{-173.f, 6.5f, 38.f}, 13.f},
    // The river's west arm
    {{-193.f, 6.f, 27.f}, 14.f},
    // River mouth
    {{-214.f, 7.f, 8.f}, 16.f},
    // The lake west of the castle
    {{-225.f, 5.f, -50.f}, 18.f},
    // Ship
    {{-222.f, 4.f, -115.f}, 6.f},
    // Ship, north village
    {{-170.f, 6.5f, -195.f}, 30.f},
    {{-90.f, 5.f, -216.f}, 30.f},
    // The two ships ahead
    {{-35.f, 5.f, -210.f}, 20.f},
    // A wide turn into the port
    {{-3.f, 5.5f, -190.f}, 15.f},
    // Port
    {{6.f, 8.5f, -160.f}, 8.f},
    // North gate
    {{7.f, 10.5f, -127.f}, 11.f},
    // North street
    {{7.f, 6.f, -100.f}, 8.f},
    // Square, in front of the smithy
    {{23.3f, 6.55f, -77.1f}, 6.f},
};

// The flyby's path from a start time: times from the distances and speeds
// (lookAt isn't used, the camera looks along the path, see lookingAhead)
std::vector<Shot> flyby(float start) {
    std::vector<Shot> shots;
    float time = start;
    for (size_t i = 0; i < FLYBY.size(); i++) {
        if (i > 0)
            time += glm::length(FLYBY[i].pos - FLYBY[i - 1].pos) /
                    ((FLYBY[i].speed + FLYBY[i - 1].speed) * 0.5f);
        shots.push_back({time, FLYBY[i].pos, FLYBY[i].pos});
    }
    return shots;
}

} // namespace

scn::Scene demoScene() {
    scn::Scene scene;

    // The clock's marks, seconds: day flyby from 0, timelapse, night flyby,
    // the cut to the smithy's inside
    const float LAPSE_AT = flyby(0.f).back().time;
    const float NIGHT_AT = LAPSE_AT + LAPSE;
    const float CUT = NIGHT_AT + LAPSE_AT + APPROACH;

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

    // ---- The city: every copy of an object in one instanced draw ----
    auto cityNode = scn::Node::create("City");
    // cityNode->setLocalRot(glm::vec3(-glm::half_pi<float>(), 0, 0));
    std::map<std::string, std::vector<component::Instance>> copies;
    std::vector<const city::Placement *> boats; // on the water, drawn singly
    // What stands on a boat's deck (its crew, cargo) rides with it: index
    // into boats, or -1
    auto aboard = [&](const city::Placement &p) {
        for (size_t i = 0; i < boats.size(); i++) {
            const city::Placement &b = *boats[i];
            const V local = glm::inverse(glm::quat(V(b.rx, b.ry, b.rz))) *
                            (V(p.x, p.y, p.z) - V(b.x, b.y, b.z));
            // Inside the hull, seen from above (the ships are 13 m long)
            const bool small = std::string(b.model) == "Boat";
            if (std::abs(local.x) < (small ? 0.7f : 2.7f) &&
                std::abs(local.z) < (small ? 1.7f : 6.4f) && local.y > 0.f &&
                local.y < 9.f)
                return (int)i;
        }
        return -1;
    };
    std::vector<std::pair<int, const city::Placement *>> crew;
    std::vector<V> chimneys; // tops, where smoke comes out
    glm::vec3 lo(1e9f), hi(-1e9f);
    bool replaced = false;
    // Boats and ships ride the waves, each its own node. Not the wreck on
    // the rocks (it lies lower and on its side).
    for (const city::Placement &p : city::placements)
        if ((std::string(p.model) == "Boat" ||
             std::string(p.model).rfind("Ship_", 0) == 0) &&
            p.y > -5.f)
            boats.push_back(&p);
    for (const city::Placement &p : city::placements) {
        if (!replaced && std::string(p.model) == REPLACED &&
            std::abs(p.x - FORGE_AT.x) < 0.5f &&
            std::abs(p.z - FORGE_AT.z) < 0.5f) {
            replaced = true; // the smithy goes here instead
            continue;
        }
        const std::string model = p.model;
        if ((model == "Boat" || model.rfind("Ship_", 0) == 0) && p.y > -5.f)
            continue; // collected before this loop
        if (const int boat = aboard(p); boat >= 0 && !isFlat(model)) {
            crew.push_back({boat, &p});
            continue;
        }
        // Chimneys near the flyby smoke at night: an emitter each, so only
        // the ones the camera comes close to
        V top;
        if (chimney(model, top) && chimneys.size() < 24) {
            float away = 1e9f;
            for (size_t i = 1; i < FLYBY.size(); i++)
                away = std::min(away, distance({p.x, p.y, p.z},
                                               FLYBY[i - 1].pos, FLYBY[i].pos));
            if (away < 30.f)
                chimneys.push_back(V(p.x, p.y, p.z) +
                                   glm::quat(V(p.rx, p.ry, p.rz)) *
                                       (top * p.scale));
        }
        component::Instance instance;
        instance.position = {p.x, p.y, p.z};
        instance.rotation = {p.rx, p.ry, p.rz};
        instance.scale = glm::vec3(p.scale);
        if (p.mirrored)
            instance.scale.y = -p.scale; // flat, so only the normal flips
        copies[p.model].push_back(instance);
        lo = glm::min(lo, instance.position);
        hi = glm::max(hi, instance.position);
    }
    for (auto &[model, instances] : copies) {
        auto node = scn::Node::create(model);
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::loadModel("MC_" + model + ".glb")),
            isFoliage(model) ? foliage : litInstanced);
        node->add_component<component::Instances>(std::move(instances));
        // Everything casts, the ground too. Flat tiles are open surfaces:
        // nothing behind their front faces for the shadow pass to draw.
        node->add_component<component::Shadow>()->open = isFlat(model);
        cityNode->add_child(node);
    }
    std::vector<std::shared_ptr<scn::Node>> boatNodes;
    for (size_t i = 0; i < boats.size(); i++) {
        const city::Placement &p = *boats[i];
        auto node = scn::Node::create(p.model, {p.x, p.y, p.z});
        boatNodes.push_back(node);
        node->setLocalScale(V(p.scale));
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::loadModel(std::string("MC_") + p.model + ".glb")),
            lit);
        node->add_component<component::Shadow>();
        auto floating = node->add_component<Floating>();
        floating->restPos = {p.x, p.y, p.z};
        floating->restRot = glm::quat(V(p.rx, p.ry, p.rz));
        floating->phase = 1.7f * (float)i;
        // The small boats rock more and quicker than the ships
        if (std::string(p.model) == "Boat") {
            floating->roll = glm::radians(6.f);
            floating->rollPeriod = 2.f;
        }
        cityNode->add_child(node);
    }
    // The crews and cargo: children of their boat, where they stood on it
    for (const auto &[boat, placed] : crew) {
        const city::Placement &p = *placed, &b = *boats[boat];
        const glm::quat toBoat = glm::inverse(glm::quat(V(b.rx, b.ry, b.rz)));
        auto node = scn::Node::create(
            p.model, toBoat * (V(p.x, p.y, p.z) - V(b.x, b.y, b.z)) / b.scale);
        node->setLocalRot(toBoat * glm::quat(V(p.rx, p.ry, p.rz)));
        node->setLocalScale(V(p.scale / b.scale));
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::loadModel(std::string("MC_") + p.model + ".glb")),
            lit);
        node->add_component<component::Shadow>();
        boatNodes[boat]->add_child(node);
    }
    if (!replaced)
        std::cerr << "[Warning] The house the smithy replaces is gone from "
                     "city_layout.hpp"
                  << std::endl;
    // What the objects don't cover (the ground), where the demo has it
    for (const char *model : city::rest) {
        // Drawn by the water below instead
        if (std::string(model) == "Rest_Environment_Water")
            continue;
        auto node = scn::Node::create(model);
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::loadModel(std::string("MC_") + model + ".glb")),
            lit);
        // The leftover terrain is one open surface too (houses and people
        // pieces in the other Rest_ models are closed)
        node->add_component<component::Shadow>()->open =
            std::string(model) == "Rest_Environment";
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

    // ---- The smithy: the forge building of the ironheart pack ----
    // Door toward the square (the building's door is in its +z wall)
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
    forgeProp("Building", "SM_Forge_Building", V(0.f));
    forgeProp("Roof", "SM_Forge_Roof", V(0.f));
    // Only its chimney shows above the roof from outside
    forgeProp("Hood", "SM_Forge_Hood", {0.2f, 0.f, -2.4f});
    forgeProp("Shutters", "SM_Window_Shutters", {3.92f, 0.f, 0.f});
    // The doors swing in as the camera comes up to them
    for (auto [name, model, x, angle] :
         {std::tuple{"Door_L", "SM_Door_L", -1.1f, -80.f},
          std::tuple{"Door_R", "SM_Door_R", 1.1f, 80.f}}) {
        auto door = forgeProp(name, model, {x, 0.f, 3.045f});
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
        auto card = scn::Node::create("Darkness", {0.f, 1.3f, 2.6f});
        // The rect lies in XZ facing +Y, stand it up to face out of the door
        card->setLocalRotEuler({glm::half_pi<float>(), 0.f, 0.f});
        card->add_component<component::Mesh>(
            std::make_shared<render::Model>(loader::createRect({1.4f, 1.4f})),
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
        const V middle = (lo + hi) * 0.5f;
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
    const glm::vec3 center = (lo + hi) * 0.5f;
    const float radius = glm::length(hi - lo) * 0.5f;

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
            component::Light::directional({1.f, 0.9f, 0.75f}, 1.5f, 0.3f);
        // Moves all the time, so its map is drawn every frame
        sun.castsShadow = true;
        sun.shadowExtent = radius / 1.75f + 20.f;
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
            component::Light::directional(V(0.f), 1.f, 0.6f));
        const V moon{0.25f, 0.3f, 0.45f};
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
        const Curve posDay = path(flyby(0.f));
        const Curve rotDay = turns(lookingAhead(posDay, 0.f, LAPSE_AT));
        // The timelapse: high above the city, looking down on it like on a
        // model (the tilt-shift blur is a post-processing pass), a slow
        // drift while the day goes by. The sun crosses the sky along its
        // path's z axis, (0.94, 0.34) on the ground: the camera looks along
        // that and drifts across it, (0.34, -0.94), so the shadows don't
        // sweep along with the camera.
        const auto [posLapse, rotLapse] = flight({
            {LAPSE_AT, {-200.6f, 120.f, -87.6f}, {-22.f, -2.f, -23.f}},
            {NIGHT_AT, {-166.6f, 120.f, -181.6f}, {12.f, -2.f, -117.f}},
        });
        // The same flyby at night, then on between the market stalls (one
        // stands at -12, 24 in the smithy's space, one at -0.7, 15.7), low
        // over the ground beside the people in front of the smithy (they
        // stand at x -0.4 .. 0.4, 9 m and further out) and into the door.
        // These last points are in the smithy's own space: straight at its
        // door. The flyby ends at (-19, 9, 21) there.
        std::vector<Shot> night = flyby(NIGHT_AT);
        night.push_back({CUT - 4.5f, inForge({-3.5f, 1.9f, 12.f}),
                         inForge({0.f, 1.4f, 3.f})});
        night.push_back({CUT - 2.f, inForge({-0.3f, 1.7f, 6.f}),
                         inForge({0.f, 1.5f, 0.f})});
        night.push_back(
            {CUT, inForge({0.f, 1.65f, 3.2f}), inForge({0.f, 1.6f, 0.f})});
        const Curve posNight = path(night);
        // Along the path up to the flyby's end, from there at the door
        Views views = lookingAhead(posNight, NIGHT_AT, NIGHT_AT + LAPSE_AT);
        for (size_t i = FLYBY.size(); i < night.size(); i++)
            views.push_back({night[i].time, night[i].lookAt - night[i].pos});
        const Curve rotNight = turns(views);
        clock->addTrack(component::AnimTarget::Position,
                        then(then(posDay, posLapse), posNight),
                        component::PlayMode::Single);
        clock->addTrack(component::AnimTarget::Rotation,
                        then(then(rotDay, rotLapse), rotNight),
                        component::PlayMode::Single);
        camNode->setLocalPos(posDay.keys.front().value);
        camNode->setLocalRotEuler(rotDay.keys.front().value);
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
        // while it's dark). Before the smithy: fades to black from just
        // before the doors open, over the front of the smithy (the doorway
        // only fills the view in the last second), its inside fades in
        // from it.
        constexpr float dip = 0.8f;
        auto &track = clock->addTrack(component::AnimTarget::Property,
                                      Curve::flat({{0.f, V(0.f)},
                                                   {dip, V(1.f)},
                                                   {LAPSE_AT - dip, V(1.f)},
                                                   {LAPSE_AT, V(0.f)},
                                                   {LAPSE_AT + dip, V(1.f)},
                                                   {NIGHT_AT - dip, V(1.f)},
                                                   {NIGHT_AT, V(0.f)},
                                                   {NIGHT_AT + dip, V(1.f)},
                                                   {CUT - 5.f, V(1.f)},
                                                   {CUT - 1.f, V(0.f)},
                                                   {CUT, V(0.f)}}),
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
    {
        auto smoke = scn::Node::create("Smoke");
        auto material = std::make_shared<render::Material>(
            "Smoke",
            std::make_shared<render::Shader>("particle.vs", "particle.fs"));
        auto spawn =
            std::make_shared<render::ComputeShader>("spawnParticle.comp");
        auto update =
            std::make_shared<render::ComputeShader>("updateParticle.comp");
        // The smithy's own chimney too (its hood's, in the building's space)
        chimneys.push_back(inForge({0.2f, 6.2f, -2.4f}));
        std::vector<std::weak_ptr<component::ParticleSystem>> emitters;
        for (const V &top : chimneys) {
            auto node = scn::Node::create("Chimney smoke", top);
            auto puffs = node->add_component<component::ParticleSystem>(
                material, spawn, update);
            // Grey, moonlit, thinning out as it rises and drifts with the
            // wind (the trees' and the water's)
            puffs->additive = false;
            puffs->minColor = {0.3f, 0.32f, 0.38f, 0.35f};
            puffs->maxColor = {0.2f, 0.22f, 0.27f, 0.f};
            puffs->minPosition = V(-0.15f, 0.f, -0.15f);
            puffs->maxPosition = V(0.15f, 0.1f, 0.15f);
            puffs->minVelocity = {-0.1f, 0.7f, -0.1f};
            puffs->maxVelocity = {0.15f, 1.3f, 0.1f};
            puffs->minAccel = {0.08f, -0.03f, 0.f};
            puffs->maxAccel = {0.2f, 0.02f, 0.06f};
            puffs->minLife = 4.f;
            puffs->maxLife = 16.f;
            puffs->size = 0.55f;
            puffs->stretch = 0.f;
            puffs->spawnInterval = 0.06f;
            puffs->maxParticles = 256;
            puffs->spawning = false;
            emitters.push_back(puffs);
            smoke->add_child(node);
        }
        scene.root->add_child(smoke);

        // The fires are lit as it gets dark, half way into the timelapse
        const float fires = LAPSE_AT + LAPSE * 0.5f;
        auto &track = clock->addTrack(component::AnimTarget::Property,
                                      Curve::flat({{0.f, V(0.f)},
                                                   {fires, V(0.f)},
                                                   {fires, V(1.f)},
                                                   {CUT, V(1.f)}}),
                                      component::PlayMode::Single);
        track.name = "chimney smoke";
        track.setProperty = [emitters](const V &v) {
            for (auto &emitter : emitters)
                if (auto e = emitter.lock())
                    e->spawning = v.x > 0.5f;
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
