#include "lc1/vk/core/command-buffer.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/render/frame-resources.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/render/ray-query-shadows.hpp"

namespace lc1 {

void RayQueryShadows::prepare_history(vk::raii::CommandBuffer const &commands,
                                      FrameResources &resources, vk::Extent2D extent)
{
    if (!history_[0] || history_[0]->visibility.extent() != extent) {
        // History is read by the FOLLOWING frame as well as written by its owner.
        // A single frame-slot fence is insufficient when replacing these images.
        if (history_[0])
            device_->wait_idle();
        history_valid_ = false;
        history_write_index_ = 0;
        for (auto &history : history_) {
            auto make_image = [&](vk::Format format) {
                return GpuImage{*device_,
                                format,
                                extent,
                                1,
                                vk::SampleCountFlagBits::e1,
                                vk::ImageUsageFlagBits::eColorAttachment |
                                    vk::ImageUsageFlagBits::eSampled,
                                vk::ImageAspectFlagBits::eColor};
            };
            history.emplace(HistoryFrame{make_image(vk::Format::eR32G32B32A32Uint),
                                         make_image(vk::Format::eR32G32B32A32Sfloat)});
            // The first frame binds both images, but history_valid prevents reading
            // their undefined contents. Establish the descriptors' promised layout.
            for (auto const *image : {&history->visibility, &history->surface})
                image->transition_layout(commands, vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eShaderReadOnlyOptimal,
                                         vk::PipelineStageFlagBits2::eNone, {},
                                         vk::PipelineStageFlagBits2::eFragmentShader,
                                         vk::AccessFlagBits2::eShaderSampledRead);
        }
    }
    auto const &previous = *history_[history_write_index_ ^ 1U];
    auto const &current = *history_[history_write_index_];
    resources.pass_data.history_visibility =
        resources.image_index(resources.history_visibility_index, previous.visibility);
    resources.pass_data.history_surface =
        resources.image_index(resources.history_surface_index, previous.surface);
    resources.pass_data.resolved_visibility =
        resources.image_index(resources.resolved_visibility_index, current.visibility);
}

void RayQueryShadows::resolve_history(CommandBuffer &wrapped, FrameResources &resources,
                                      vk::Extent2D extent) const
{
    auto const &commands = wrapped.raii();
    wrapped.push_data(std::span{&resources.pass_data, 1});
    auto const &current = *history_[history_write_index_];
    for (auto const *image : {&current.visibility, &current.surface})
        image->transition_layout(commands, vk::ImageLayout::eShaderReadOnlyOptimal,
                                 vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::PipelineStageFlagBits2::eFragmentShader |
                                     vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eShaderSampledRead |
                                     vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite);
    std::array const attachments{
        vk::RenderingAttachmentInfo{.imageView = *current.visibility.view(),
                                    .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                    .loadOp = vk::AttachmentLoadOp::eDontCare,
                                    .storeOp = vk::AttachmentStoreOp::eStore},
        vk::RenderingAttachmentInfo{.imageView = *current.surface.view(),
                                    .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                    .loadOp = vk::AttachmentLoadOp::eDontCare,
                                    .storeOp = vk::AttachmentStoreOp::eStore},
    };
    vk::RenderingInfo rendering{
        .renderArea = {.offset = {0, 0}, .extent = extent},
        .layerCount = 1,
    };
    rendering.setColorAttachments(attachments);
    commands.beginRendering(rendering);
    temporal_shaders_.bind(commands);
    set_graphics_state(commands, {.color_attachment_count = 2});
    commands.setViewportWithCount(vk::Viewport{.y = static_cast<float>(extent.height),
                                               .width = static_cast<float>(extent.width),
                                               .height = -static_cast<float>(extent.height),
                                               .minDepth = 0.0F,
                                               .maxDepth = 1.0F});
    commands.setScissorWithCount(vk::Rect2D{.offset = {0, 0}, .extent = extent});
    commands.draw(3, 1, 0, 0);
    commands.endRendering();
    for (auto const *image : {&current.visibility, &current.surface})
        image->transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::eShaderReadOnlyOptimal,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eFragmentShader,
                                 vk::AccessFlagBits2::eShaderSampledRead);
}

void RayQueryShadows::filter_history(CommandBuffer &wrapped, FrameResources &resources,
                                     vk::Extent2D extent) const
{
    auto const &commands = wrapped.raii();
    wrapped.push_data(std::span{&resources.pass_data, 1});
    // Raw visibility is dead after temporal resolve. Reuse it for the spatial
    // output; keep the unfiltered temporal history to avoid recursive blur.
    auto const &output = *resources.ray_query.visibility;
    output.transition_layout(
        commands, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    vk::RenderingAttachmentInfo const attachment{.imageView = *output.view(),
                                                 .imageLayout =
                                                     vk::ImageLayout::eColorAttachmentOptimal,
                                                 .loadOp = vk::AttachmentLoadOp::eDontCare,
                                                 .storeOp = vk::AttachmentStoreOp::eStore};
    commands.beginRendering(
        vk::RenderingInfo{.renderArea = {.extent = extent}, .layerCount = 1}.setColorAttachments(
            attachment));
    spatial_shaders_.bind(commands);
    set_graphics_state(commands);
    commands.setViewportWithCount(vk::Viewport{.y = static_cast<float>(extent.height),
                                               .width = static_cast<float>(extent.width),
                                               .height = -static_cast<float>(extent.height),
                                               .minDepth = 0.0F,
                                               .maxDepth = 1.0F});
    commands.setScissorWithCount(vk::Rect2D{.extent = extent});
    commands.draw(3, 1, 0, 0);
    commands.endRendering();
    output.transition_layout(
        commands, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
}
} // namespace lc1
