#include "curve_loader.hpp"

#include <cmrc/cmrc.hpp>
#include <cstring>
#include <iostream>
#include <vector>

CMRC_DECLARE(curves);

namespace loader {

component::Curve<glm::vec3> loadCurve(const std::string &filename,
                                      float duration) {
    auto fs = cmrc::curves::get_filesystem();
    if (!fs.exists(filename)) {
        std::cerr << "Embedded curve not found: " << filename << std::endl;
        return {};
    }
    auto file = fs.open(filename);

    // Tightly packed little-endian float32 x y z per point, no header
    std::size_t count = file.size() / sizeof(glm::vec3);
    std::vector<glm::vec3> control(count);
    std::memcpy(control.data(), file.begin(), count * sizeof(glm::vec3));

    auto curve = component::Curve<glm::vec3>::fromBezier(control, duration);
    if (curve.empty())
        std::cerr << "Curve " << filename << " has too few points" << std::endl;
    return curve;
}

} // namespace loader
