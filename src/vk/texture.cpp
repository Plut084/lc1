#include "lc1/vk/texture.hpp"

#include "lc1/vk/buffer.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/one-time-submit.hpp"

#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <cstddef>
#include <memory>
#include <span>

namespace lc1 {
namespace {

struct StbiFree {
    void operator()(stbi_uc *data) const { stbi_image_free(data); }
};

} // namespace

struct Texture::Pixels {
    std::unique_ptr<stbi_uc, StbiFree> data;
    vk::Extent2D extent;

    // 4 bytes per pixel: load() asks stb for RGBA whatever the file holds.
    std::span<stbi_uc const> bytes() const
    {
        return {data.get(), std::size_t{extent.width} * extent.height * 4};
    }
};

constexpr std::uint32_t max_supported_mip_levels(vk::Extent2D extent)
{
    return 1 +
           static_cast<std::uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height))));
}

Texture Texture::load_from_file(Device const &device, std::filesystem::path const &path)
{
    return {device, path};
}

Texture::Pixels Texture::load_from_file(std::filesystem::path const &path, int /*_*/)
{
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    stbi_uc *data =
        stbi_load(path.string().c_str(), &width, &height, &channels_in_file, STBI_rgb_alpha);
    if (data == nullptr) {
        fail("failed to load {}: {}", std::filesystem::absolute(path).string(),
             stbi_failure_reason());
    }
    return {
        .data = std::unique_ptr<stbi_uc, StbiFree>{data},
        .extent = {.width = static_cast<std::uint32_t>(width),
                   .height = static_cast<std::uint32_t>(height)},
    };
}

Texture::Texture(Device const &device, std::filesystem::path const &path)
    : Texture(device, load_from_file(path, 1))
{
    spdlog::info("[Texture] loaded {}", path.string());
}

Texture::Texture(Device const &device, Pixels const &pixels)
    : image_(device, vk::Format::eR8G8B8A8Srgb, pixels.extent,
             max_supported_mip_levels(pixels.extent), vk::SampleCountFlagBits::e1,
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
            .imageExtent = {.width = pixels.extent.width,
                            .height = pixels.extent.height,
                            .depth = 1},
        };
        vk::CopyBufferToImageInfo2 copy{
            .srcBuffer = *staging.raii(),
            .dstImage = image,
            .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
        };
        copy.setRegions(region);
        command_buffer.copyBufferToImage2(copy);

        image_.generate_mipmaps(command_buffer);
        // As in Mesh: the fence wait in one_time_submit only tells the CPU the
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
