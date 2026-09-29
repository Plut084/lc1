#pragma once

#include "lc1/vk/common.hpp"

#include <utility>

namespace lc1 {

// How a mesh's surface looks: for now, one texture. Holds descriptor set 2
// (see Pipeline::material_set_layout), written once and never changed, so
// unlike the uniform buffer's set there is one per material, not one per frame
// in flight. Shared like Mesh: a hundred soldiers carrying the same banner
// point at one Material.
//
// Build it with Renderer::make_material, which owns the pool and the layout
// the set must come from. The Texture it was made from must outlive it, and
// it must not outlive that Renderer: its set frees itself back into the pool.
class Material {
  public:
    explicit Material(vk::raii::DescriptorSet descriptor_set)
        : descriptor_set_(std::move(descriptor_set))
    {
    }

    vk::raii::DescriptorSet const &descriptor_set() const { return descriptor_set_; }

  private:
    vk::raii::DescriptorSet descriptor_set_;
};

} // namespace lc1
