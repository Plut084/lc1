#pragma once

#include "lc1/vk/material.hpp"
#include "lc1/vk/gpu-mesh.hpp"

#include <span>
#include <vector>

namespace lc1 {

class Continent;
class Device;
class Renderer;
class GpuTexture;

// Static GPU snapshot, divided into 64-metre chunks. All chunks are resident for now.
// Device, Renderer and GpuTexture must outlive it.
class ContinentView {
  public:
    ContinentView(Device const &device, Renderer &renderer, GpuTexture const &texture,
                  Continent const &continent);
    ContinentView(ContinentView const &) = delete;
    ContinentView &operator=(ContinentView const &) = delete;
    ContinentView(ContinentView &&) = delete;
    ContinentView &operator=(ContinentView &&) = delete;

    std::span<DrawItem const> draw_items() const { return draws_; }

  private:
    Material material_;
    std::vector<GpuMesh> meshes_;
    // Borrow the completed meshes_ array and material_; neither moves after construction.
    std::vector<DrawItem> draws_;
};

} // namespace lc1
