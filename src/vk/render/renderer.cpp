#include "lc1/vk/render/renderer.hpp"

#include "lc1/vk/core/command-buffer.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"
#include "lc1/vk/resources/shader-stages.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

Pipeline make_tone_map_pipeline(Device const &device, vk::Format output_format)
{
    auto const shader = ShaderModule::load_from_file(device, "shaders/tone-map.spv");
    ShaderStages stages;
    stages.append(vk::ShaderStageFlagBits::eVertex, shader, "vertMain");
    stages.append(vk::ShaderStageFlagBits::eFragment, shader, "fragMain");
    return {device,
            stages,
            {output_format},
            vk::Format::eUndefined,
            vk::SampleCountFlagBits::e1,
            PipelineKind::ToneMap};
}

vk::SampleCountFlagBits hdr_sample_count(Device const &device, vk::Format depth_format,
                                         vk::SampleCountFlagBits requested)
{
    auto const &physical = device.raii_physical();
    auto const hdr_features =
        physical.getFormatProperties(Renderer::hdr_format).optimalTilingFeatures;
    constexpr auto required =
        vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage;
    if ((hdr_features & required) != required)
        fail("RGBA16F color attachment and sampling are required for HDR rendering");
    auto const resolve = physical.getImageFormatProperties(
        Renderer::hdr_format, vk::ImageType::e2D, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferSrc,
        {});
    if (!(resolve.sampleCounts & vk::SampleCountFlagBits::e1))
        fail("single-sampled HDR resolve attachment is unsupported");
    auto const color = physical.getImageFormatProperties(
        Renderer::hdr_format, vk::ImageType::e2D, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        {});
    auto const depth = physical.getImageFormatProperties(
        depth_format, vk::ImageType::e2D, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment, {});
    auto const supported = color.sampleCounts & depth.sampleCounts;
    for (auto const count :
         {vk::SampleCountFlagBits::e4, vk::SampleCountFlagBits::e2, vk::SampleCountFlagBits::e1})
        if (count <= requested && (supported & count))
            return count;
    fail("no supported HDR/depth sample count");
}

constexpr std::array<std::uint8_t, 4> white_pixel{255, 255, 255, 255};
constexpr std::array<std::uint8_t, 4> normal_pixel{128, 128, 255, 255};

} // namespace

Renderer::Renderer(Device const &device, std::vector<vk::Format> const &color_attachment_formats,
                   vk::SampleCountFlagBits samples)
    : device_{device}, color_format_{single_color_format(color_attachment_formats)},
      depth_format_{find_depth_format(device)},
      samples_{hdr_sample_count(device, depth_format_, samples)},
      pipeline_{make_pipeline(device, {hdr_format}, depth_format_, samples_)},
      tone_map_pipeline_{make_tone_map_pipeline(device, color_format_)},
      ray_query_shadows_{device, depth_format_}, material_pool_(nullptr), sampler_(device),
      white_srgb_(device, {.width = 1, .height = 1}, white_pixel, TextureColorSpace::Srgb),
      white_linear_(device, {.width = 1, .height = 1}, white_pixel, TextureColorSpace::Linear),
      flat_normal_(device, {.width = 1, .height = 1}, normal_pixel, TextureColorSpace::Linear),
      // TODO: capacities here should be re-considered.
      descriptor_heap_{device, max_materials, max_materials, 100}
{
    std::array const pool_sizes{
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler,
                               .descriptorCount = 5 * max_materials},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer,
                               .descriptorCount = max_materials}};
    vk::DescriptorPoolCreateInfo pool_info{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = max_materials,
    };
    pool_info.setPoolSizes(pool_sizes);
    material_pool_ = device.raii().createDescriptorPool(pool_info);

    descriptor_heap_.probe();
}

