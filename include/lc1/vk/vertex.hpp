#pragma once

#include "lc1/vk/common.hpp"

#include <glm/glm.hpp>

#include <array>

namespace lc1 {

struct Vertex {
    glm::vec2 position;
    glm::vec3 color;

    static vk::VertexInputBindingDescription binding_description()
    {
        return {.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
    }

    static std::array<vk::VertexInputAttributeDescription, 2> attribute_descriptions()
    {
        return {{{.location = 0,
                  .binding = 0,
                  .format = vk::Format::eR32G32Sfloat,
                  .offset = offsetof(Vertex, position)},
                 {.location = 1,
                  .binding = 0,
                  .format = vk::Format::eR32G32B32Sfloat,
                  .offset = offsetof(Vertex, color)}}};
    }
};

} // namespace lc1
