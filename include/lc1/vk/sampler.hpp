#pragma once

#include "lc1/vk/common.hpp"
#include "lc1/vk/device.hpp"

namespace lc1 {

class Sampler {
  public:
    Sampler(Device const &device)
        : sampler_(device.raii().createSampler(vk::SamplerCreateInfo{
              .magFilter = vk::Filter::eLinear,
              .minFilter = vk::Filter::eLinear,
              .addressModeU = vk::SamplerAddressMode::eRepeat,
              .addressModeV = vk::SamplerAddressMode::eRepeat,
              .addressModeW = vk::SamplerAddressMode::eRepeat,
              .anisotropyEnable = vk::True,
              .maxAnisotropy = device.raii_physical().getProperties().limits.maxSamplerAnisotropy,
              .compareEnable = vk::False,
              .compareOp = vk::CompareOp::eAlways,
              .borderColor = vk::BorderColor::eIntOpaqueBlack,
              .unnormalizedCoordinates = vk::False,
          }
                                                   .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                                                   .setMipLodBias(0.0F)
                                                   .setMinLod(0.0F)
                                                   .setMaxLod(0.0F)))
    {
    }

    vk::raii::Sampler const &raii() const { return sampler_; }

  private:
    vk::raii::Sampler sampler_;
};

} // namespace lc1
