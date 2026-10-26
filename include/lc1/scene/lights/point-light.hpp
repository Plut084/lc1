#pragma once

#include "lc1/scene/lights/light-base.hpp"
#include "lc1/scene/lights/light.hpp"

#include <glm/glm.hpp>

namespace lc1::scene {

struct PointLight {
    LightBase base;
    glm::vec3 position{0.0F}; // World space, in metres.
    // Unbounded inverse-square distance attenuation.
};

inline Light to_light(PointLight const &light)
{
    return {.position = light.position,
            .type = 1,
            .color = light.base.color,
            .intensity = light.base.intensity};
}

} // namespace lc1::scene
