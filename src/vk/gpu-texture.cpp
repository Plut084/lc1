#include "lc1/vk/gpu-texture.hpp"

#include "lc1/image.hpp"
#include "lc1/vk/buffer.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/one-time-submit.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace lc1 {
namespace {

constexpr std::uint32_t max_supported_mip_levels(vk::Extent2D extent)
{
    return 1 +
           static_cast<std::uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height))));
}

} // namespace

GpuTexture::GpuTexture(Device const &device, Image const &pixels)
    : image_(device, vk::Format::eR8G8B8A8Srgb,
             {.width = pixels.extent.x, .height = pixels.extent.y},
             max_supported_mip_levels({.width = pixels.extent.x, .height = pixels.extent.y}),
             vk::SampleCountFlagBits::e1,
             vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                 vk::ImageUsageFlagBits::eSampled,
             vk::ImageAspectFlagBits::eColor)
{
    // Only needed until the copy has finished; one_time_submit waits for that,
    // so it can die at the end of this constructor.
    Buffer staging{device, pixels.bytes().size_bytes(), vk::BufferUsageFlagBits::eTransferSrc,
                   vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
    staging.upload(std::as_bytes(pixels.bytes()));

    vk::Image const image = *image_.raii();
    one_time_submit(device, [&](vk::raii::CommandBuffer const &command_buffer) {
        // Nothing earlier to wait for: the image is new. All mip levels are
        // transitioned here: level 0 is written by COPY, the rest by BLIT.
        image_.transition_layout(
            command_buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);

        vk::BufferImageCopy2 region{
            .bufferOffset = 0,
            // 0 and 0: the pixels are tightly packed, row after row.
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                 .mipLevel = 0,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
            .imageOffset = {.x = 0, .y = 0, .z = 0},
            .imageExtent = {.width = pixels.extent.x, .height = pixels.extent.y, .depth = 1},
        };
        vk::CopyBufferToImageInfo2 copy{
            .srcBuffer = *staging.raii(),
            .dstImage = image,
            .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
        };
        copy.setRegions(region);
        command_buffer.copyBufferToImage2(copy);

        image_.generate_mipmaps(command_buffer);
        // As in GpuMesh: the fence wait in one_time_submit only tells the CPU the
        // copy is done. This barrier is what makes the copy visible to the
        // fragment shader's sampling in the frame loop's later submits.
        // image_.transition_layout(
        //     command_buffer, vk::ImageLayout::eTransferDstOptimal,
        //     vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eCopy,
        //     vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        //     vk::AccessFlagBits2::eShaderSampledRead);
    });
}

} // namespace lc1
