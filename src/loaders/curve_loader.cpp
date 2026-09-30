#include "curve_loader.hpp"

#include <cmrc/cmrc.hpp>
#include <cstring>
#include <iostream>

CMRC_DECLARE(curves);

namespace loader {

component::Bezier<glm::vec3> loadCurve(const std::string &filename) {
    component::Bezier<glm::vec3> curve;

    auto fs = cmrc::curves::get_filesystem();
    if (!fs.exists(filename)) {
        std::cerr << "Embedded curve not found: " << filename << std::endl;
        return curve;
    }
    auto file = fs.open(filename);

    // Tightly packed little-endian float32 x y z per point, no header
    std::size_t count = file.size() / sizeof(glm::vec3);
    curve.control.resize(count);
    std::memcpy(curve.control.data(), file.begin(), count * sizeof(glm::vec3));

    if (curve.getSegmentCount() == 0)
        std::cerr << "Curve " << filename << " has too few points" << std::endl;
    return curve;
}

} // namespace loader
