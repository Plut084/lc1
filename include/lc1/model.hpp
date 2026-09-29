#pragma once

#include "lc1/vk/gpu-mesh.hpp"

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

        auto position_of = [&](tinyobj::index_t const &index) {
            return glm::vec3{attrib.vertices[3 * index.vertex_index],
                             attrib.vertices[3 * index.vertex_index + 1],
                             attrib.vertices[3 * index.vertex_index + 2]};
        };
        // LoadObj triangulates by default. Missing normals use flat triangle normals;
        // including normals in Vertex equality preserves authored hard edges.
        for (auto const &shape : shapes) {
            for (std::size_t i = 0; i + 2 < shape.mesh.indices.size(); i += 3) {
                auto const face_normal = glm::cross(
                    position_of(shape.mesh.indices[i + 1]) - position_of(shape.mesh.indices[i]),
                    position_of(shape.mesh.indices[i + 2]) - position_of(shape.mesh.indices[i]));
                float const face_length = glm::length(face_normal);
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    auto const &index = shape.mesh.indices[i + corner];
                    Vertex vertex{.position = position_of(index), .uv = {0.0F, 0.0F}};
                    if (index.texcoord_index >= 0)
                        vertex.uv = {attrib.texcoords[2 * index.texcoord_index],
                                     attrib.texcoords[2 * index.texcoord_index + 1]};
                    if (face_length > 0.0F)
                        vertex.normal = face_normal / face_length;
                    if (index.normal_index >= 0) {
                        glm::vec3 const normal{attrib.normals[3 * index.normal_index],
                                               attrib.normals[3 * index.normal_index + 1],
                                               attrib.normals[3 * index.normal_index + 2]};
                        float const length = glm::length(normal);
                        if (length > 0.0F)
                            vertex.normal = normal / length;
                    }
                    auto [it, inserted] = index_of_vertex.insert({vertex, model.vertices_.size()});
                    if (inserted)
                        model.vertices_.push_back(vertex);
                    model.indices_.push_back(it->second);
                }
            }
        }

        return model;
    }

    Model(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices)
        : vertices_(std::move(vertices)), indices_(std::move(indices))
    {
    }

    GpuMesh gen_mesh(Device const &device) const { return {device, vertices_, indices_}; }

  private:
    Model() = default;

    std::vector<Vertex> vertices_;
    std::vector<std::uint32_t> indices_;
};

} // namespace lc1
