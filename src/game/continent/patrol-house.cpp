#include "layout.hpp"

#include "lc1/game/continent/continent.hpp"
#include "lc1/game/continent/patrol-house.hpp"

namespace lc1 {

void add_patrol_house(ContinentData &data)
{
    auto block = [&](glm::vec3 min, glm::vec3 max, ContinentSurface surface,
                     bool walkable = false) {
        data.blocks.push_back({min, max, surface, true, walkable});
    };
    auto slab = [&](float x0, float z0, float x1, float z1, float top) {
        block({x0, top - 0.2F, z0}, {x1, top, z1}, ContinentSurface::Wood, true);
    };

    // Foundation and three entrance steps. The east edge meets the gentle hillside.
    block({28, -1, 14}, {50, 0.6F, 42}, ContinentSurface::Stone, true);
    for (int i = 0; i < 3; ++i)
        block({26.5F + i * 0.5F, 0, 32}, {27.0F + i * 0.5F, (i + 1) * 0.2F, 36},
              ContinentSurface::Stone, true);

    // North/back wall and west wall with a full-height ground-floor entrance.
    block({28, 0.6F, 14}, {50, 10.5F, 14.4F}, ContinentSurface::Plaster);
    block({28, 0.6F, 14.4F}, {28.4F, 10.5F, 32}, ContinentSurface::Plaster);
    block({28, 0.6F, 36}, {28.4F, 10.5F, 42}, ContinentSurface::Plaster);
    block({28, 3.4F, 32}, {28.4F, 10.5F, 36}, ContinentSurface::Wood);

    // Cutaway camera-facing walls: retain low sills on every level and omit a roof.
    for (float top : {0.6F, 4.2F, 7.8F}) {
        block({49.6F, top, 14.4F}, {50, top + 0.9F, 42}, ContinentSurface::Plaster);
        block({28.4F, top, 41.6F}, {49.6F, top + 0.9F, 42}, ContinentSurface::Plaster);
    }

    // First floor surrounds the west stairwell; second floor has an east stairwell.
    slab(34, 14.4F, 49.6F, 41.6F, 4.2F);
    slab(28.4F, 14.4F, 34, 28, 4.2F);
    slab(28.4F, 36, 34, 41.6F, 4.2F);
    slab(28.4F, 14.4F, 43, 41.6F, 7.8F);
    slab(47, 14.4F, 49.6F, 41.6F, 7.8F);
    slab(43, 14.4F, 47, 26, 7.8F);
    slab(43, 36, 47, 41.6F, 7.8F);

    // Two 3.6 m flights, each with twelve 0.3 m risers and a 2 m clear width.
    for (int step = 0; step < 12; ++step) {
        float const a = step == 11 ? 4.2F : 0.6F + (step + 1) * 0.3F;
        float const z = 34.0F - (step + 1) * 0.5F;
        block({31, a - 0.2F, z}, {33, a, z + 0.5F}, ContinentSurface::Stone, true);
        float const b = step == 11 ? 7.8F : 4.2F + (step + 1) * 0.3F;
        float const other_z = 28.0F + step * 0.5F;
        block({44, b - 0.2F, other_z}, {46, b, other_z + 0.5F}, ContinentSurface::Stone, true);
    }
    slab(30, 26, 34, 28, 4.2F);
    slab(43, 34, 47, 36, 7.8F);

    // Ground-floor rooms, a doorway, and a raised sitting platform with two steps.
    block({39, 0.6F, 14.4F}, {39.4F, 3.8F, 24}, ContinentSurface::Plaster);
    block({39, 0.6F, 27}, {39.4F, 3.8F, 32}, ContinentSurface::Plaster);
    block({39, 3.2F, 24}, {39.4F, 3.8F, 27}, ContinentSurface::Wood);
    slab(39, 35, 46, 40, 1.2F);
    block({37, 0.6F, 36}, {38, 0.9F, 39}, ContinentSurface::Stone, true);
    block({38, 0.6F, 36}, {39, 1.2F, 39}, ContinentSurface::Stone, true);

    // Upper rooms are connected by a 3 m doorway, with a desk and a low cabinet.
    block({39, 4.2F, 14.4F}, {39.4F, 7.6F, 24}, ContinentSurface::Plaster);
    block({39, 4.2F, 27}, {39.4F, 7.6F, 41.6F}, ContinentSurface::Plaster);
    block({39, 6.8F, 24}, {39.4F, 7.6F, 27}, ContinentSurface::Wood);
    block({35, 4.2F, 17}, {38, 5.1F, 19}, ContinentSurface::Wood);
    block({34, 7.8F, 17}, {38, 8.7F, 19}, ContinentSurface::Wood);

    // Rails protect the open stairwell sides, while leaving the landings clear.
    block({33.7F, 4.2F, 29}, {34, 5.1F, 35.7F}, ContinentSurface::Wood);
    block({42.7F, 7.8F, 27}, {43, 8.7F, 33}, ContinentSurface::Wood);
    block({47, 7.8F, 27}, {47.3F, 8.7F, 33}, ContinentSurface::Wood);
}

std::vector<glm::vec3> patrol_house_route()
{
    std::vector<glm::vec3> route{
        {0, 0, 56},        {24, 0, 56},          {24, 0, 34},          patrol_house_spawn,
        {35, 0.6F, 34},    {35, 0.6F, 37.5F},    {37.5F, 0.9F, 37.5F}, {38.5F, 1.2F, 37.5F},
        {42, 1.2F, 37.5F}, {38.5F, 1.2F, 37.5F}, {37.5F, 0.9F, 37.5F}, {35, 0.6F, 37.5F},
        {35, 0.6F, 35},    {32, 0.6F, 35},
    };
    for (int i = 0; i < 12; ++i)
        route.push_back({32, i == 11 ? 4.2F : 0.6F + (i + 1) * 0.3F, 33.75F - i * 0.5F});
    route.insert(route.end(),
                 {{32, 4.2F, 27}, {36, 4.2F, 25.5F}, {41, 4.2F, 25.5F}, {45, 4.2F, 27}});
    for (int i = 0; i < 12; ++i)
        route.push_back({45, i == 11 ? 7.8F : 4.2F + (i + 1) * 0.3F, 28.25F + i * 0.5F});
    route.insert(route.end(), {{45, 7.8F, 35}, {40, 7.8F, 37}, {36, 7.8F, 30}, {36, 7.8F, 23}});
    auto const outward = route.size();
    for (std::size_t i = outward - 1; i > 1; --i)
        route.push_back(route[i - 1]);
    return route;
}

} // namespace lc1
