#include "json.hpp"

#include <stdexcept>

namespace loader {

float readNumber(const cJSON *json, const char *key, float fallback) {
    auto item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : fallback;
}

bool readBool(const cJSON *json, const char *key, bool fallback) {
    auto item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

std::string readString(const cJSON *json, const char *key,
                       const std::string &fallback) {
    auto item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}

glm::vec3 toVec3(const cJSON *array, glm::vec3 fallback) {
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != 3)
        return fallback;
    glm::vec3 v;
    for (int i = 0; i < 3; i++)
        v[i] = (float)cJSON_GetNumberValue(cJSON_GetArrayItem(array, i));
    return v;
}

glm::vec3 readVec3(const cJSON *json, const char *key, glm::vec3 fallback) {
    return toVec3(cJSON_GetObjectItemCaseSensitive(json, key), fallback);
}

glm::vec2 readVec2(const cJSON *json, const char *key, glm::vec2 fallback) {
    auto array = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != 2)
        return fallback;
    glm::vec2 v;
    for (int i = 0; i < 2; i++)
        v[i] = (float)cJSON_GetNumberValue(cJSON_GetArrayItem(array, i));
    return v;
}

util::Guid readGuid(const cJSON *json, const char *key) {
    try {
        return util::Guid::fromString(readString(json, key));
    } catch (const std::invalid_argument &) {
        return util::Guid();
    }
}

} // namespace loader
