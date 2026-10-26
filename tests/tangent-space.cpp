#include "lc1/tangent-space.hpp"
#include "lc1/error.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool condition, char const *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
bool near(glm::vec3 a, glm::vec3 b)
{
    return glm::length(a - b) < 1e-5F;
}

std::vector<lc1::Vertex> quad()
{
    return {{.position = {0, 0, 0}, .normal = {0, 0, 1}, .uv = {0, 0}},
            {.position = {1, 0, 0}, .normal = {0, 0, 1}, .uv = {1, 0}},
            {.position = {1, 1, 0}, .normal = {0, 0, 1}, .uv = {1, 1}},
            {.position = {0, 1, 0}, .normal = {0, 0, 1}, .uv = {0, 1}}};
}

void basic_frame()
{
    auto vertices = quad();
    std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};
    check(!lc1::valid_tangent_frame(vertices[0]), "default tangent is absent");
    lc1::generate_tangents(vertices, indices);
    check(vertices.size() == 4 && indices.size() == 6,
          "continuous planar chart should stay welded");
    for (auto const &vertex : vertices) {
        check(lc1::valid_tangent_frame(vertex), "generated tangent must be valid");
        check(near(glm::vec3{vertex.tangent}, {1, 0, 0}) && vertex.tangent.w == 1,
              "UV axes define +X tangent and +Y bitangent");
    }
    vertices = quad();
    for (auto &vertex : vertices)
        vertex.uv = {vertex.position.y, -vertex.position.x};
    lc1::generate_tangents(vertices, indices);
    check(near(glm::vec3{vertices[0].tangent}, {0, 1, 0}) && vertices[0].tangent.w == 1,
          "rotated UVs rotate the tangent");
}

void mirrored_chart()
{
    std::vector<lc1::Vertex> vertices{{.position = {0, 0, 0}, .normal = {0, 0, 1}, .uv = {0, 0}},
                                      {.position = {1, 0, 0}, .normal = {0, 0, 1}, .uv = {1, 0}},
                                      {.position = {0, 1, 0}, .normal = {0, 0, 1}, .uv = {0, 1}},
                                      {.position = {-1, 0, 0}, .normal = {0, 0, 1}, .uv = {1, 0}}};
    std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};
    lc1::generate_tangents(vertices, indices);
    check(vertices.size() == 6 && indices[0] != indices[3] && indices[2] != indices[4],
          "mirrored UV seam must split shared corners");
    auto const &left = vertices[indices[0]];
    auto const &right = vertices[indices[3]];
    check(near(glm::vec3{left.tangent}, {1, 0, 0}) && left.tangent.w == 1, "first chart tangent");
    check(near(glm::vec3{right.tangent}, {-1, 0, 0}) && right.tangent.w == -1,
          "mirrored chart tangent and sign");
    check(near(glm::cross(right.normal, glm::vec3{right.tangent}) * right.tangent.w, {0, 1, 0}),
          "mirrored chart preserves its bitangent direction");
}

void invalid_input()
{
    auto rejects = [](std::vector<lc1::Vertex> vertices, std::vector<std::uint32_t> indices) {
        auto const original_indices = indices;
        bool rejected = false;
        try {
            lc1::generate_tangents(vertices, indices);
        }
        catch (lc1::Error const &) {
            rejected = true;
        }
        check(rejected && indices == original_indices,
              "invalid input must fail before modifying indices");
        for (auto const &vertex : vertices)
            check(vertex.tangent.w == 0, "failed generation modified tangents");
    };
    auto vertices = quad();
    for (auto &vertex : vertices)
        vertex.uv = {0, 0};
    rejects(vertices, {0, 1, 2});
    vertices = quad();
    vertices[1].position = vertices[0].position;
    rejects(vertices, {0, 1, 2});
    vertices = quad();
    vertices[0].normal = {0, 0, 0};
    rejects(vertices, {0, 1, 2});
    vertices = quad();
    vertices[0].uv.x = std::numeric_limits<float>::quiet_NaN();
    rejects(vertices, {0, 1, 2});
    rejects(quad(), {0, 1, 99});
    rejects(quad(), {0, 1});
    rejects(quad(), {});
    auto vertex = quad()[0];
    vertex.tangent = {0, 0, 1, 1};
    check(!lc1::valid_tangent_frame(vertex), "parallel tangent and normal must fail");
    vertex.tangent = {1, 0, 0, 0.5F};
    check(!lc1::valid_tangent_frame(vertex), "invalid handedness must fail");
}
} // namespace

int main()
{
    try {
        basic_frame();
        mirrored_chart();
        invalid_input();
        std::cout << "MikkTSpace frames, mirrored seams and invalid input passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
