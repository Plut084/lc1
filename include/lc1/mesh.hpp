#pragma once

#include "lc1/vk/vertex.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace lc1 {

class Mesh {
    friend class Model;

  public:
    Mesh(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices)
        : vertices_(std::move(vertices)), indices_(std::move(indices))
    {
    }

  private:
    Mesh() = default;

    std::vector<Vertex> vertices_;
    std::vector<std::uint32_t> indices_;
    // Matetial material_;
};

} // namespace lc1
