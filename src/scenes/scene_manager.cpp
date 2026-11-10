#include "scene_manager.hpp"

#include "../components/camera.hpp"
#include <iostream>
#include <utility>

namespace scenes {

void SceneManager::add(std::string name, Builder build) {
    list.push_back({std::move(name), std::move(build)});
}

void SceneManager::load(std::size_t index) {
    if (index >= list.size())
        return;

    // Physics speed is a viewer setting, keep it across reloads
    float timeScale = active.physics_context.timeScale;

    active = list[index].build();
    current = index;
    active.physics_context.timeScale = timeScale;

    if (auto camera = active.activeCamera.lock())
        camera->setSize(width, height);
}

void SceneManager::applyPending() {
    if (!pending && active.cutTo && !active.cutTo->empty()) {
        for (std::size_t i = 0; i < list.size(); i++)
            if (list[i].name == *active.cutTo)
                pending = i;
        if (!pending) {
            std::cerr << "[Warning] Cut to unknown scene '" << *active.cutTo
                      << "'" << std::endl;
            active.cutTo->clear();
        }
    }
    if (!pending)
        return;
    std::size_t index = *pending;
    pending.reset();
    load(index);
}

void SceneManager::setViewport(float w, float h) {
    width = w;
    height = h;
    if (auto camera = active.activeCamera.lock())
        camera->setSize(width, height);
}

} // namespace scenes
