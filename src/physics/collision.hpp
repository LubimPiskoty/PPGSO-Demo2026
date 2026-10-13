#pragma once

#include "common.hpp"

namespace physics {

// Narrow phase test for one pair of bodies: returns true and fills `c` when
// they touch. Works for any shape combination and either argument order
// (plane vs plane never collides). Defined in collision.cpp
bool collide(const Body *a, const Body *b, Contact &c);

} // namespace physics
