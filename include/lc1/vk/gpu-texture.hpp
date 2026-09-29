#pragma once

#include "lc1/vk/gpu-image.hpp"

namespace lc1 {

class Device;
struct Image;

// An uploaded image shared like GpuMesh: a hundred soldiers carrying the same
// banner sample one GpuTexture. After construction the image is in
// SHADER_READ_ONLY_OPTIMAL, ready for the fragment shader to sample.
class GpuTexture {
  public:
    // Upload completes before returning; the CPU pixels need not outlive this call.
    GpuTexture(Device const &device, Image const &pixels);

    GpuTexture(GpuTexture const &) = delete;
    GpuTexture(GpuTexture &&) = default;
    GpuTexture &operator=(GpuTexture const &) = delete;
    GpuTexture &operator=(GpuTexture &&) = default;
    ~GpuTexture() = default;

    GpuImage const &image() const { return image_; }

  private:
    GpuImage image_;
};

} // namespace lc1
