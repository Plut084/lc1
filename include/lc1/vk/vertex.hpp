#pragma once

#include "lc1/vk/common.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include <vector>

namespace lc1 {

struct Vertex {
    glm::vec3 position;
    glm::vec2 uv; // texture coordinate

    friend constexpr bool operator==(Vertex const &lhs, Vertex const &rhs) = default;

    static vk::VertexInputBindingDescription binding_description()
    {
        return {.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
    }

    static std::vector<vk::VertexInputAttributeDescription> attribute_descriptions()
    {
        return {{
            {.location = 0,
             .binding = 0,
             .format = vk::Format::eR32G32B32Sfloat,
             .offset = offsetof(Vertex, position)},
            {.location = 1,
             .binding = 0,
             .format = vk::Format::eR32G32Sfloat,
             .offset = offsetof(Vertex, uv)},
        }};
    }
};

} // namespace lc1

namespace std {
template <> struct hash<lc1::Vertex> {
    size_t operator()(lc1::Vertex const &vertex) const
    {
        return std::hash<glm::vec3>{}(vertex.position) ^ (std::hash<glm::vec2>{}(vertex.uv) << 1);
    }
};
} // namespace std
