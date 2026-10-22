#pragma once

#include "framebuffer.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace render {

// Pool of depth-only framebuffers, one per shadow casting light. Owned by the
// Scene, so the GL objects die with it.
class ShadowMaps {
  public:
    static constexpr size_t MAX_SHADOW_LIGHTS = 8;
    static constexpr uint16_t RESOLUTION = 1 << 11; // 2048
    // Frames the light count has to stay lower before maps are freed, so
    // toggling a light on/off doesn't reallocate textures every time
    static constexpr int SHRINK_DELAY = 120;

    // Owns GL objects: movable (scenes are returned by value), not copyable
    ShadowMaps() = default;
    ShadowMaps(const ShadowMaps &) = delete;
    ShadowMaps &operator=(const ShadowMaps &) = delete;
    ShadowMaps(ShadowMaps &&) = default;
    ShadowMaps &operator=(ShadowMaps &&) = default;

    // Call once per frame before rendering. Only touches GL when the
    // resolution changes or the number of maps has to grow/shrink.
    void sync(size_t count) {
        count = std::min(count, MAX_SHADOW_LIGHTS);

        while (maps.size() < count)
            maps.push_back(std::make_unique<Framebuffer>(RESOLUTION, RESOLUTION,
                                                         true, false));

        if (count < maps.size()) {
            if (++framesBelow >= SHRINK_DELAY) {
                maps.resize(count);
                framesBelow = 0;
            }
        } else {
            framesBelow = 0;
        }
    }

    Framebuffer &at(size_t i) { return *maps[i]; }
    const Framebuffer &at(size_t i) const { return *maps[i]; }
    size_t size() const { return maps.size(); }

  private:
    std::vector<std::unique_ptr<Framebuffer>> maps;
    int framesBelow = 0;
};

} // namespace render
