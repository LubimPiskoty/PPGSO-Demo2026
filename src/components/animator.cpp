#include "animator.hpp"

#include "../scene/scene.hpp"

namespace component {

void Animator::update(float dt) {
    auto owner = node.lock();
    if (!owner)
        return;

    for (auto &track : tracks) {
        if (!track.playback.playing || track.curve.getSegmentCount() == 0)
            continue;

        track.playback.advance(dt);
        glm::vec3 value = track.curve.sample(track.playback.progress());
        switch (track.target) {
        case AnimTarget::Position:
            owner->localPos = value;
            break;
        case AnimTarget::Rotation:
            owner->localRotEuler = value;
            break;
        case AnimTarget::Scale:
            owner->localScale = value;
            break;
        }
    }
}

AnimatorTrack &Animator::addTrack(AnimTarget target, Bezier<glm::vec3> curve,
                                  float duration, PlayMode mode) {
    AnimatorTrack track;
    track.target = target;
    track.curve = curve;
    track.playback.duration = duration;
    track.playback.mode = mode;
    tracks.push_back(track);
    return tracks.back();
}

void Animator::play() {
    for (auto &track : tracks)
        track.playback.playing = true;
}

void Animator::stop() {
    for (auto &track : tracks)
        track.playback.playing = false;
}

} // namespace component
