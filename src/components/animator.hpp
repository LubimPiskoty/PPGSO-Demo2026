#pragma once

#include "animation.hpp"
#include "ecs.hpp"
#include <deque>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace component {

// Which part of the node's local transform a track drives
// Color drives the colour of the node's Light, Property any value in the scene
enum class AnimTarget { Position, Rotation, Scale, Color, Property };

struct AnimatorTrack {
    AnimTarget target = AnimTarget::Position;
    Curve<glm::vec3> curve;
    Playback playback;
    std::function<void(const glm::vec3 &)> setProperty; // Property only
    // Shown in the Animation window instead of the target ("fade", "fov")
    std::string name;
};

// Animates the owner node's local position / rotation (Euler radians) /
// scale, its light's colour, or any float / vec3 by reference, along keyframe
// curves. Everything it drives shows up in the Animation window.
class Animator : public ComponentBase<Animator> {
  public:
    // A deque: the track addTrack returns stays valid when more are added
    std::deque<AnimatorTrack> tracks;

    void update(float dt) override;

    // Writes the track's value at its current time to the node, also used to
    // scrub a paused track
    void apply(const AnimatorTrack &track);

    // A track lasts until the last key of its curve. The returned track
    // stays valid as more are added.
    AnimatorTrack &addTrack(AnimTarget target, const Curve<glm::vec3> &curve,
                            PlayMode mode = PlayMode::Loop);
    // Animates a value directly. It must outlive this Animator.
    AnimatorTrack &addTrack(float &property, const Curve<float> &curve,
                            PlayMode mode = PlayMode::Loop);
    AnimatorTrack &addTrack(glm::vec3 &property, const Curve<glm::vec3> &curve,
                            PlayMode mode = PlayMode::Loop);

    // Start or stop every track
    void play();
    void stop();
    // Moves every track to the given time on the shared timeline
    void seek(float time);

    const char *type_name() const override { return "Animator"; }

    std::string info() const override {
        return std::to_string(tracks.size()) + " tracks";
    }
};

} // namespace component
