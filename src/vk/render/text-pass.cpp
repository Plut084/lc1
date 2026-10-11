#include "lc1/vk/render/text-pass.hpp"

#include "lc1/error.hpp"
#include "lc1/text/text-layout.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/render/frame-resources.hpp"
#include "lc1/vk/resources/command-buffer.hpp"
#include "lc1/vk/resources/gpu-font.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>

namespace lc1 {
namespace {

// Text is rebuilt from scratch every frame; a frame needing more than this is a
// bug, not a larger font.
constexpr std::size_t max_vertices = 1U << 16;

} // namespace

TextPass::TextPass(Device const &device, vk::SampleCountFlagBits samples)
    : device_{&device}, shaders_{device, "shaders/text.spv"},
      world_state_{
          .vertex_input = VertexInput::Mesh,
          .samples = samples,
          .depth_test = true,
          .blend = true,
      },
      screen_state_{
          .vertex_input = VertexInput::Mesh,
          .samples = vk::SampleCountFlagBits::e1,
          .depth_test = false,
          .blend = true,
      }
{
}

void TextPass::draw_world(CommandBuffer &commands, FrameResources &resources, GpuFont const &font,
                          std::span<WorldTextItem const> items, glm::mat4 const &view_projection)
{
    if (items.empty())
        return;
    auto const &atlas = font.atlas();
    vertices_.clear();
    for (auto const &item : items) {
        auto const metrics = text::measure_text(atlas, item.text);
        // `height` is the first line's ascent-to-descent box, so a line keeps the
        // size the caller asked for whatever the face's own em is.
        auto scale = item.height / atlas.line_height();
        if (item.max_width > 0.0F && metrics.width > 0.0F)
            scale = std::min(scale, item.max_width / metrics.width);
        text::append_text(vertices_, atlas, item.text,
                          {
                              .origin = item.center,
                              .scale = scale,
                              .color = item.color,
                              .center_x = true,
                          });
    }
    upload_and_draw(commands, resources, resources.world_text_vertices, font, view_projection,
                    world_state_);
}

void TextPass::draw_screen(CommandBuffer &commands, FrameResources &resources, vk::Extent2D extent,
                           GpuFont const &font, std::span<ScreenTextItem const> items)
{
    if (items.empty())
        return;
    auto const &atlas = font.atlas();
    vertices_.clear();
    for (auto const &item : items) {
        text::append_text(vertices_, atlas, item.text,
                          {
                              .origin = {item.position.x, item.position.y, 0.0F},
                              .scale = item.pixel_height / atlas.line_height(),
                              .color = item.color,
                          });
    }
    // Pixels with the origin at the bottom-left corner to clip space. The negative
    // height is what keeps `up` pointing up under this pass's positive viewport,
    // mirroring the scene's negative-height viewport over a y-up world.
    glm::mat4 transform{1.0F};
    transform[0][0] = 2.0F / static_cast<float>(extent.width);
    transform[1][1] = -2.0F / static_cast<float>(extent.height);
    transform[3][0] = -1.0F;
    transform[3][1] = 1.0F;
    upload_and_draw(commands, resources, resources.screen_text_vertices, font, transform,
                    screen_state_);
}

void TextPass::upload_and_draw(CommandBuffer &commands, FrameResources &resources,
                               std::optional<GpuBuffer> &buffer, GpuFont const &font,
                               glm::mat4 const &transform, GraphicsState const &state)
{
    auto const bytes = std::span<Vertex const>{vertices_}.size_bytes();
    if (bytes == 0)
        return;
    if (vertices_.size() > max_vertices)
        fail("text needs {} vertices, over the {} supported", vertices_.size(), max_vertices);

    if (!buffer || buffer->size() < bytes) {
        // The slot fence was waited before recording, so the previous buffer has
        // no uses left.
        buffer.emplace(*device_, bytes, vk::BufferUsageFlagBits2::eVertexBuffer,
                       vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
    }
    buffer->upload(std::as_bytes(std::span<Vertex const>{vertices_}));

    auto &heap = *resources.descriptor_heap;
    commands.bind_descriptor_heap(heap);
    PushData const push{
        .transform = transform,
        .atlas_index = resources.image_index(resources.font_atlas_index, font.texture().image()),
        .sampler_index = resources.sampler_index(resources.font_sampler_index, font.sampler_info()),
    };
    commands.push_data(std::span{&push, 1});

    shaders_.bind_to_command_buffer(commands);
    set_graphics_state(commands.raii(), state);
    commands.raii().bindVertexBuffers(0, *buffer->raii(), vk::DeviceSize{0});
    commands.raii().draw(static_cast<std::uint32_t>(vertices_.size()), 1, 0, 0);
}

} // namespace lc1
