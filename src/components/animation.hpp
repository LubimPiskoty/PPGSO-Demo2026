#pragma once

#include "ecs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace component {

enum class PlayMode {
    Single,  // Play once and stop
    Loop,    // Repeat indefinitely
    PingPong // Play forward then backward
};

template <typename T> struct Bezier {
  public:
    std::vector<T> control;

    // Non-overlapping segments: control.size() = numSegments * 3 + 1
    int getSegmentCount() const {
        if (control.size() < 4)
            return 0;
        return ((int)control.size() - 1) / 3;
    }

    T eval(int segmentIndex, const float t) const {
        // For non-overlapping: segment i starts at control[i*3]
        int i = segmentIndex * 3;
        if (i + 3 >= (int)control.size())
            return control.back();

        const float a = std::pow(1 - t, 3);
        const float b = 3 * std::pow(1 - t, 2) * t;
        const float c = 3 * std::pow(1 - t, 1) * t * t;
        const float d = std::pow(t, 3);

        return a * control[i] + b * control[i + 1] + c * control[i + 2] +
               d * control[i + 3];
    }

    // Evaluate the whole curve at u in [0, 1], every segment takes the same
    // share of u. Needs getSegmentCount() > 0.
    T sample(float u) const {
        const int segmentCount = getSegmentCount();
        float position = std::clamp(u, 0.f, 1.f) * segmentCount;
        int segmentIndex = std::min((int)position, segmentCount - 1);
        return eval(segmentIndex, position - segmentIndex);
    }
};

// Playback state of one animation: time runs from 0 to duration and wraps
// according to the play mode
struct Playback {
    float time = 0.f;
    float duration = 5.f;
    float speed = 1.f;
    PlayMode mode = PlayMode::Loop;
    bool playing = false;
    bool forward = true;

    void advance(double dt) {
        if (!playing)
            return;

        time += dt * speed * (forward ? 1.f : -1.f);

        if (mode == PlayMode::Single) {
            if (time >= duration) {
                time = duration;
                playing = false;
            } else if (time < 0.f) {
                time = 0.f;
                playing = false;
            }
        } else if (mode == PlayMode::Loop) {
            // Reset to 0 explicitly to avoid fmod floating point errors
            if (time >= duration) {
                time = 0.f;
            } else if (time < 0.f) {
                time = duration - std::fmod(-time, duration);
            }
        } else if (mode == PlayMode::PingPong) {
            if (time >= duration) {
                time = duration;
                forward = false;
            } else if (time < 0.f) {
                time = 0.f;
                forward = true;
            }
        }
    }

    // Time mapped to [0, 1]
    float progress() const {
        return duration > 0.f ? std::clamp(time / duration, 0.f, 1.f) : 0.f;
    }
};

template <typename T>
class Animation : public component::ComponentBase<Animation<T>> {
    const Bezier<T> curve;
    T &property;
    Playback playback;

  public:
    Animation(const Bezier<T> &curve, T &property)
        : curve(curve), property(property) {}

    void update(double dt) override {
        if (!this->enabled || !playback.playing)
            return;

        playback.advance(dt);
        if (curve.getSegmentCount() > 0)
            property = curve.sample(playback.progress());
    }

    void play() {
        playback.playing = true;
    }

    void stop() {
        playback.playing = false;
    }

    bool isPlaying() const {
        return playback.playing;
    }

    void setMaxTime(float t) {
        playback.duration = t;
    }

    void setPlayMode(PlayMode mode) {
        playback.mode = mode;
    }

    void setPlaySpeed(float s) {
        playback.speed = s;
    }

    const char *type_name() const override {
        return "Animation";
    }
};
} // namespace component
