#include "lc1/vk/resources/image-barrier.hpp"

namespace lc1 {

void transition_image_layout(vk::raii::CommandBuffer const &command_buffer, vk::Image image,
                             vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                             vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
                             vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access,
                             vk::ImageAspectFlags aspect)
{
    // subresourceRange has to be spelled out: unlike most hpp structs, whose
    // defaults are the Vulkan defaults, ImageSubresourceRange defaults to all
    // zeros -- and levelCount = 0 is invalid.
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange =
            vk::ImageSubresourceRange{
                .aspectMask = aspect,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vk::DependencyInfo dependency;
    dependency.setImageMemoryBarriers(barrier);

    command_buffer.pipelineBarrier2(dependency);
}

} // namespace lc1
