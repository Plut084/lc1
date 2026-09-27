#pragma once

#include "lc1/vk/buffer.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/one-time-submit.hpp"
#include "lc1/vk/vertex.hpp"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <span>

namespace lc1 {

class Device;

vk::IndexType index_type_for_size(std::size_t size);

// Something drawable: vertex data on the GPU plus how many vertices to draw.
// A resource, loaded once and shared -- a hundred soldiers are a hundred
// DrawItems pointing at one Mesh.
class Mesh {
  public:
    // Vertex data lives in DEVICE_LOCAL memory, which the CPU usually cannot map.
    // So it goes through a staging buffer: the CPU writes a host-visible copy, and
    // the GPU copies that into the device-local one.
    template <std::unsigned_integral T>
    Mesh(Device const &device, std::span<Vertex const> vertices, std::span<T const> indices)
        : vertices_{device, vertices.size_bytes(),
                    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst},
          vertex_count_{static_cast<std::uint32_t>(vertices.size())},
          indices_{device, indices.size_bytes(),
                   vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst},
          index_count_(static_cast<std::uint32_t>(indices.size())),
          index_type_(index_type_for_size(sizeof(T)))
    {
        // Only needed until the copy has finished; one_time_submit waits for that,
        // so it can die at the end of this constructor.
        Buffer const staging_vertices{device, vertices.size_bytes(),
                                      vk::BufferUsageFlagBits::eTransferSrc,
                                      vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
        staging_vertices.upload(std::as_bytes(vertices));

        Buffer const staging_indices{device, indices.size_bytes(),
                                     vk::BufferUsageFlagBits::eTransferSrc,
                                     vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
        staging_indices.upload(std::as_bytes(indices));

        one_time_submit(device, [this, &staging_vertices, &vertices, &staging_indices,
                                 &indices](vk::raii::CommandBuffer const &command_buffer) {
            command_buffer.copyBuffer(*staging_vertices.raii(), *vertices_.raii(),
                                      vk::BufferCopy{.size = vertices.size_bytes()});

            // The fence wait in one_time_submit only tells the CPU the copy is
            // done; it does not make the GPU's write visible to later GPU reads.
            // This barrier does: its second scope covers every later command on
            // the queue, including the vertex fetch in the frame loop's submits.
            vk::BufferMemoryBarrier2 barrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eVertexAttributeInput,
                .dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead,
                .buffer = *vertices_.raii(),
                .offset = 0,
                .size = vk::WholeSize,
            };
            vk::DependencyInfo dependency;
            dependency.setBufferMemoryBarriers(barrier);
            command_buffer.pipelineBarrier2(dependency);

            command_buffer.copyBuffer(*staging_indices.raii(), *indices_.raii(),
                                      vk::BufferCopy{.size = indices.size_bytes()});

            vk::BufferMemoryBarrier2 barrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eIndexInput,
                .dstAccessMask = vk::AccessFlagBits2::eIndexRead,
                .buffer = *indices_.raii(),
                .offset = 0,
                .size = vk::WholeSize,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setBufferMemoryBarriers(barrier2));
        });

        spdlog::info("[Mesh] loaded {} vertices, {} indices", vertex_count_, index_count_);
    }

    template <std::ranges::contiguous_range Container>
        requires std::unsigned_integral<std::ranges::range_value_t<Container>>
    Mesh(Device const &device, std::span<Vertex const> vertices, Container const &indices)
        : Mesh(device, vertices, std::span(indices))
    {
    }

    // Binds the vertex buffer and draws. The pipeline must already be bound.
    void draw(vk::raii::CommandBuffer const &command_buffer) const;

  private:
    Buffer vertices_;
    std::uint32_t vertex_count_;
    Buffer indices_;
    std::uint32_t index_count_;
    vk::IndexType index_type_;
};

// One entry of "what to draw this frame", rebuilt every frame. Non-owning: the
// Mesh must outlive the frame that draws it.
struct DrawItem {
    Mesh const *mesh = nullptr;
};

} // namespace lc1
