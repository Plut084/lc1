#include "lc1/vk/renderer.hpp"

#include "lc1/camera.hpp"
#include "lc1/lights/light.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/gpu-texture.hpp"
#include "lc1/vk/memory.hpp"
#include "lc1/vk/shader-stages.hpp"
#include "lc1/vk/vertex.hpp"

#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
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
    auto const shader_module = ShaderModule::load_from_file(device, "shaders/shader.spv");
    ShaderStages shader_stages;
    shader_stages.append(vk::ShaderStageFlagBits::eVertex, shader_module, "vertMain");
    shader_stages.append(vk::ShaderStageFlagBits::eFragment, shader_module, "fragMain");
    return Pipeline{device, shader_stages, color_attachment_formats, depth_image_format, samples};
}

constexpr std::uint32_t shadow_resolution = 2048;

vk::Format shadow_format(Device const &device)
{
    auto const required = vk::FormatFeatureFlagBits::eDepthStencilAttachment |
                          vk::FormatFeatureFlagBits::eSampledImage;
    for (auto const format : {vk::Format::eD32Sfloat, vk::Format::eD16Unorm})
        if ((device.raii_physical().getFormatProperties(format).optimalTilingFeatures & required) ==
            required)
            return format;
    fail("no sampled depth-attachment format for shadow mapping");
}

Pipeline make_shadow_pipeline(Device const &device, vk::Format format)
{
    auto const module = ShaderModule::load_from_file(device, "shaders/shadow.spv");
    ShaderStages stages;
    stages.append(vk::ShaderStageFlagBits::eVertex, module, "shadowMain");
    return Pipeline{device, stages, {}, format, vk::SampleCountFlagBits::e1, PipelineKind::Shadow};
}

Pipeline make_shadow_preview_pipeline(Device const &device, std::vector<vk::Format> const &formats,
                                      vk::Format depth_format, vk::SampleCountFlagBits samples)
{
    auto const module = ShaderModule::load_from_file(device, "shaders/shadow-preview.spv");
    ShaderStages stages;
    stages.append(vk::ShaderStageFlagBits::eVertex, module, "previewVert");
    stages.append(vk::ShaderStageFlagBits::eFragment, module, "previewFrag");
    return Pipeline{device, stages, formats, depth_format, samples, PipelineKind::ShadowPreview};
}

ShadowData shadow_data(std::span<Light> lights, glm::vec3 center)
{
    ShadowData data;
    for (std::uint32_t i = 0; i < std::min(lights.size(), std::size_t{max_num_lights}); ++i) {
        auto const &light = lights[i];
        float const length = glm::length(light.direction);
        if (light.type != 0 || !std::isfinite(length) || length <= 1e-4F)
            continue;
        auto const direction = light.direction / length;
        glm::vec3 const up =
            std::abs(direction.y) > 0.99F ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0};
        // Fixed light-space basis and texel-snapped center stabilize a moving shadow map.
        auto const rotation = glm::lookAt(glm::vec3{0}, direction, up);
        auto const light_center = glm::vec3{rotation * glm::vec4{center, 1}};
        constexpr float half_extent = 96.0F;
        constexpr float texel = 2.0F * half_extent / shadow_resolution;
        glm::vec3 const snapped{std::round(light_center.x / texel) * texel,
                                std::round(light_center.y / texel) * texel, light_center.z};
        auto const view = glm::translate(glm::mat4{1}, -snapped - glm::vec3{0, 0, 256}) * rotation;
        // Explicit Vulkan [0,1] depth; Y is flipped by the negative viewport.
        data.view_projection =
            glm::orthoRH_ZO(-half_extent, half_extent, -half_extent, half_extent, 0.1F, 512.0F) *
            view;
        data.light_index.x = i;
        break;
    }
    return data;
}

} // namespace

