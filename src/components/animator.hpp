#pragma once

#include "animation.hpp"
#include "ecs.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace component {

// Which part of the node's local transform a track drives
// Color drives the colour of the node's Light
enum class AnimTarget { Position, Rotation, Scale, Color };

struct AnimatorTrack {
    AnimTarget target = AnimTarget::Position;
    Bezier<glm::vec3> curve;
    Playback playback;
};

// Animates the owner node's local position / rotation (Euler radians) /
// scale, or its light's colour, along Bezier curves. Unlike Animation<T> it only targets the node
// transform, so it can be loaded with the scene.
class Animator : public ComponentBase<Animator> {
  public:
    std::vector<AnimatorTrack> tracks;

    void update(float dt) override;

    // Writes the track's value at its current time to the node, also used to
    // scrub a paused track
    void apply(const AnimatorTrack &track);

    AnimatorTrack &addTrack(AnimTarget target, Bezier<glm::vec3> curve,
                            float duration, PlayMode mode = PlayMode::Loop);

    // Start or stop every track
    void play();
    void stop();

    const char *type_name() const override {
        return "Animator";
    }

    std::string info() const override {
        return std::to_string(tracks.size()) + " tracks";
    }
};

} // namespace component
