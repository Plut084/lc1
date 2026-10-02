#pragma once

#include "lc1/scene/pbr-material.hpp"
#include "lc1/vk/descriptor-set.hpp"

#include <utility>

namespace lc1 {

class GpuTexture;
class Sampler;

struct MaterialTexture {
    // Non-owning: these resources must survive all GPU uses of the material.
    // Null texture uses a renderer-owned fallback; null sampler uses its default.
    GpuTexture const *texture = nullptr;
    Sampler const *sampler = nullptr;
};

struct MaterialInfo {
    scene::PbrParameters parameters{};
    MaterialTexture base_color{};
    MaterialTexture metallic_roughness{};
    MaterialTexture normal{};
    // Reserved until indirect illumination is implemented; nonempty values fail.
    MaterialTexture occlusion{};
    MaterialTexture emissive{};
};

// Immutable GPU material: owns set 2 and its parameter UBO through DescriptorSet.
// Shared across frame slots. Build through Renderer::make_material; Renderer,
// borrowed textures and samplers must outlive it. Wait for GPU use before teardown.
class Material {
    friend class Renderer;
    Material(DescriptorSet descriptor_set, std::uint32_t &live_materials, bool uses_normal_map)
        : descriptor_set_(std::move(descriptor_set)), live_materials_(&live_materials),
          uses_normal_map_(uses_normal_map)
    {
        ++*live_materials_;
    }

  public:
    Material(Material const &) = delete;
    Material &operator=(Material const &) = delete;
    Material(Material &&other) noexcept
        : descriptor_set_(std::move(other.descriptor_set_)),
          live_materials_(std::exchange(other.live_materials_, nullptr)),
          uses_normal_map_(other.uses_normal_map_)
    {
    }
    Material &operator=(Material &&) = delete;
    ~Material()
    {
        if (live_materials_)
            --*live_materials_;
    }

    vk::raii::DescriptorSet const &descriptor_set() const { return descriptor_set_.raii(); }
    bool uses_normal_map() const { return uses_normal_map_; }

  private:
    DescriptorSet descriptor_set_;
    // Non-owning renderer counter; null only after moving. Renderer must outlive us.
    std::uint32_t *live_materials_;
    bool uses_normal_map_;
};

} // namespace lc1
