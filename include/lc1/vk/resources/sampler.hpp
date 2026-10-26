#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/device.hpp"

namespace lc1 {

class Sampler {
  public:
    static vk::SamplerCreateInfo default_create_info(Device const &device)
    {
        return vk::SamplerCreateInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .anisotropyEnable = vk::True,
            .maxAnisotropy = device.raii_physical().getProperties().limits.maxSamplerAnisotropy,
            .compareEnable = vk::False,
            .compareOp = vk::CompareOp::eAlways,
            .maxLod = vk::LodClampNone,
            .borderColor = vk::BorderColor::eIntOpaqueBlack,
            .unnormalizedCoordinates = vk::False,
        };
    }

    explicit Sampler(Device const &device)
        : info_(default_create_info(device)), sampler_(device.raii().createSampler(info_))
    {
    }

    vk::SamplerCreateInfo const &create_info() const { return info_; }

    vk::raii::Sampler const &raii() const { return sampler_; }

  private:
    vk::SamplerCreateInfo info_;
    vk::raii::Sampler sampler_;
};

} // namespace lc1