FrameResources Renderer::make_frame_resources(std::uint32_t object_capacity) const
{
    if (object_capacity > 65534U) {
        fail("object capacity exceeds 16-bit temporal surface IDs");
    }
    // Reserve sets for the lazily created preview and HDR tone mapping.
    std::array const pool_sizes{
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer,
                               .descriptorCount = object_capacity + 5},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampledImage, .descriptorCount = 6},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eAccelerationStructureKHR,
                               .descriptorCount = 1},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler,
                               .descriptorCount = 1},
    };
    vk::DescriptorPoolCreateInfo pool_info{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = object_capacity + 3,
    };
    pool_info.setPoolSizes(pool_sizes);
    auto frame_descriptor_pool = device_.raii().createDescriptorPool(pool_info);

    DescriptorSet globals{device_, frame_descriptor_pool, pipeline_.frame_set_layout(),
                          Pipeline::frame_bindings};
    globals.add_uniform(0, sizeof(CameraData));
    globals.add_uniform(1, sizeof(LightsData));
    globals.add_uniform(6, sizeof(TemporalShadowData));
    std::vector<DescriptorSet> objects;
    objects.reserve(object_capacity);
    for (std::uint32_t i = 0; i < object_capacity; ++i) {
        objects.emplace_back(device_, frame_descriptor_pool, pipeline_.object_set_layout(),
                             Pipeline::object_bindings);
        objects.back().add_uniform(0, sizeof(ObjectData));
    }
    return FrameResources{std::move(frame_descriptor_pool), std::move(globals), std::move(objects)};
}

GpuMaterial Renderer::make_material(GpuTexture const &texture)
{
    return make_material(
        MaterialInfo{.parameters = {.metallic_factor = 0.0F, .roughness_factor = 0.8F},
                     .base_color = {.texture = &texture}});
}

