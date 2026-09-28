#include "lc1/vk/renderer.hpp"

#include "lc1/vk/common.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/frame_loop.hpp"
#include "lc1/vk/memory.hpp"
#include "lc1/vk/shader-stages.hpp"
#include "lc1/vk/texture.hpp"
#include "lc1/vk/vertex.hpp"

#include <array>
#include <vector>

namespace lc1 {
namespace {

// The shader modules only need to live until the pipeline is created, so they
// stay local to this function.
Pipeline make_pipeline(Device const &device,
                       std::vector<vk::Format> const &color_attachment_formats,
                       vk::Format depth_image_format)
{
    auto const shader_code = read_file("shaders/shader.spv");
    ShaderModule const shader_module{device, shader_code};

    ShaderStages shader_stages;
    shader_stages.append(vk::ShaderStageFlagBits::eVertex, shader_module, "vertMain");
    shader_stages.append(vk::ShaderStageFlagBits::eFragment, shader_module, "fragMain");
    return Pipeline{device, shader_stages, color_attachment_formats, depth_image_format};
}

} // namespace

Renderer::Renderer(Device const &device, std::vector<vk::Format> const &color_attachment_formats)
    : device_{device}, depth_format_{find_depth_format(device)},
      pipeline_{make_pipeline(device, color_attachment_formats, depth_format_)},
      descriptor_pool_(nullptr), sampler_(device)
{
    try {
        vk::raii::Device const &raii_device = device.raii();

        // Set 0 once per frame in flight, set 1 once per material.
        std::vector<vk::DescriptorPoolSize> pool_sizes;
        pool_sizes.push_back({
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = FrameLoop::frames_in_flight,
        });
        pool_sizes.push_back({
            .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = max_materials,
        });
        vk::DescriptorPoolCreateInfo pool_info{
            // Allows for individual descriptor set to be freed, which is what
            // ~vk::raii::DescriptorSet does.
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = FrameLoop::frames_in_flight + max_materials,
        };
        pool_info.setPoolSizes(pool_sizes);
        descriptor_pool_ = raii_device.createDescriptorPool(pool_info);

        // One layout per set to allocate: the length of this array is the count.
        std::vector<vk::DescriptorSetLayout> const layouts(FrameLoop::frames_in_flight,
                                                           *pipeline_.frame_set_layout());
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
            vk::WriteDescriptorSet const write{
                .dstSet = descriptor_set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &buffer_info,
            };
            raii_device.updateDescriptorSets(write, {});

            frames_.push_back({
                .uniform_buffer = std::move(uniform_buffer),
                .descriptor_set = std::move(descriptor_set),
                .depth_image = std::nullopt, // Create on the fly.
            });
        }
    }
    catch (vk::SystemError const &error) {
        fail("descriptor resource creation failed: {}", error.what());
    }
}

Material Renderer::make_material(Texture const &texture)
{
    try {
        vk::DescriptorSetAllocateInfo alloc_info{.descriptorPool = descriptor_pool_};
        alloc_info.setSetLayouts(*pipeline_.material_set_layout());
        auto descriptor_sets = device_.raii().allocateDescriptorSets(alloc_info);

        // Written once: the texture never changes, so no frame in flight can be
        // reading an older version of this set.
        vk::DescriptorImageInfo image_info{
            .sampler = *sampler_.raii(),
            .imageView = *texture.image().view(),
            // Texture's constructor leaves the image in this layout.
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        vk::WriteDescriptorSet const write{
            .dstSet = descriptor_sets.front(),
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &image_info,
        };
        device_.raii().updateDescriptorSets(write, {});

        return Material{std::move(descriptor_sets.front())};
    }
    catch (vk::SystemError const &error) {
        fail("material creation failed: {}", error.what());
    }
}

Image const &Renderer::ensure_depth_image(FrameData &frame, vk::Extent2D extent)
{
    // This slot's fence protects its depth image. Other frames keep
    // their own images, so a resize needs no device-wide wait here.
    if (!frame.depth_image || frame.depth_image->extent() != extent) {
        frame.depth_image.emplace(device_, depth_format_, extent,
                                  vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                  vk::ImageAspectFlagBits::eDepth);
    }
    return *frame.depth_image;
}

void Renderer::record(vk::raii::CommandBuffer const &command_buffer, std::uint32_t frame_index,
                      RenderTarget const &target, UniformBufferObject const &ubo,
                      std::span<DrawItem const> draws)
{
    if (target.extent.width == 0 || target.extent.height == 0) {
        fail("cannot render to a target with a zero extent");
    }

    FrameData &frame = frames_[frame_index];

    // Safe to overwrite here: the frame loop calls record only after waiting on
    // this slot's fence, so the GPU is done with the frame that last read it.
    frame.uniform_buffer.upload(std::as_bytes(std::span{&ubo, 1}));
    Image const &depth_image = ensure_depth_image(frame, target.extent);

    vk::ImageAspectFlags depth_aspects = vk::ImageAspectFlagBits::eDepth;
    if (depth_format_ == vk::Format::eD32SfloatS8Uint ||
        depth_format_ == vk::Format::eD24UnormS8Uint) {
        // separateDepthStencilLayouts is not enabled: transition both aspects
        // of a combined format even though only depth is attached.
        depth_aspects |= vk::ImageAspectFlagBits::eStencil;
    }
    constexpr auto depth_stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                  vk::PipelineStageFlagBits2::eLateFragmentTests;
    // Depth is cleared every frame, so discard its old contents. Keep the
    // dependency on earlier depth writes when reusing an existing image.
    transition_image_layout(command_buffer, *depth_image.raii(), vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eDepthStencilAttachmentOptimal, depth_stages,
                            vk::AccessFlagBits2::eDepthStencilAttachmentWrite, depth_stages,
                            vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                            depth_aspects);

    vk::RenderingInfo rendering{
        .renderArea =
            {
                .offset = vk::Offset2D{.x = 0, .y = 0},
                .extent = target.extent,
            },
        // must not be 0: VUID-VkRenderingInfo-viewMask-06069
        .layerCount = 1,
    };
    // Deliberately not black, so an untouched target is easy to distinguish
    // from a rendered frame.
    constexpr vk::ClearColorValue clear_color{0.9F, 0.35F, 0.05F, 1.0F};

    vk::RenderingAttachmentInfo const color_attachment{
        .imageView = *target.view,
        // NOT VK_IMAGE_LAYOUT_UNDEFINED:
        // VUID-VkRenderingAttachmentInfo-imageView-06135 forbids it here.
        // The caller transitions the target before record.
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{}.setColor(clear_color),
    };
    rendering.setColorAttachments(color_attachment);

    vk::RenderingAttachmentInfo const depth_attachment{
        .imageView = *depth_image.view(),
        .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = vk::ClearValue{}.setDepthStencil({.depth = 1.0F, .stencil = 0}),
    };
    rendering.setPDepthAttachment(&depth_attachment);

    command_buffer.beginRendering(rendering);

    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.raii());
    // Set 0 once for the whole frame; set 1 per draw, below. Binding set 1
    // leaves set 0 bound: the two layouts are compatible up to set 0.
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_.layout(), 0,
                                      *frame.descriptor_set, nullptr);
    // Render area, viewport and scissor use the same caller-provided extent.
    vk::Extent2D const extent = target.extent;
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

    for (DrawItem const &item : draws) {
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_.layout(), 1,
                                          *item.material->descriptor_set(), nullptr);
        item.mesh->draw(command_buffer);
    }

    command_buffer.endRendering();
}

} // namespace lc1
