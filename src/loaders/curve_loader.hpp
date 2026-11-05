#pragma once

#include "../components/animation.hpp"
#include <glm/glm.hpp>
#include <string>

namespace loader {
// filename is the embedded resource path, e.g. "B_zierCurve.bin" (written by
// tools/export_curves.py). The file has no times, its segments share the
// duration evenly. Returns an empty curve if it can't be read.
component::Curve<glm::vec3> loadCurve(const std::string &filename,
                                      float duration);
} // namespace loader
