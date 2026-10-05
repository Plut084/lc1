#include "lc1/vk/render/renderer.hpp"

namespace lc1 {

void Renderer::tone_map(vk::raii::CommandBuffer const &commands, FrameResources &resources,
                        RenderTarget const &target, OutputSettings output) const
{
    ToneMapData const data{
        {output.exposure, output.encoding == OutputEncoding::Srgb ? 1.0F : 0.0F, 0.0F, 0.0F}};
    resources.tone_map->upload(0, std::as_bytes(std::span{&data, 1}));
    vk::RenderingAttachmentInfo const attachment{
        .imageView = *target.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    vk::RenderingInfo rendering{.renderArea = {.offset = {.x = 0, .y = 0}, .extent = target.extent},
                                .layerCount = 1};
    rendering.setColorAttachments(attachment);
    commands.beginRendering(rendering);
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, tone_map_pipeline_.raii());
    commands.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, tone_map_pipeline_.layout(), 0,
                                *resources.tone_map->raii(), nullptr);
    commands.setViewport(0, vk::Viewport{.x = 0.0F,
                                         .y = 0.0F,
                                         .width = static_cast<float>(target.extent.width),
                                         .height = static_cast<float>(target.extent.height),
                                         .minDepth = 0.0F,
                                         .maxDepth = 1.0F});
    commands.setScissor(0, vk::Rect2D{.offset = {.x = 0, .y = 0}, .extent = target.extent});
    commands.setFrontFace(vk::FrontFace::eCounterClockwise);
    commands.draw(3, 1, 0, 0);
    commands.endRendering();
}

} // namespace lc1
