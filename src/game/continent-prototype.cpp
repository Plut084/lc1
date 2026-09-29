#include "lc1/game/continent.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lc1 {

Continent Continent::make_prototype()
{
    constexpr RegionId plain{1}, forest{2}, mountain{3}, wasteland{4}, sea{5};
    constexpr LocationId city{1}, village{2}, fortress{3}, port{4}, ruins{5}, camp{6};
    ContinentData data{
        .origin = {-384.0F, -576.0F},
        .tile_size = 16.0F,
        .tiles = std::vector(56, std::vector<TileData>(48)),
        .spawn = {0.0F, 0.0F, 32.0F},
        .patches = {},
        .blocks = {},
        .regions = {{plain, "长风平原", RegionType::Plain},
                    {forest, "东境森林", RegionType::Forest},
                    {mountain, "北境山脉", RegionType::Mountain},
                    {wasteland, "赤土荒原", RegionType::Wasteland},
                    {sea, "西海", RegionType::Sea}},
        .locations =
            {
                {city, "长风城", LocationType::City, plain, {{-160, -384}, {160, 0}}, {0, 0}},
                {village, "南田村", LocationType::Village, plain, {{-64, 80}, {64, 160}}, {0, 112}},
                {fortress,
                 "北山要塞",
                 LocationType::Fortress,
                 plain,
                 {{-240, -448}, {-176, -400}},
                 {-208, -400}},
                {port, "西岸港", LocationType::Port, plain, {{-288, 64}, {-224, 128}}, {-240, 96}},
                {ruins,
                 "赤土遗迹",
                 LocationType::Ruins,
                 wasteland,
                 {{80, 208}, {144, 272}},
                 {112, 224}},
                {camp,
                 "林边营地",
                 LocationType::Wilderness,
                 forest,
                 {{208, 32}, {272, 96}},
                 {224, 64}},
            },
        .roads =
            {
                {RoadId{1}, "城南大道", city, village, 8, {{0, 0}, {0, 112}}},
                {RoadId{2}, "西港商路", village, port, 6, {{0, 112}, {-112, 112}, {-240, 96}}},
                {RoadId{3}, "北山军道", port, fortress, 6, {{-240, 96}, {-208, 48}, {-208, -400}}},
                {RoadId{4}, "林间小径", village, camp, 4, {{0, 112}, {96, 112}, {224, 64}}},
                {RoadId{5},
                 "荒原古道",
                 village,
                 ruins,
                 4,
                 {{0, 112}, {0, 208}, {64, 208}, {112, 224}}},
            },
    };
    for (std::size_t z = 0; z < data.tiles.size(); ++z) {
        for (std::size_t x = 0; x < data.tiles[z].size(); ++x) {
            glm::vec2 const position =
                data.origin +
                glm::vec2{static_cast<float>(x), static_cast<float>(z)} * data.tile_size;
            data.tiles[z][x].region = position.x < -288                       ? sea
                                      : position.y < -448                     ? mountain
                                      : position.x >= 192                     ? forest
                                      : position.y >= 192 && position.x >= 32 ? wasteland
                                                                              : plain;
        }
    }
    auto patch = [&](glm::vec2 min, glm::vec2 max, ContinentSurface surface) {
        data.patches.push_back({{min, max}, surface});
    };
    auto block = [&](glm::vec3 min, glm::vec3 max, ContinentSurface surface, bool solid = true) {
        data.blocks.push_back({min, max, surface, solid});
    };

    // A 320 x 384 metre city, with an open twelve-metre gate and a continuous main road.
    patch({-160.0F, -384.0F}, {160.0F, 0.0F}, ContinentSurface::Stone);
    patch({-8.0F, -380.0F}, {8.0F, 0.0F}, ContinentSurface::Road);
    patch({-30.0F, -220.0F}, {30.0F, -164.0F}, ContinentSurface::Road);
    for (float z = -64.0F; z > -384.0F; z -= 64.0F)
        patch({-156.0F, z - 6.0F}, {156.0F, z + 6.0F}, ContinentSurface::Road);
    for (float x : {-104.0F, -56.0F, 56.0F, 104.0F})
        patch({x - 5.0F, -380.0F}, {x + 5.0F, -4.0F}, ContinentSurface::Road);

    block({-160.0F, 0.0F, -4.0F}, {-6.0F, 10.0F, 0.0F}, ContinentSurface::Stone);
    block({6.0F, 0.0F, -4.0F}, {160.0F, 10.0F, 0.0F}, ContinentSurface::Stone);
    block({-6.0F, 6.0F, -4.0F}, {6.0F, 12.0F, 0.0F}, ContinentSurface::Stone);
    block({-160.0F, 0.0F, -384.0F}, {160.0F, 10.0F, -380.0F}, ContinentSurface::Stone);
    block({-160.0F, 0.0F, -380.0F}, {-156.0F, 10.0F, -4.0F}, ContinentSurface::Stone);
    block({156.0F, 0.0F, -380.0F}, {160.0F, 10.0F, -4.0F}, ContinentSurface::Stone);
    for (float x : {-14.0F, 14.0F}) {
        block({x - 5.0F, 0.0F, -8.0F}, {x + 5.0F, 16.0F, 4.0F}, ContinentSurface::Stone);
        block({x - 5.5F, 16.0F, -8.5F}, {x + 5.5F, 17.0F, 4.5F}, ContinentSurface::Roof);
    }
    for (float x = -156.0F; x < 160.0F; x += 8.0F) {
        if (std::abs(x) >= 22.0F)
            block({x - 1.5F, 10.0F, -4.0F}, {x + 1.5F, 12.0F, 0.0F}, ContinentSurface::Stone);
    }
    // Facade markers are decorative; building interiors are not implemented yet.
    for (int row = 0; row < 6; ++row) {
        float const z = -32.0F - static_cast<float>(row) * 64.0F;
        for (float x : {-132.0F, -80.0F, -32.0F, 32.0F, 80.0F, 132.0F}) {
            if ((row == 2 || row == 3) && std::abs(x) < 40.0F)
                continue;
            float const height = 7.0F + static_cast<float>(row % 3) * 2.0F;
            block({x - 12.0F, 0.0F, z - 17.0F}, {x + 12.0F, height, z + 17.0F},
                  ContinentSurface::Plaster);
            block({x - 13.0F, height, z - 18.0F}, {x + 13.0F, height + 2.0F, z + 18.0F},
                  ContinentSurface::Roof);
            block({x - 1.2F, 0.0F, z + 17.0F}, {x + 1.2F, 2.5F, z + 17.08F}, ContinentSurface::Wood,
                  false);
            for (float offset : {-8.0F, -4.0F, 4.0F, 8.0F})
                block({x + offset - 0.7F, 3.5F, z + 17.0F}, {x + offset + 0.7F, 5.2F, z + 17.08F},
                      ContinentSurface::Window, false);
        }
    }
    block({-22.0F, 0.0F, -366.0F}, {22.0F, 18.0F, -340.0F}, ContinentSurface::Stone);
    block({-24.0F, 18.0F, -368.0F}, {24.0F, 21.0F, -338.0F}, ContinentSurface::Roof);

    // Village: a clear central lane, houses and fields on either side.
    for (float x : {-40.0F, 40.0F}) {
        for (float z : {92.0F, 140.0F}) {
            block({x - 8, 0, z - 7}, {x + 8, 5, z + 7}, ContinentSurface::Plaster);
            block({x - 9, 5, z - 8}, {x + 9, 7, z + 8}, ContinentSurface::Roof);
        }
        patch({x - 12, 166}, {x + 12, 188}, ContinentSurface::Road);
        for (float z = 168; z < 188; z += 4)
            block({x - 11, 0, z}, {x + 11, 0.4F, z + 1}, ContinentSurface::Foliage, false);
    }
    // Fortress at the foot of the mountains, with a south-facing gate.
    patch({-240, -448}, {-176, -400}, ContinentSurface::Stone);
    block({-240, 0, -448}, {-176, 8, -444}, ContinentSurface::Stone);
    block({-240, 0, -444}, {-236, 8, -400}, ContinentSurface::Stone);
    block({-180, 0, -444}, {-176, 8, -400}, ContinentSurface::Stone);
    block({-240, 0, -404}, {-214, 8, -400}, ContinentSurface::Stone);
    block({-202, 0, -404}, {-176, 8, -400}, ContinentSurface::Stone);
    for (float x : {-230.0F, -186.0F})
        block({x - 5, 0, -441}, {x + 5, 13, -431}, ContinentSurface::Stone);
    // Port: warehouse, quay and land-side wooden jetties. Water remains impassable.
    patch({-288, 64}, {-278, 128}, ContinentSurface::Wood);
    for (float z : {72.0F, 116.0F}) {
        patch({-278, z - 3}, {-246, z + 3}, ContinentSurface::Wood);
        block({-286, 0, z - 1}, {-284, 7, z + 1}, ContinentSurface::Wood);
    }
    block({-272, 0, 68}, {-250, 7, 84}, ContinentSurface::Plaster);
    block({-274, 7, 66}, {-248, 9, 86}, ContinentSurface::Roof);
    for (float x : {-260.0F, -254.0F, -248.0F})
        block({x, 0, 120}, {x + 4, 3, 124}, ContinentSurface::Wood);
    // Ruined pillars and broken walls leave the entrance and middle open.
    patch({88, 232}, {136, 264}, ContinentSurface::Stone);
    for (float x : {92.0F, 132.0F}) {
        for (float z : {236.0F, 260.0F})
            block({x - 2, 0, z - 2}, {x + 2, x < 100 ? 9 : 4, z + 2}, ContinentSurface::Stone);
    }
    block({90, 0, 264}, {104, 4, 267}, ContinentSurface::Stone);
    block({120, 0, 264}, {134, 2, 267}, ContinentSurface::Stone);
    // Forest campsite: canvas shelters, a fire ring and a supply stack.
    patch({216, 48}, {264, 88}, ContinentSurface::Road);
    for (float z : {52.0F, 80.0F})
        block({244, 0, z - 4}, {256, 4, z + 4}, ContinentSurface::Canvas);
    block({238, 0, 64}, {242, 0.5F, 68}, ContinentSurface::Stone);
    block({258, 0, 68}, {263, 2, 72}, ContinentSurface::Wood);

    for (std::size_t z = 0; z < data.tiles.size(); ++z) {
        for (std::size_t x = 0; x < data.tiles[z].size(); ++x) {
            glm::vec2 const center =
                data.origin +
                (glm::vec2{static_cast<float>(x), static_cast<float>(z)} + 0.5F) * data.tile_size;
            if (data.tiles[z][x].region == mountain) {
                float const height = 12.0F + static_cast<float>((x * 7 + z * 11) % 5) * 5.0F;
                block({center.x - 7, 0, center.y - 7}, {center.x + 7, height, center.y + 7},
                      ContinentSurface::Stone);
            }
            if (data.tiles[z][x].region != forest || (x + z) % 2 != 0)
                continue;
            bool const near_road = std::ranges::any_of(
                data.roads, [&](Road const &road) { return road.contains(center, 6.0F); });
            bool const near_location =
                std::ranges::any_of(data.locations, [&](Location const &place) {
                    return glm::all(glm::greaterThanEqual(center, place.bounds.min - 6.0F)) &&
                           glm::all(glm::lessThanEqual(center, place.bounds.max + 6.0F));
                });
            if (near_road || near_location)
                continue;
            block({center.x - 0.7F, 0, center.y - 0.7F}, {center.x + 0.7F, 6, center.y + 0.7F},
                  ContinentSurface::Wood);
            block({center.x - 3, 4, center.y - 3}, {center.x + 3, 10, center.y + 3},
                  ContinentSurface::Foliage);
        }
    }
    return Continent{std::move(data)};
}

} // namespace lc1
