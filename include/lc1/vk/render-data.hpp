#pragma once

#include "lc1/scene/lights/light.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace lc1 {

// Matches MaterialData in shaders/shader.slang; CPU image pointers never cross here.
struct MaterialData {
    glm::vec4 base_color_factor;
    glm::vec4 emissive_factor;
    glm::vec4 surface; // Metallic, perceptual roughness, normal scale, occlusion strength.
    glm::uvec4 flags;  // x bit 0: normal map present; other bits reserved.
};
static_assert(sizeof(MaterialData) == 64 && offsetof(MaterialData, emissive_factor) == 16 &&
              offsetof(MaterialData, surface) == 32 && offsetof(MaterialData, flags) == 48);

struct ToneMapData {
    glm::vec4 output; // Exposure, manual sRGB encoding (0/1), reserved, reserved.
};
static_assert(sizeof(ToneMapData) == 16);

// Matches Camera in shaders/shader.slang, field for field.
struct CameraData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 position; // World-space camera position; w is unused.
};

struct ObjectData {
    glm::mat4 model;
    glm::mat4 normal_transform; // Inverse transpose; padded to match shader matrices.
    glm::uvec4 shadow_identity; // x: surface ID; y: moving receiver rejects history.
};

static_assert(sizeof(CameraData) == 144 && offsetof(CameraData, position) == 128);
static_assert(sizeof(ObjectData) == 144 && offsetof(ObjectData, normal_transform) == 64);

constexpr auto max_num_lights = 10U;
struct LightsData {
    std::uint32_t count{};
    std::array<scene::Light, max_num_lights> lights;
};

} // namespace lc1
