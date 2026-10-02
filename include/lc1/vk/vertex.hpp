#pragma once

#include "lc1/vk/common.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include <vector>

namespace lc1 {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal{0.0F, 1.0F, 0.0F}; // Object-space unit surface normal.
    glm::vec3 color{1.0F};
    glm::vec2 uv; // texture coordinate
    // Object-space tangent; w is bitangent handedness (+/-1). Zero means absent.
    glm::vec4 tangent{0.0F};

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
};

} // namespace lc1

namespace std {
template <> struct hash<lc1::Vertex> {
    size_t operator()(lc1::Vertex const &vertex) const
    {
        return std::hash<glm::vec3>{}(vertex.position) ^ (std::hash<glm::vec2>{}(vertex.uv) << 1) ^
               (std::hash<glm::vec3>{}(vertex.color) << 2) ^
               (std::hash<glm::vec3>{}(vertex.normal) << 3) ^
               (std::hash<glm::vec4>{}(vertex.tangent) << 4);
    }
};
} // namespace std