GpuMaterial Renderer::make_material(MaterialInfo const &info)
{
    if (live_materials_ >= max_materials)
        fail("material capacity exhausted ({} live materials)", max_materials);
    auto const &p = info.parameters;
    auto unit = [](float value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
    if (!unit(p.base_color_factor.r) || !unit(p.base_color_factor.g) ||
        !unit(p.base_color_factor.b) || !unit(p.base_color_factor.a) || !unit(p.metallic_factor) ||
        !unit(p.roughness_factor) || !unit(p.occlusion_strength) ||
        !std::isfinite(p.normal_scale) || p.normal_scale < 0.0F ||
        !std::isfinite(p.emissive_factor.r) || !std::isfinite(p.emissive_factor.g) ||
        !std::isfinite(p.emissive_factor.b) ||
        glm::any(glm::lessThan(p.emissive_factor, glm::vec3{0})))
        fail("invalid PBR material factors");
    if (info.occlusion.texture)
        fail("occlusion textures require the IBL milestone");
    std::array const textures{info.base_color, info.metallic_roughness, info.normal, info.occlusion,
                              info.emissive};
    std::array const fallbacks{&white_srgb_, &white_linear_, &flat_normal_, &white_linear_,
                               &white_srgb_};
    // Validate before allocating a descriptor set, including slots not yet sampled.
    for (std::size_t i = 0; i < textures.size(); ++i)
        if (textures[i].texture &&
            textures[i].texture->color_space() != fallbacks[i]->color_space())
            fail("PBR texture slot {} has the wrong color space", i);
    try {
        DescriptorSet set{device_, material_pool_, pipeline_.material_set_layout(),
                          Pipeline::material_bindings};
        set.add_uniform(1, sizeof(MaterialData));
        MaterialData const data{
            p.base_color_factor,
            glm::vec4{p.emissive_factor, 0.0F},
            {p.metallic_factor, p.roughness_factor, p.normal_scale, p.occlusion_strength},
            {info.normal.texture ? 1U : 0U, 0, 0, 0}};
        set.upload(1, std::as_bytes(std::span{&data, 1}));
        constexpr std::array bindings{0U, 2U, 3U, 4U, 5U};
        for (std::size_t i = 0; i < textures.size(); ++i) {
            auto const *texture = textures[i].texture ? textures[i].texture : fallbacks[i];
            auto const *sampler = textures[i].sampler ? textures[i].sampler : &sampler_;
            set.set_image(bindings[i], {.sampler = *sampler->raii(),
                                        .imageView = *texture->image().view(),
                                        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal});
        }
        return GpuMaterial{std::move(set), live_materials_, info.normal.texture != nullptr};
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
        resources.color_image.emplace(device_, hdr_format, extent, 1, samples_,
                                      vk::ImageUsageFlagBits::eColorAttachment |
                                          vk::ImageUsageFlagBits::eTransientAttachment,
                                      vk::ImageAspectFlagBits::eColor);
    }
    if (!resources.hdr_image || resources.hdr_image->extent() != extent) {
        // The caller waited this slot's fence; retire its old set before its image.
        resources.tone_map.reset();
        resources.hdr_image.emplace(device_, hdr_format, extent, 1, vk::SampleCountFlagBits::e1,
                                    vk::ImageUsageFlagBits::eColorAttachment |
                                        vk::ImageUsageFlagBits::eSampled |
                                        vk::ImageUsageFlagBits::eTransferSrc,
                                    vk::ImageAspectFlagBits::eColor);
        resources.tone_map.emplace(device_, resources.descriptor_pool,
                                   tone_map_pipeline_.frame_set_layout(),
                                   Pipeline::tone_map_bindings);
        resources.tone_map->add_uniform(0, sizeof(ToneMapData));
        resources.tone_map->set_image(1, {.imageView = *resources.hdr_image->view(),
                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal});
    }
}

void Renderer::record(CommandBuffer &command_buffer_1, FrameResources &resources,
                      RenderTarget const &target, scene::FpsCamera const &camera,
                      std::span<scene::Light const> lights, std::span<DrawItem const> draws,
                      OutputSettings output)
{
    auto &command_buffer = command_buffer_1.raii();

    if (!std::isfinite(output.exposure) || output.exposure <= 0.0F)
        fail("exposure must be finite and positive");
    if (output.encoding == OutputEncoding::Srgb && color_format_ != vk::Format::eR8G8B8A8Unorm &&
        color_format_ != vk::Format::eB8G8R8A8Unorm)
        fail("manual sRGB output requires an RGBA8/BGRA8 UNORM target");
    ray_query_shadows_.record(command_buffer, resources, target.extent, camera, lights, draws);
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

    // Preserve the dependency on prior shader reads when this frame slot is reused.
    resources.hdr_image->transition_layout(
        command_buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    vk::RenderingAttachmentInfo color_attachment{
        .imageView = *resources.hdr_image->view(),
        // The HDR barrier above establishes this layout before rendering.
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
        color_attachment.resolveImageView = *resources.hdr_image->view();
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
                                      *resources.ray_query.globals.raii(), nullptr);
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
    resources.hdr_image->transition_layout(
        command_buffer, vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
    tone_map(command_buffer, resources, target, output);
}

void Renderer::record(CommandBuffer &command_buffer, FrameResources &resources,
                      RenderTarget const &target, scene::Scene const &scene, OutputSettings output)
{
    command_buffer.bind_descriptor_heap(descriptor_heap_);

    auto const *camera = scene.active_camera();
    if (!camera)
        fail("cannot render a scene without an active camera");
    record(command_buffer, resources, target, *camera, scene.lights(), scene.draws(), output);
}

void Renderer::record_shadow_map_preview(vk::raii::CommandBuffer const &command_buffer,
                                         FrameResources &resources, RenderTarget const &target,
                                         std::span<scene::Light const> lights,
                                         std::span<DrawItem const> draws,
                                         ShadowMapRegion const &region)
{
    ray_query_shadows_.invalidate_history();
    if (!shadow_map_preview_)
        shadow_map_preview_.emplace(device_, color_format_);
    shadow_map_preview_->record(command_buffer, resources, target, lights, draws, region);
}

} // namespace lc1
