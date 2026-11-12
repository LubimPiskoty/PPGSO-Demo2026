#include "animator.hpp"

#include "../scene/scene.hpp"
#include "light.hpp"
#include <algorithm>
#include <cmath>

namespace component {

void Animator::update(float dt) {
    for (auto &track : tracks) {
        if (!track.playback.playing)
            continue;
        track.playback.advance(dt);
        apply(track);
    }
}

void Animator::apply(const AnimatorTrack &track) {
    auto owner = node.lock();
    if (!owner || track.curve.empty())
        return;

    glm::vec3 value = track.curve.sample(track.playback.time);
    switch (track.target) {
    case AnimTarget::Position:
        owner->setLocalPos(value);
        break;
    case AnimTarget::Rotation:
        owner->setLocalRotEuler(value);
        break;
    case AnimTarget::Scale:
        owner->setLocalScale(value);
        break;
    case AnimTarget::Color:
        if (auto light = owner->get_component<Light>())
            light->color = value;
        break;
    case AnimTarget::Property:
        if (track.setProperty)
            track.setProperty(value);
        break;
    }
}

AnimatorTrack &Animator::addTrack(AnimTarget target,
                                  const Curve<glm::vec3> &curve,
                                  PlayMode mode) {
    AnimatorTrack track;
    track.target = target;
    track.curve = curve;
    track.playback.duration = curve.getDuration();
    track.playback.mode = mode;
    tracks.push_back(track);
    return tracks.back();
}

AnimatorTrack &Animator::addTrack(float &property, const Curve<float> &curve,
                                  PlayMode mode) {
    // Tracks hold vec3 curves, a float lives in x
    Curve<glm::vec3> vec3Curve;
    for (const auto &key : curve.keys)
        vec3Curve.keys.push_back({key.time, glm::vec3(key.value),
                                  glm::vec3(key.in), glm::vec3(key.out)});
    auto &track = addTrack(AnimTarget::Property, vec3Curve, mode);
    track.setProperty = [&property](const glm::vec3 &v) { property = v.x; };
    return track;
}

AnimatorTrack &Animator::addTrack(glm::vec3 &property,
                                  const Curve<glm::vec3> &curve,
                                  PlayMode mode) {
    auto &track = addTrack(AnimTarget::Property, curve, mode);
    track.setProperty = [&property](const glm::vec3 &v) { property = v; };
    return track;
}

void Animator::seek(float time) {
    for (auto &track : tracks) {
        auto &pb = track.playback;
        if (pb.mode == PlayMode::Single)
            pb.time = std::clamp(time, 0.f, pb.duration);
        else
        {
            // Wrapped into [0, duration), before 0 too
            pb.time = 0.f;
            if (pb.duration > 0.f) {
                pb.time = std::fmod(time, pb.duration);
                if (pb.time < 0.f)
                    pb.time += pb.duration;
            }
        }
        apply(track);
    }
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
