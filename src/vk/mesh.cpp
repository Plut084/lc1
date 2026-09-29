#include "lc1/vk/mesh.hpp"
#include <print>

namespace lc1 {

void Mesh::draw(vk::raii::CommandBuffer const &command_buffer) const
{
    command_buffer.bindVertexBuffers(0, *vertices_.raii(), vk::DeviceSize{0});
    command_buffer.bindIndexBuffer(*indices_.raii(), vk::DeviceSize{0}, index_type_);
    spdlog::debug("[Mesh] drawing {} vertices, {} indices", vertex_count_, index_count_);
    command_buffer.drawIndexed(index_count_, 1, 0, 0, 0);
}

} // namespace lc1
