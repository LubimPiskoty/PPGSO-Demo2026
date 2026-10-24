#pragma once

// Internal to the scene loader: JSON read helpers for the deserializer.
// Use ../scene_loader.hpp from outside.

#include "../../render/material.hpp"
#include "../../util/guid.hpp"
#include <cJSON.h>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

// V3: shaders store whether they're instanced, and UserMovement, Instances
// and Animator components are stored. V2 files still load, the missing
// fields fall back to defaults.
// V3.1: Instances can be a generator recipe instead of transforms
// V3.2: PhysicsBody components and scene physics settings
// V3.3: Light, Shadow, Skybox and Spin components
#define SCENE_FORMAT_VERSION 3.3

namespace loader {

inline const char *textureSlotNames[(int)render::TextureSlot::Count] = {
    "albedo",
    "normal",
    "specular",
};

// Indexed by PlayMode / AnimTarget
inline const char *playModeNames[] = {"single", "loop", "pingpong"};
inline const char *animTargetNames[] = {"position", "rotation", "scale",
                                          "color"};
// Indexed by LightType
inline const char *lightTypeNames[] = {"directional", "point", "spot"};

// Read helpers: return the fallback when the key is missing or has the wrong
// type, so older files and hand-edited ones still load
float readNumber(const cJSON *json, const char *key, float fallback);
bool readBool(const cJSON *json, const char *key, bool fallback);
std::string readString(const cJSON *json, const char *key,
                       const std::string &fallback = "");
glm::vec3 toVec3(const cJSON *array, glm::vec3 fallback);
glm::vec3 readVec3(const cJSON *json, const char *key, glm::vec3 fallback);
glm::vec4 readVec4(const cJSON *json, const char *key, glm::vec4 fallback);
glm::vec2 readVec2(const cJSON *json, const char *key, glm::vec2 fallback);

// Null guid when missing or malformed
util::Guid readGuid(const cJSON *json, const char *key);

// Index of name in names, or fallback
template <std::size_t N>
int readEnum(const cJSON *json, const char *key, const char *(&names)[N],
             int fallback) {
    std::string value = readString(json, key);
    for (std::size_t i = 0; i < N; i++)
        if (value == names[i])
            return (int)i;
    return fallback;
}

} // namespace loader
