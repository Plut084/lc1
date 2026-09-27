#include "lc1/vk/renderer.hpp"

#include "lc1/utils.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/frame_loop.hpp"
#include "lc1/vk/memory.hpp"
#include "lc1/vk/shader-stages.hpp"
#include "lc1/vk/swapchain.hpp"
#include "lc1/vk/vertex.hpp"

#include <array>
#include <string>
#include <vector>

namespace lc1 {
namespace {

void transition_image_layout(vk::raii::CommandBuffer const &command_buffer, vk::Image image,
                             vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                             vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
                             vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access)
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
                .aspectMask = vk::ImageAspectFlagBits::eColor,
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

// The shader modules only need to live until the pipeline is created, so they
// stay local to this function.
Pipeline make_pipeline(Device const &device, Swapchain const &swapchain)
{
    auto const shader_code = read_file("shaders/shader.spv");
    ShaderModule const vert_module{device, shader_code};
    ShaderModule const frag_module{device, shader_code};

    ShaderStages shader_stages;
    shader_stages.append(vk::ShaderStageFlagBits::eVertex, vert_module, "vertMain");
    shader_stages.append(vk::ShaderStageFlagBits::eFragment, frag_module, "fragMain");
    return Pipeline{device, swapchain, shader_stages};
}

} // namespace

Renderer::Renderer(Device const &device, Swapchain const &swapchain)
    : pipeline_{make_pipeline(device, swapchain)}
{
    try {
        vk::raii::Device const &raii_device = device.raii();

        vk::DescriptorPoolSize pool_size{
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = FrameLoop::frames_in_flight,
        };
        vk::DescriptorPoolCreateInfo pool_info{
            // Allows for individual descriptor set to be freed, which is what
            // ~vk::raii::DescriptorSet does.
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = FrameLoop::frames_in_flight,
        };
        pool_info.setPoolSizes(pool_size);
        descriptor_pool_ = raii_device.createDescriptorPool(pool_info);

        // One layout per set to allocate: the length of this array is the count.
        std::vector<vk::DescriptorSetLayout> const layouts(FrameLoop::frames_in_flight,
                                                           *pipeline_.descriptor_set_layout());
        vk::DescriptorSetAllocateInfo alloc_info{.descriptorPool = descriptor_pool_};
        alloc_info.setSetLayouts(layouts);
        auto descriptor_sets = raii_device.allocateDescriptorSets(alloc_info);

        frames_.reserve(FrameLoop::frames_in_flight);
        for (auto &descriptor_set : descriptor_sets) {
            // Host-visible with no staging buffer: the CPU rewrites it every
            // frame, so a staging copy would only add work.
            Buffer uniform_buffer{device, sizeof(UniformBufferObject),
                                  vk::BufferUsageFlagBits::eUniformBuffer,
                                  vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};

            vk::DescriptorBufferInfo buffer_info{
                .buffer = *uniform_buffer.raii(),
                .offset = 0,
                .range = sizeof(UniformBufferObject),
            };
            vk::WriteDescriptorSet write{
                .dstSet = descriptor_set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
            };
            write.setBufferInfo(buffer_info);
            raii_device.updateDescriptorSets(write, {});

            frames_.push_back({
                .uniform_buffer = std::move(uniform_buffer),
                .descriptor_set = std::move(descriptor_set),
            });
        }
    }
    catch (vk::SystemError const &error) {
        fail(std::string("descriptor resource creation failed: ") + error.what());
    }
}

void Renderer::record(vk::raii::CommandBuffer const &command_buffer, std::uint32_t frame_index,
                      Swapchain const &swapchain, std::uint32_t image_index,
                      UniformBufferObject const &ubo, std::span<DrawItem const> draws) const
{
    FrameData const &frame = frames_[frame_index];
    SwapchainImage const &target = swapchain.images()[image_index];

    // Safe to overwrite here: the frame loop calls record only after waiting on
    // this slot's fence, so the GPU is done with the frame that last read it.
    frame.uniform_buffer.upload(std::as_bytes(std::span{&ubo, 1}));

    command_buffer.begin({
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    });

    // UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL.
    //
    // srcStageMask must be COLOR_ATTACHMENT_OUTPUT, matching the stage at which
    // frame_loop waits on the acquire semaphore. VK_PIPELINE_STAGE_2_NONE is
    // the tempting sync2 idiom here ("the previous contents are irrelevant"),
    // and it is legal against this barrier's own VUIDs -- but it leaves the
    // layout transition unordered against vkAcquireNextImageKHR, and sync
    // validation reports it as SYNC-HAZARD-WRITE-AFTER-READ.
    //
    // srcAccessMask stays NONE: the presentation engine's read is not tracked
    // by access masks, so there is nothing to make available. This is
    // deliberately an execution-only dependency.
    transition_image_layout(command_buffer, target.image, vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eColorAttachmentOptimal,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::AccessFlags2{}, // VK_ACCESS_2_NONE
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::AccessFlagBits2::eColorAttachmentWrite);

    // Deliberately not black. A black clear is indistinguishable from a broken
    // swapchain or a blank window, which is exactly the failure this milestone
    // exists to rule out.
    constexpr vk::ClearColorValue clear_color{0.9F, 0.35F, 0.05F, 1.0F};

    // One dereference back to the C++ handle. Going all the way to VkImageView
    // would be two user-defined conversions, and those do not chain.
    vk::RenderingAttachmentInfo color_attachment;
    color_attachment
        .setImageView(*target.view)
        // NOT VK_IMAGE_LAYOUT_UNDEFINED:
        // VUID-VkRenderingAttachmentInfo-imageView-06135 forbids it here, even
        // though the barrier immediately above legitimately uses UNDEFINED as
        // oldLayout. This is the single most common mistake in this path.
        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearValue{}.setColor(clear_color));

