#pragma once

#include "lc1/vertex.hpp"
#include "lc1/vk/core/common.hpp"

#include <cstddef>
#include <vector>

namespace lc1::detail {

inline vk::VertexInputBindingDescription vertex_binding_description()
{
    return {.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
}

inline std::vector<vk::VertexInputAttributeDescription> vertex_attribute_descriptions()
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
