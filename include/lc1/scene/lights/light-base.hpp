#pragma once

#include <glm/glm.hpp>

namespace lc1::scene {

struct LightBase {
    glm::vec3 color{1.0F};
    float intensity = 1.0F;
};

} // namespace lc1::scene
