#include "lc1/vk/render/shadow-map-preview.hpp"

#include "draw-validation.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/render/frame-resources.hpp"
#include "lc1/vk/render/render-data.hpp"
#include "lc1/vk/resources/shader-stages.hpp"

#include <algorithm>
#include <cmath>

namespace lc1 {
namespace {

struct ShadowData {
    glm::mat4 view_projection{1.0F};
    glm::uvec4 light_index{10U, 0U, 0U, 0U}; // x selects the shadowed light; 10 disables it.
};
static_assert(sizeof(ShadowData) == 80 && offsetof(ShadowData, light_index) == 64);

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

Pipeline make_shadow_preview_pipeline(Device const &device, vk::Format output_format)
{
    auto const module = ShaderModule::load_from_file(device, "shaders/shadow-preview.spv");
    ShaderStages stages;
    stages.append(vk::ShaderStageFlagBits::eVertex, module, "previewVert");
    stages.append(vk::ShaderStageFlagBits::eFragment, module, "previewFrag");
    return Pipeline{device,
                    stages,
                    {output_format},
                    vk::Format::eUndefined,
                    vk::SampleCountFlagBits::e1,
                    PipelineKind::ShadowPreview};
}

ShadowData shadow_data(std::span<scene::Light const> lights, ShadowMapRegion const &region)
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
        auto const light_center = glm::vec3{rotation * glm::vec4{region.center, 1}};
        float const half_extent = region.half_extent;
        float const texel = 2.0F * half_extent / shadow_resolution;
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

ShadowMapPreview::ShadowMapPreview(Device const &device, vk::Format output_format)
    : device_{device}, depth_format_{shadow_format(device)},
      depth_pipeline_{make_shadow_pipeline(device, depth_format_)},
      preview_pipeline_{make_shadow_preview_pipeline(device, output_format)},
      sampler_{device.raii().createSampler(vk::SamplerCreateInfo{
          .magFilter = vk::Filter::eNearest,
          .minFilter = vk::Filter::eNearest,
          .mipmapMode = vk::SamplerMipmapMode::eNearest,
          .addressModeU = vk::SamplerAddressMode::eClampToBorder,
          .addressModeV = vk::SamplerAddressMode::eClampToBorder,
          .addressModeW = vk::SamplerAddressMode::eClampToBorder,
          .maxLod = 0.0F,
          .borderColor = vk::BorderColor::eFloatOpaqueWhite,
      })}
{
}

void ShadowMapPreview::ensure_resources(FrameResources &resources) const
{
    if (resources.shadow_map)
        return;
    GpuImage depth{device_,
                   depth_format_,
                   {shadow_resolution, shadow_resolution},
                   1,
                   vk::SampleCountFlagBits::e1,
                   vk::ImageUsageFlagBits::eDepthStencilAttachment |
                       vk::ImageUsageFlagBits::eSampled,
                   vk::ImageAspectFlagBits::eDepth};
    DescriptorSet globals{device_, resources.descriptor_pool, depth_pipeline_.frame_set_layout(),
                          Pipeline::shadow_map_frame_bindings};
    globals.add_uniform(2, sizeof(ShadowData));
    globals.set_image(3, {.sampler = *sampler_,
                          .imageView = *depth.view(),
                          .imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal});
    resources.shadow_map.emplace(ShadowMapFrameResources{std::move(depth), std::move(globals)});
}

void ShadowMapPreview::record(vk::raii::CommandBuffer const &command_buffer,
                              FrameResources &resources, RenderTarget const &target,
                              std::span<scene::Light const> lights, std::span<DrawItem const> draws,
                              ShadowMapRegion const &region) const
{
    validate_draws(resources, target.extent, draws);
    if (!std::isfinite(region.center.x) || !std::isfinite(region.center.y) ||
        !std::isfinite(region.center.z) || !std::isfinite(region.half_extent) ||
        region.half_extent <= 0.0F)
        fail("shadow-map preview requires a finite center and positive half extent");
    ensure_resources(resources);
    for (std::size_t i = 0; i < draws.size(); ++i) {
        // The depth shader only consumes model; the remaining fields are unused.
        ObjectData const object{
            .model = draws[i].model, .normal_transform = glm::mat4{1}, .shadow_identity = {}};
        resources.objects[i].upload(0, std::as_bytes(std::span{&object, 1}));
    }
    auto const shadow = shadow_data(lights, region);
    resources.shadow_map->globals.upload(2, std::as_bytes(std::span{&shadow, 1}));

    constexpr auto shadow_stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits2::eLateFragmentTests;
    // Discard old depth, but order the previous frame slot's shader reads/depth writes.
    resources.shadow_map->depth.transition_layout(
        command_buffer, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthStencilAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader | shadow_stages,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        shadow_stages,
        vk::AccessFlagBits2::eDepthStencilAttachmentRead |
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
    vk::RenderingAttachmentInfo const shadow_attachment{
        .imageView = *resources.shadow_map->depth.view(),
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
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, depth_pipeline_.raii());
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, depth_pipeline_.layout(), 0,
                                      *resources.shadow_map->globals.raii(), nullptr);
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
            if (!item.casts_shadow)
                continue;
            command_buffer.setFrontFace(glm::determinant(glm::mat3{item.model}) < 0.0F
                                            ? vk::FrontFace::eClockwise
                                            : vk::FrontFace::eCounterClockwise);
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                              depth_pipeline_.layout(), 1,
                                              *resources.objects[i].raii(), nullptr);
            item.mesh->draw(command_buffer);
        }
    }
    command_buffer.endRendering();
    resources.shadow_map->depth.transition_layout(
        command_buffer, vk::ImageLayout::eDepthStencilAttachmentOptimal,
        vk::ImageLayout::eDepthStencilReadOnlyOptimal, shadow_stages,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);

    // Preview goes directly to the single-sampled output: it needs neither the
    // main pass's depth/MSAA attachments nor any ray-query resources.
    vk::RenderingAttachmentInfo const color{
        .imageView = *target.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    command_buffer.beginRendering(
        vk::RenderingInfo{.renderArea = {.extent = target.extent}, .layerCount = 1}
            .setColorAttachments(color));
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, preview_pipeline_.raii());
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, preview_pipeline_.layout(),
                                      0, *resources.shadow_map->globals.raii(), nullptr);
    command_buffer.setViewport(0, vk::Viewport{.y = static_cast<float>(target.extent.height),
                                               .width = static_cast<float>(target.extent.width),
                                               .height = -static_cast<float>(target.extent.height),
                                               .minDepth = 0.0F,
                                               .maxDepth = 1.0F});
    command_buffer.setScissor(0, vk::Rect2D{.extent = target.extent});
    command_buffer.draw(3, 1, 0, 0);
    command_buffer.endRendering();
}

} // namespace lc1
