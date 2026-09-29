#pragma once

#include "lc1/vk/mesh.hpp"

#include <tiny_obj_loader.h>

#include <filesystem>

namespace lc1 {

class Model {
  public:
    static Model load_from_file(std::filesystem::path const &path)
    {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn;
        std::string err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str())) {
            throw std::runtime_error(warn + err);
        }

        Model model;
        std::unordered_map<Vertex, std::uint32_t> index_of_vertex;

        for (auto const &shape : shapes) {
            for (auto const &index : shape.mesh.indices) {
                Vertex vertex{
                    .position =
                        {
                            attrib.vertices[3 * index.vertex_index + 0],
                            attrib.vertices[3 * index.vertex_index + 1],
                            attrib.vertices[3 * index.vertex_index + 2],

                        },
                    .uv =
                        {
                            attrib.texcoords[2 * index.texcoord_index + 0],
                            attrib.texcoords[2 * index.texcoord_index + 1],
                        },
                };
                auto [it, inserted] = index_of_vertex.insert({vertex, model.vertices_.size()});
                if (inserted)
                    model.vertices_.push_back(vertex);
                model.indices_.push_back(it->second);
            }
        }

        return model;
    }

    Model(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices)
        : vertices_(std::move(vertices)), indices_(std::move(indices))
    {
    }

    Mesh gen_mesh(Device const &device) const { return {device, vertices_, indices_}; }

  private:
    Model() = default;

    std::vector<Vertex> vertices_;
    std::vector<std::uint32_t> indices_;
};

} // namespace lc1
