#include "lc1/vk/renderer.hpp"

#include "lc1/vk/common.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/memory.hpp"
#include "lc1/vk/shader-stages.hpp"
#include "lc1/vk/texture.hpp"
#include "lc1/vk/vertex.hpp"

#include <array>
#include <vector>

namespace lc1 {
namespace {

vk::Format single_color_format(std::vector<vk::Format> const &formats)
{
    if (formats.size() != 1) {
        fail("renderer requires exactly one color attachment format");
    }
    return formats.front();
}

// The shader modules only need to live until the pipeline is created, so they
// stay local to this function.
Pipeline make_pipeline(Device const &device,
                       std::vector<vk::Format> const &color_attachment_formats,
                       vk::Format depth_image_format, vk::SampleCountFlagBits samples)
{
    auto const shader_code = read_file("shaders/shader.spv");
    ShaderModule const shader_module{device, shader_code};

    ShaderStages shader_stages;
    shader_stages.append(vk::ShaderStageFlagBits::eVertex, shader_module, "vertMain");
    shader_stages.append(vk::ShaderStageFlagBits::eFragment, shader_module, "fragMain");
    return Pipeline{device, shader_stages, color_attachment_formats, depth_image_format, samples};
}

} // namespace

Renderer::Renderer(Device const &device, std::vector<vk::Format> const &color_attachment_formats,
                   vk::SampleCountFlagBits samples)
    : device_{device}, color_format_{single_color_format(color_attachment_formats)},
      depth_format_{find_depth_format(device)}, samples_{samples},
      pipeline_{make_pipeline(device, color_attachment_formats, depth_format_, samples)},
      material_pool_(nullptr), sampler_(device)
{
    vk::DescriptorPoolSize const pool_size{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = max_materials,
    };
    vk::DescriptorPoolCreateInfo pool_info{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = max_materials,
    };
    pool_info.setPoolSizes(pool_size);
    material_pool_ = device.raii().createDescriptorPool(pool_info);
}

FrameResources Renderer::make_frame_resources() const
{
    // Host-visible: record rewrites this buffer after its previous GPU use.
    Buffer uniform_buffer{device_, sizeof(UniformBufferObject),
                          vk::BufferUsageFlagBits::eUniformBuffer,
                          vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
    vk::DescriptorPoolSize const pool_size{
        .type = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
    };
    vk::DescriptorPoolCreateInfo pool_info{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 1,
    };
    pool_info.setPoolSizes(pool_size);
    auto descriptor_pool = device_.raii().createDescriptorPool(pool_info);
    vk::DescriptorSetAllocateInfo alloc_info{.descriptorPool = *descriptor_pool};
    alloc_info.setSetLayouts(*pipeline_.frame_set_layout());
    auto descriptor_sets = device_.raii().allocateDescriptorSets(alloc_info);

    vk::DescriptorBufferInfo const buffer_info{
        .buffer = *uniform_buffer.raii(),
        .offset = 0,
        .range = sizeof(UniformBufferObject),
    };
    vk::WriteDescriptorSet const write{
        .dstSet = *descriptor_sets.front(),
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &buffer_info,
    };
    device_.raii().updateDescriptorSets(write, {});

    return FrameResources{std::move(uniform_buffer), std::move(descriptor_pool),
                          std::move(descriptor_sets.front())};
}

Material Renderer::make_material(Texture const &texture)
{
    try {
        vk::DescriptorSetAllocateInfo alloc_info{.descriptorPool = material_pool_};
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

void Renderer::ensure_attachments(FrameResources &resources, vk::Extent2D extent) const
{
    // The caller has waited for these resources. Other submissions use their
    // own resources, so replacing these images needs no device-wide wait here.
    if (!resources.depth_image || resources.depth_image->extent() != extent) {
        vk::ImageAspectFlags depth_aspects = vk::ImageAspectFlagBits::eDepth;
        if (depth_format_ == vk::Format::eD32SfloatS8Uint ||
            depth_format_ == vk::Format::eD24UnormS8Uint) {
            // separateDepthStencilLayouts is not enabled: transition both aspects
            // of a combined format even though only depth is attached.
            depth_aspects |= vk::ImageAspectFlagBits::eStencil;
        }
        resources.depth_image.emplace(device_, depth_format_, extent, 1, samples_,
                                      vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                      depth_aspects);
    }
    if (samples_ != vk::SampleCountFlagBits::e1 &&
        (!resources.color_image || resources.color_image->extent() != extent)) {
        resources.color_image.emplace(device_, color_format_, extent, 1, samples_,
                                      vk::ImageUsageFlagBits::eColorAttachment |
                                          vk::ImageUsageFlagBits::eTransientAttachment,
                                      vk::ImageAspectFlagBits::eColor);
    }
}

void Renderer::record(vk::raii::CommandBuffer const &command_buffer, FrameResources &resources,
                      RenderTarget const &target, UniformBufferObject const &ubo,
                      std::span<DrawItem const> draws)
{
    if (target.extent.width == 0 || target.extent.height == 0) {
        fail("cannot render to a target with a zero extent");
    }

    // Safe to overwrite: the caller has waited for these resources' GPU use.
    resources.uniform_buffer.upload(std::as_bytes(std::span{&ubo, 1}));
    ensure_attachments(resources, target.extent);
    Image const &depth_image = *resources.depth_image;

    constexpr auto depth_stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                  vk::PipelineStageFlagBits2::eLateFragmentTests;
    // Depth is cleared every frame, so discard its old contents. Keep the
    // dependency on earlier depth writes when reusing an existing image.
    depth_image.transition_layout(command_buffer, vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eDepthStencilAttachmentOptimal, depth_stages,
                                  vk::AccessFlagBits2::eDepthStencilAttachmentWrite, depth_stages,
                                  vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                      vk::AccessFlagBits2::eDepthStencilAttachmentWrite);

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

    vk::RenderingAttachmentInfo color_attachment{
        .imageView = *target.view,
        // NOT VK_IMAGE_LAYOUT_UNDEFINED:
        // VUID-VkRenderingAttachmentInfo-imageView-06135 forbids it here.
        // The caller transitions the target before record.
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{}.setColor(clear_color),
    };
    if (resources.color_image) {
        resources.color_image->transition_layout(
            command_buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite);
        color_attachment.imageView = *resources.color_image->view();
        color_attachment.resolveMode = vk::ResolveModeFlagBits::eAverage;
        color_attachment.resolveImageView = *target.view;
        color_attachment.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        // Only the single-sampled output must survive the rendering pass.
        color_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    }
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
                                      *resources.descriptor_set, nullptr);
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
