#pragma once

#include "ecs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

namespace component {

enum class PlayMode {
    Single,  // Play once and stop
    Loop,    // Repeat indefinitely
    PingPong // Play forward then backward
};

// One keyframe: the value at a time, and how fast it changes (per second)
// arriving at and leaving the key. in != out makes a sharp corner.
template <typename T> struct Key {
    float time = 0.f;
    T value{};
    T in{};
    T out{};
};

// Keyframes sorted by time, cubic between neighbours
template <typename T> struct Curve {
    using Point = std::pair<float, T>; // (time, value)

    std::vector<Key<T>> keys;

    bool empty() const { return keys.empty(); }

    float getDuration() const { return keys.empty() ? 0.f : keys.back().time; }

    // Value at an absolute time, holds the first / last key outside the
    // curve. Needs at least one key.
    T sample(float time) const {
        if (time <= keys.front().time)
            return keys.front().value;
        if (time >= keys.back().time)
            return keys.back().value;

        // First key after time. There is a key at or before time, so a and b
        // always differ in time, even with two keys on the same time (a step).
        auto next = std::upper_bound(
            keys.begin(), keys.end(), time,
            [](float t, const Key<T> &key) { return t < key.time; });
        const Key<T> &a = *(next - 1), &b = *next;
        const float dt = b.time - a.time;
        const float t = (time - a.time) / dt;

        // A speed times a third of the segment gives the Bezier handle
        const T p1 = a.value + a.out * (dt / 3.f);
        const T p2 = b.value - b.in * (dt / 3.f);

        const float u = 1.f - t;
        return (u * u * u) * a.value + (3.f * u * u * t) * p1 +
               (3.f * u * t * t) * p2 + (t * t * t) * b.value;
    }

    // Keys with zero speed: eases in and out of every key and never overshoots
    static Curve flat(const std::vector<Point> &points) {
        Curve curve;
        for (const auto &[time, value] : points)
            curve.keys.push_back({time, value, T{}, T{}});
        return curve;
    }

    // Catmull-Rom: passes through the points without stopping, the speed at a
    // key is the slope between its neighbours. Overshoots into a hold, use
    // flat() there.
    static Curve smooth(const std::vector<Point> &points) {
        Curve curve = flat(points);
        const int n = (int)points.size();
        for (int i = 0; i < n && n > 1; i++) {
            const Point &prev = points[std::max(i - 1, 0)];
            const Point &next = points[std::min(i + 1, n - 1)];
            const T speed =
                (next.second - prev.second) / (next.first - prev.first);
            curve.keys[i].in = curve.keys[i].out = speed;
        }
        return curve;
    }

    // smooth() with the values evenly spaced over the duration
    static Curve smoothEven(const std::vector<T> &values, float duration) {
        std::vector<Point> points;
        const int n = (int)values.size();
        for (int i = 0; i < n; i++)
            points.push_back({n > 1 ? duration * i / (n - 1) : 0.f, values[i]});
        return smooth(points);
    }

    // From Bezier control points, every segment gets the same share of the
    // duration: P0, P0 right handle, P1 left handle, P1, ... (3 * segments + 1
    // points, the layout of tools/export_curves.py). Empty if there are too
    // few.
    static Curve fromBezier(const std::vector<T> &control, float duration) {
        Curve curve;
        const int segments = ((int)control.size() - 1) / 3;
        if (segments < 1 || duration <= 0.f)
            return curve;

        const float dt = duration / segments;
        curve.keys.resize(segments + 1);
        for (int i = 0; i <= segments; i++) {
            Key<T> &key = curve.keys[i];
            key.time = dt * i;
            key.value = control[i * 3];
            if (i < segments)
                key.out = (control[i * 3 + 1] - key.value) * (3.f / dt);
            if (i > 0)
                key.in = (key.value - control[i * 3 - 1]) * (3.f / dt);
        }
        curve.keys.front().in = curve.keys.front().out;
        curve.keys.back().out = curve.keys.back().in;
        return curve;
    }
};

// Playback state of one animation: time runs from 0 to duration and wraps
// according to the play mode
struct Playback {
    float time = 0.f;
    float speed = 1.f;
    float duration = 1.f;
    PlayMode mode = PlayMode::Loop;
    bool playing = false;
    bool forward = true;

    void advance(double dt) {
        if (!playing)
            return;

        time += (float)dt * speed * (forward ? 1.f : -1.f);

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
};

template <typename T>
class Animation : public component::ComponentBase<Animation<T>> {
    const Curve<T> curve;
    T &property;
    Playback playback;

  public:
    Animation(const Curve<T> &curve, T &property)
        : curve(curve), property(property) {
        playback.duration = curve.getDuration();
    }

    void update(float dt) override {
        if (!this->enabled || !playback.playing)
            return;

        playback.advance(dt);
        if (!curve.empty())
            property = curve.sample(playback.time);
    }

    void play() { playback.playing = true; }

    void stop() { playback.playing = false; }

    bool isPlaying() const { return playback.playing; }

    void setPlayMode(PlayMode mode) { playback.mode = mode; }

    void setPlaySpeed(float s) { playback.speed = s; }

    const char *type_name() const override { return "Animation"; }
};
} // namespace component
