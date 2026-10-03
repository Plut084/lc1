#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lc1::scene {

// Unified GPU upload record. Matches Light in shaders/lights.slang in 16-byte rows
// within ConstantBuffer<LightsData>; recheck shader offsets/stride if layout changes.
struct Light {
    glm::vec3 position{0.0F};
    std::uint32_t type = 0; // 0 = directional, 1 = point, 2 = spot, 3 = sphere
    // Unit world-space propagation direction: directional rays / spot cone axis.
    // Callers must normalize before upload; to_light preserves the scene direction.
    alignas(16) glm::vec3 direction{0.0F, -1.0F, 0.0F};
    float radius = 0.0F; // Sphere-light emitter radius, not an illumination cutoff.
    alignas(16) glm::vec3 color{1.0F};
    float intensity = 1.0F;
    // Half-angles in radians: x = inner, y = outer; 0 <= inner <= outer < pi/2.
    // Equal angles give a hard edge.
    alignas(8) glm::vec2 spot_angles{0.0F};
    std::uint32_t shadow_sample_count = 1;

    friend bool operator==(Light const &, Light const &) = default;
};

static_assert(std::is_standard_layout_v<Light> && std::is_trivially_copyable_v<Light>);
static_assert(sizeof(Light) == 64);
static_assert(offsetof(Light, position) == 0);
static_assert(offsetof(Light, type) == 12);
static_assert(offsetof(Light, direction) == 16);
static_assert(offsetof(Light, radius) == 28);
static_assert(offsetof(Light, color) == 32);
static_assert(offsetof(Light, intensity) == 44);
static_assert(offsetof(Light, spot_angles) == 48);
static_assert(offsetof(Light, shadow_sample_count) == 56);

} // namespace lc1::scene
