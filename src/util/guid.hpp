#pragma once

#include <cstdint>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

namespace util {

// Random 64-bit id, stable for an object's lifetime. Used to reference
// objects (nodes, materials, ...) when serializing, since raw
// pointers/indices don't survive a save/load round-trip.
// A default-constructed Guid is null (0) and converts to false.
class Guid {
  public:
    Guid() = default;

    explicit Guid(std::uint64_t value) : value(value) {}

    static Guid generate() {
        static thread_local std::mt19937_64 rng{std::random_device{}()};
        // Skip 0 so a generated Guid is never null
        static thread_local std::uniform_int_distribution<std::uint64_t> dist(
            1);
        return Guid(dist(rng));
    }

    // Throws std::invalid_argument on malformed input
    static Guid fromString(const std::string &str) {
        std::uint64_t value;
        std::istringstream is(str);
        is >> std::hex >> value;
        if (is.fail())
            throw std::invalid_argument("invalid guid string: " + str);
        return Guid(value);
    }

    // 16 lowercase hex digits, zero-padded
    std::string toString() const {
        std::ostringstream os;
        os << std::hex << std::setw(16) << std::setfill('0') << value;
        return os.str();
    }

    std::uint64_t raw() const {
        return value;
    }

    explicit operator bool() const {
        return value != 0;
    }

    bool operator==(const Guid &other) const {
        return value == other.value;
    }

    bool operator!=(const Guid &other) const {
        return value != other.value;
    }

    bool operator<(const Guid &other) const {
        return value < other.value;
    }

  private:
    std::uint64_t value = 0;
};

} // namespace util

// Lets Guid be a key in std::unordered_map / std::unordered_set
template <> struct std::hash<util::Guid> {
    std::size_t operator()(const util::Guid &g) const noexcept {
        return std::hash<std::uint64_t>{}(g.raw());
    }
};
