#pragma once

#include "ecs.hpp"
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
};

template <typename T>
class Animation : public component::ComponentBase<Animation<T>> {
    const Bezier<T> curve;
    T &property;
    float time, max_time, play_speed;
    PlayMode play_mode;
    bool is_playing, forward;

  public:
    Animation(const Bezier<T> &curve, T &property)
        : curve(curve), time(0), max_time(5.f), play_mode(PlayMode::Loop),
          play_speed(1.f), property(property), is_playing(false),
          forward(true) {}

    void update(double dt) override {
        if (!this->enabled || !is_playing)
            return;

        time += dt * play_speed * (forward ? 1.f : -1.f);

        if (play_mode == PlayMode::Single) {
            if (time >= max_time) {
                time = max_time;
                is_playing = false;
            } else if (time < 0.f) {
                time = 0.f;
                is_playing = false;
            }
        } else if (play_mode == PlayMode::Loop) {
            // Reset to 0 explicitly to avoid fmod floating point errors
            if (time >= max_time) {
                time = 0.f;
            } else if (time < 0.f) {
                time = max_time - std::fmod(-time, max_time);
            }
        } else if (play_mode == PlayMode::PingPong) {
            if (time >= max_time) {
                time = max_time;
                forward = false;
            } else if (time < 0.f) {
                time = 0.f;
                forward = true;
            }
        }

        property = sample();
    }

    void play() {
        is_playing = true;
    }

    void stop() {
        is_playing = false;
    }

    bool isPlaying() const {
        return is_playing;
    }

    void setMaxTime(float t) {
        max_time = t;
    }

    void setPlayMode(PlayMode mode) {
        play_mode = mode;
    }

    void setPlaySpeed(float s) {
        play_speed = s;
    }

    const char *type_name() const override {
        return "Animation";
    }

  private:
    T sample() {
        const int segmentCount = curve.getSegmentCount();
        if (segmentCount <= 0)
            return property;

        // Map time [0, max_time] to segment range [0, segmentCount)
        float normalizedTime = std::max(0.f, std::min(1.f, time / max_time));
        float position = normalizedTime * segmentCount;

        // Which segment and local t parameter
        int segmentIndex = (int)position;

        // Clamp segment index, or wrap for Loop mode
        if (play_mode != PlayMode::Loop) {
            segmentIndex = std::max(0, std::min(segmentIndex, segmentCount - 1));
        } else {
            // For Loop mode: wrap segments so last segment connects to first
            if (segmentCount > 0)
                segmentIndex = segmentIndex % segmentCount;
        }

        // Recalculate t based on (possibly clamped) segment index
        float t = position - segmentIndex;
        t = std::max(0.f, std::min(1.f, t));

        return curve.eval(segmentIndex, t);
    }
};
} // namespace component