    vk::RenderingInfo rendering{
        .renderArea = {.offset = vk::Offset2D{.x = 0, .y = 0}, .extent = swapchain.extent()},
        // must not be 0: VUID-VkRenderingInfo-viewMask-06069
        .layerCount = 1,
    };
    rendering.setColorAttachments(color_attachment);

    command_buffer.beginRendering(rendering);

    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.raii());
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_.layout(), 0,
                                      *frame.descriptor_set, nullptr);
    // Both cover the whole image, taken from the swapchain as it is now, so
    // they are correct again right after a resize.
    vk::Extent2D const extent = swapchain.extent();
    // Negative height flips Y for glm, whose clip-space Y points up where
    // Vulkan's points down. y moves to the bottom edge to match: with y = 0,
    // the flipped viewport would cover [-height, 0], entirely off-screen.
    command_buffer.setViewport(0, vk::Viewport{
                                      .x = 0.0F,
                                      .y = static_cast<float>(extent.height),
                                      .width = static_cast<float>(extent.width),
                                      .height = -static_cast<float>(extent.height),
                                      .minDepth = 0.0F,
                                      .maxDepth = 1.0F,
                                  });
    command_buffer.setScissor(0, vk::Rect2D{.offset = {.x = 0, .y = 0}, .extent = extent});

    for (DrawItem const &item : draws)
        item.mesh->draw(command_buffer);

    command_buffer.endRendering();

    // COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR.
    // Destination stage/access are NONE, not BOTTOM_OF_PIPE: presentation is
    // not a pipeline stage. What actually orders this write against the
    // presentation engine's read is the submit -> present semaphore pair, so a
    // missing render_finished wait in present is a real race, not a style
    // issue.
    transition_image_layout(command_buffer, target.image, vk::ImageLayout::eColorAttachmentOptimal,
                            vk::ImageLayout::ePresentSrcKHR,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::AccessFlagBits2::eColorAttachmentWrite,
                            vk::PipelineStageFlagBits2{}, // VK_PIPELINE_STAGE_2_NONE
                            vk::AccessFlags2{});          // VK_ACCESS_2_NONE

    command_buffer.end();
}

} // namespace lc1
