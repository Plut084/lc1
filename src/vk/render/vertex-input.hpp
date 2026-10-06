#pragma once

#include "lc1/vertex.hpp"
#include "lc1/vk/core/common.hpp"

#include <array>
#include <cstddef>

namespace lc1::detail {

inline constexpr vk::VertexInputBindingDescription2EXT vertex_binding_description()
{
    return {.binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = vk::VertexInputRate::eVertex,
            .divisor = 1};
}

inline constexpr std::array<vk::VertexInputAttributeDescription2EXT, 5>
vertex_attribute_descriptions()
{
    return {{
        {.location = 0,
         .binding = 0,
         .format = vk::Format::eR32G32B32Sfloat,
         .offset = offsetof(Vertex, position)},
        {.location = 1,
         .binding = 0,
         .format = vk::Format::eR32G32B32Sfloat,
         .offset = offsetof(Vertex, normal)},
        {.location = 2,
         .binding = 0,
         .format = vk::Format::eR32G32B32Sfloat,
         .offset = offsetof(Vertex, color)},
        {.location = 3,
         .binding = 0,
         .format = vk::Format::eR32G32Sfloat,
         .offset = offsetof(Vertex, uv)},
        {.location = 4,
         .binding = 0,
         .format = vk::Format::eR32G32B32A32Sfloat,
         .offset = offsetof(Vertex, tangent)},
    }};
}

} // namespace lc1::detail