Renderer::Renderer(Device const &device, std::vector<vk::Format> const &color_attachment_formats,
                   vk::SampleCountFlagBits samples)
    : device_{device}, color_format_{single_color_format(color_attachment_formats)},
      depth_format_{find_depth_format(device)}, samples_{samples},
      pipeline_{make_pipeline(device, color_attachment_formats, depth_format_, samples)},
      shadow_format_{shadow_format(device)},
      shadow_pipeline_{make_shadow_pipeline(device, shadow_format_)},
      shadow_preview_pipeline_{
          make_shadow_preview_pipeline(device, color_attachment_formats, depth_format_, samples)},
      shadow_sampler_{device.raii().createSampler(vk::SamplerCreateInfo{
          .magFilter = vk::Filter::eNearest,
          .minFilter = vk::Filter::eNearest,
          .mipmapMode = vk::SamplerMipmapMode::eNearest,
          .addressModeU = vk::SamplerAddressMode::eClampToBorder,
          .addressModeV = vk::SamplerAddressMode::eClampToBorder,
          .addressModeW = vk::SamplerAddressMode::eClampToBorder,
          .maxLod = 0.0F,
          .borderColor = vk::BorderColor::eFloatOpaqueWhite,
      })},
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

FrameResources Renderer::make_frame_resources(std::uint32_t object_capacity) const
{
    if (object_capacity > std::numeric_limits<std::uint32_t>::max() - 3) {
        fail("object capacity leaves no descriptors for camera and lights");
    }
    std::array const pool_sizes{
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer,
                               .descriptorCount = object_capacity + 3},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler,
                               .descriptorCount = 1},
    };
    vk::DescriptorPoolCreateInfo pool_info{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = object_capacity + 1,
    };
    pool_info.setPoolSizes(pool_sizes);
    auto descriptor_pool = device_.raii().createDescriptorPool(pool_info);

    DescriptorSet globals{device_, descriptor_pool, pipeline_.frame_set_layout(),
                          Pipeline::frame_bindings};
    globals.add_uniform(0, sizeof(CameraData));
    globals.add_uniform(1, sizeof(LightsData));
    globals.add_uniform(2, sizeof(ShadowData));
    GpuImage shadow_image{device_,
                          shadow_format_,
                          {shadow_resolution, shadow_resolution},
                          1,
                          vk::SampleCountFlagBits::e1,
                          vk::ImageUsageFlagBits::eDepthStencilAttachment |
                              vk::ImageUsageFlagBits::eSampled,
                          vk::ImageAspectFlagBits::eDepth};
    globals.set_image(3, {.sampler = *shadow_sampler_,
                          .imageView = *shadow_image.view(),
                          .imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal});
    std::vector<DescriptorSet> objects;
    objects.reserve(object_capacity);
    for (std::uint32_t i = 0; i < object_capacity; ++i) {
        objects.emplace_back(device_, descriptor_pool, pipeline_.object_set_layout(),
                             Pipeline::object_bindings);
        objects.back().add_uniform(0, sizeof(ObjectData));
    }
    return FrameResources{std::move(descriptor_pool), std::move(globals), std::move(objects),
                          std::move(shadow_image)};
}

