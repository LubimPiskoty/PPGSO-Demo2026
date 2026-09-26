#pragma once

#include "../render/model.hpp"
#include <string>

namespace loader {
// filename is the embedded resource path, e.g. "SM_PROP_crate_02.glb"
render::Model loadModel(const std::string &filename);
} // namespace loader
