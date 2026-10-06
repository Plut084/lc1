#pragma once

#include "lc1/scene/pbr-material.hpp"

namespace lc1 {

class GpuTexture;
class Sampler;

struct MaterialTexture {
    // Non-owning: these resources must survive all GPU uses of the material.
    // Null texture uses a renderer-owned fallback; null sampler uses its default.
    GpuTexture const *texture = nullptr;
    Sampler const *sampler = nullptr;
};

// CPU-owned parameters. Renderer snapshots these during record(); texture and
// sampler borrows must remain alive until all submitted uses have completed.
struct MaterialInfo {
    scene::PbrParameters parameters{};
    MaterialTexture base_color{};
    MaterialTexture metallic_roughness{};
    MaterialTexture normal{};
    // Reserved until indirect illumination is implemented; nonempty values fail.
    MaterialTexture occlusion{};
    MaterialTexture emissive{};
};

// Validate CPU parameters and texture color spaces before recording draws.
void validate_material(MaterialInfo const &info);

} // namespace lc1
