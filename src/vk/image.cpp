#include "lc1/vk/image.hpp"

#include "lc1/vk/device.hpp"

#include <spdlog/spdlog.h>

namespace lc1 {

vk::Format find_supported_format(Device const &device, std::vector<vk::Format> const &candidates,
                                 vk::ImageTiling tiling, vk::FormatFeatureFlags features)
{
    for (auto const format : candidates) {
        vk::FormatProperties props = device.raii_physical().getFormatProperties(format);

        if (((tiling == vk::ImageTiling::eLinear) &&
             ((props.linearTilingFeatures & features) == features)) ||
            ((tiling == vk::ImageTiling::eOptimal) &&
             ((props.optimalTilingFeatures & features) == features))) {
            return format;
        }
    }

    fail("failed to find supported format");
}

vk::Format find_depth_format(Device const &device)
{
    return find_supported_format(
        device, {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

Image::Image(Device const &device, vk::Format format, vk::Extent2D extent,
             vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect)
    : image_{device.allocator().createImage(
          {
              .imageType = vk::ImageType::e2D,
              .format = format,
              .extent = vk::Extent3D{.width = extent.width, .height = extent.height, .depth = 1},
              .mipLevels = 1,
              .arrayLayers = 1,
              .samples = vk::SampleCountFlagBits::e1,
              .tiling = vk::ImageTiling::eOptimal,
              .usage = usage,
              .sharingMode = vk::SharingMode::eExclusive,
              .initialLayout = vk::ImageLayout::eUndefined,
          },
          {
              // No host-access flag: images are filled through a staging
              // buffer, never mapped, so VMA picks DEVICE_LOCAL memory.
              .usage = vma::MemoryUsage::eAuto,
          })},
      view_{device.raii().createImageView({
          .image = *image_,
          .viewType = vk::ImageViewType::e2D,
          .format = format,
          .subresourceRange =
              {
                  .aspectMask = aspect,
                  .baseMipLevel = 0,
                  .levelCount = 1,
                  .baseArrayLayer = 0,
                  .layerCount = 1,
              },
      })},
      extent_{extent}
{
    spdlog::info("[Image] created a {}x{} image, format {}, usage {}", extent.width, extent.height,
                 vk::to_string(format), vk::to_string(usage));
}

} // namespace lc1
