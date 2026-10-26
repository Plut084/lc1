#pragma once

#include "lc1/scene/lights/light-base.hpp"
#include "lc1/scene/lights/light.hpp"

#include <cstdint>
#include <glm/glm.hpp>

namespace lc1::scene {

// Area-light approximation: evaluate lighting at the center and average shadow
// rays over a receiver-facing disk of this radius. This is not a sphere mesh.
struct SphereLight {
    LightBase base;
    glm::vec3 position{0.0F}; // Center, in world-space metres.
    // Unbounded inverse-square attenuation evaluated at the center.
    float radius = 1.0F; // Nonnegative; zero recovers a point light's hard shadow.
    std::uint32_t shadow_sample_count = 1; // Clamped to [1, 32] in the shader.
};

inline Light to_light(SphereLight const &light)
{
    return {.position = light.position,
            .type = 3,
            .radius = light.radius,
            .color = light.base.color,
            .intensity = light.base.intensity,
            .shadow_sample_count = light.shadow_sample_count};
}

} // namespace lc1::scene