Material Renderer::make_material(GpuTexture const &texture)
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
            // GpuTexture's constructor leaves the image in this layout.
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
                      RenderTarget const &target, FpsCamera const &camera, std::span<Light> lights,
                      std::span<DrawItem const> draws, glm::vec3 shadow_center, bool preview_shadow)
{
    if (target.extent.width == 0 || target.extent.height == 0) {
        fail("cannot render to a target with a zero extent");
    }

    if (draws.size() > resources.objects.size()) {
        fail("draw count {} exceeds frame object capacity {}", draws.size(),
             resources.objects.size());
    }
    for (std::size_t i = 0; i < draws.size(); ++i) {
        if (!draws[i].mesh || !draws[i].material) {
            fail("draw {} requires a mesh and material", i);
        }
        if (glm::determinant(glm::mat3{draws[i].model}) == 0.0F)
            fail("draw {} has a singular model transform", i);
        ObjectData const object_data{
            .model = draws[i].model,
            .normal_transform = glm::transpose(glm::inverse(draws[i].model)),
        };
        resources.objects[i].upload(0, std::as_bytes(std::span{&object_data, 1}));
    }

    // Safe to overwrite: the caller has waited for these resources' GPU use.
    CameraData const ubo{
        .view = camera.view_matrix(),
        .proj = camera.projection_matrix(),
        .position = glm::vec4{camera.position(), 1.0F},
    };
    resources.globals.upload(0, std::as_bytes(std::span{&ubo, 1}));

    LightsData const lights_data{
        .count = static_cast<std::uint32_t>(std::min(lights.size(), std::size_t{max_num_lights})),
        .lights =
            [&] {
                std::array<Light, max_num_lights> arr{};
                std::copy_n(lights.begin(), std::min(lights.size(), arr.size()), arr.begin());
                return arr;
            }(),
    };
    resources.globals.upload(1, std::as_bytes(std::span{&lights_data, 1}));

    auto const shadow = shadow_data(lights, shadow_center);
    resources.globals.upload(2, std::as_bytes(std::span{&shadow, 1}));

    constexpr auto shadow_stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits2::eLateFragmentTests;
    // Discard old depth, but order the previous frame slot's shader reads/depth writes.
    resources.shadow_image.transition_layout(
        command_buffer, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthStencilAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader | shadow_stages,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        shadow_stages,
        vk::AccessFlagBits2::eDepthStencilAttachmentRead |
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
    vk::RenderingAttachmentInfo const shadow_attachment{
        .imageView = *resources.shadow_image.view(),
        .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{}.setDepthStencil({.depth = 1.0F, .stencil = 0}),
    };
    vk::RenderingInfo const shadow_rendering{
        .renderArea = {.offset = {0, 0}, .extent = {shadow_resolution, shadow_resolution}},
        .layerCount = 1,
        .pDepthAttachment = &shadow_attachment,
    };
    command_buffer.beginRendering(shadow_rendering);
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, shadow_pipeline_.raii());
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shadow_pipeline_.layout(),
                                      0, *resources.globals.raii(), nullptr);
    command_buffer.setViewport(0, vk::Viewport{.x = 0,
                                               .y = static_cast<float>(shadow_resolution),
                                               .width = static_cast<float>(shadow_resolution),
                                               .height = -static_cast<float>(shadow_resolution),
                                               .minDepth = 0,
                                               .maxDepth = 1});
    command_buffer.setScissor(
        0, vk::Rect2D{.offset = {0, 0}, .extent = {shadow_resolution, shadow_resolution}});
    if (shadow.light_index.x < max_num_lights) {
        for (std::size_t i = 0; i < draws.size(); ++i) {
            auto const &item = draws[i];
            command_buffer.setFrontFace(glm::determinant(glm::mat3{item.model}) < 0.0F
                                            ? vk::FrontFace::eClockwise
                                            : vk::FrontFace::eCounterClockwise);
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                              shadow_pipeline_.layout(), 1,
                                              *resources.objects[i].raii(), nullptr);
            item.mesh->draw(command_buffer);
        }
    }
    command_buffer.endRendering();
    resources.shadow_image.transition_layout(
        command_buffer, vk::ImageLayout::eDepthStencilAttachmentOptimal,
        vk::ImageLayout::eDepthStencilReadOnlyOptimal, shadow_stages,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);

    ensure_attachments(resources, target.extent);
    GpuImage const &depth_image = *resources.depth_image;

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
    constexpr vk::ClearColorValue clear_color{0.24F, 0.42F, 0.64F, 1.0F};

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
    // Set 0 is shared by every draw; sets 1 and 2 select object and material.
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_.layout(), 0,
                                      *resources.globals.raii(), nullptr);
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

    if (preview_shadow) {
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                    shadow_preview_pipeline_.raii());
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          shadow_preview_pipeline_.layout(), 0,
                                          *resources.globals.raii(), nullptr);
        command_buffer.draw(3, 1, 0, 0);
        command_buffer.endRendering();
        return;
    }

    for (std::size_t i = 0; i < draws.size(); ++i) {
        DrawItem const &item = draws[i];
        // A reflected model reverses winding; callers never need to flip cull mode.
        command_buffer.setFrontFace(glm::determinant(glm::mat3{item.model}) < 0.0F
                                        ? vk::FrontFace::eClockwise
                                        : vk::FrontFace::eCounterClockwise);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_.layout(), 1,
                                          *resources.objects[i].raii(), nullptr);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_.layout(), 2,
                                          *item.material->descriptor_set(), nullptr);
        item.mesh->draw(command_buffer);
    }

    command_buffer.endRendering();
}

} // namespace lc1
