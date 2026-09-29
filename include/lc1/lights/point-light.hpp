#pragma once

#include "lc1/lights/light-base.hpp"

#include <glm/glm.hpp>

namespace lc1 {

struct PointLight {
    LightBase base;
    glm::vec3 position{0.0F}; // World space, in metres.
    // Nonnegative cutoff distance in metres; zero means no finite cutoff.
    // Within this range, lighting should use inverse-square distance attenuation.
    float range = 0.0F;
};

} // namespace lc1
