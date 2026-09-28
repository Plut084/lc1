#include "lc1/vk/mesh.hpp"
#include <print>

namespace lc1 {

vk::IndexType index_type_for_size(std::size_t size)
{
    switch (size) {
    case sizeof(std::uint8_t):
        return vk::IndexType::eUint8;
    case sizeof(std::uint16_t):
        return vk::IndexType::eUint16;
    case sizeof(std::uint32_t):
        return vk::IndexType::eUint32;
    default:
        fail("index_type_for_size: unsupported index size {}", size);
    };
}

void Mesh::draw(vk::raii::CommandBuffer const &command_buffer) const
{
    command_buffer.bindVertexBuffers(0, *vertices_.raii(), vk::DeviceSize{0});
    command_buffer.bindIndexBuffer(*indices_.raii(), vk::DeviceSize{0}, index_type_);
    spdlog::debug("[Mesh] drawing {} vertices, {} indices", vertex_count_, index_count_);
    command_buffer.drawIndexed(index_count_, 1, 0, 0, 0);
}

} // namespace lc1
