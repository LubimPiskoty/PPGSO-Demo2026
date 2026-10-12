#pragma once

#include "../scene/scene.hpp"
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace scenes {

// Owns the active scene and a list of named ways to build one (hand built
// test scenes, loader::loadScene, ...), so the scene can be swapped or
// rebuilt at runtime (e.g. from the debug UI).
class SceneManager {
  public:
    using Builder = std::function<scn::Scene()>;

    struct Entry {
        std::string name;
        Builder build;
    };

    void add(std::string name, Builder build);

    // Builds entry `index` right away and makes it the active scene
    void load(std::size_t index);

    // Deferred variants for the UI: applied by applyPending() at the start
    // of the next frame, so the scene isn't replaced mid update/draw
    void requestLoad(std::size_t index) { pending = index; }
    void requestReload() { pending = current; }
    void applyPending();

    // Framebuffer size, applied to the active camera of every loaded scene
    void setViewport(float width, float height);

    scn::Scene &scene() { return active; }
    const std::vector<Entry> &entries() const { return list; }
    std::size_t currentIndex() const { return current; }

  private:
    std::vector<Entry> list;
    scn::Scene active;
    std::size_t current = 0;
    std::optional<std::size_t> pending;
    float width = 1.f, height = 1.f;
};

} // namespace scenes
