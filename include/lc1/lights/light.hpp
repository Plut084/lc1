#pragma once

#include "lc1/lights/directional-light.hpp"
#include "lc1/lights/point-light.hpp"
#include "lc1/lights/spot-light.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lc1 {

// Unified GPU upload record. Matches Light in shaders/lights.slang in 16-byte rows
// within ConstantBuffer<LightsData>; recheck shader offsets/stride if layout changes.
struct alignas(16) Light {
    glm::vec3 position{0.0F};
    std::uint32_t type = 0; // 0 = directional, 1 = point, 2 = spot
    // Unit world-space propagation direction: directional rays / spot cone axis.
    // Callers must normalize before upload; to_light preserves the scene direction.
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
    float range = 0.0F;
    glm::vec3 color{1.0F};
    float intensity = 1.0F;
    // Half-angles in radians: x = inner, y = outer; 0 <= inner <= outer < pi/2.
    // Equal angles give a hard edge.
    glm::vec2 spot_angles{0.0F};
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
};

static_assert(std::is_standard_layout_v<Light> && std::is_trivially_copyable_v<Light>);
static_assert(sizeof(Light) == 64 && alignof(Light) == 16);
static_assert(offsetof(Light, position) == 0 && offsetof(Light, type) == 12);
static_assert(offsetof(Light, direction) == 16 && offsetof(Light, range) == 28);
static_assert(offsetof(Light, color) == 32 && offsetof(Light, intensity) == 44);
static_assert(offsetof(Light, spot_angles) == 48);
static_assert(offsetof(Light, padding0) == 56 && offsetof(Light, padding1) == 60);

inline Light to_light(DirectionalLight const &light)
{
    return {.type = 0,
            .direction = light.direction,
            .color = light.base.color,
            .intensity = light.base.intensity};
}

inline Light to_light(PointLight const &light)
{
    return {.position = light.position,
            .type = 1,
            .range = light.range,
            .color = light.base.color,
            .intensity = light.base.intensity};
}

inline Light to_light(SpotLight const &light)
{
    return {.position = light.position,
            .type = 2,
            .direction = light.direction,
            .range = light.range,
            .color = light.base.color,
            .intensity = light.base.intensity,
            .spot_angles = {light.inner_angle, light.outer_angle}};
}

} // namespace lc1
