#pragma once

#include "lc1/scene/lights/light-base.hpp"
#include "lc1/scene/lights/light.hpp"

#include <glm/glm.hpp>

namespace lc1::scene {

// Scene data only; independent of GPU buffer layouts and Vulkan resources.
struct DirectionalLight {
    LightBase base;
    // Unit world-space direction in which light travels (Y is up).
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
    // Linear RGB tint, nonnegative; white preserves the intensity's photometric value.
};

inline Light to_light(DirectionalLight const &light)
{
    return {.type = 0,
            .direction = light.direction,
            .color = light.base.color,
            .intensity = light.base.intensity};
}

} // namespace lc1::scene
