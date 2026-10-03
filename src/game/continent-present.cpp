#include "lc1/game/continent.hpp"

#include "continent-land.generated.hpp"
#include "continent-layout.hpp"

#include <utility>

namespace lc1 {

Continent Continent::make_present(std::span<ContinentBlock const> spawn_blocks)
{
    constexpr RegionId plain{1}, sea{5};
    constexpr LocationId city{1}, village{2}, shore{7};
    ContinentData data{
        // The source canvas is 2400 x 1600. Scale by 2.56 m per source unit:
        // the south-bay city gate at (1250, 1050) becomes world (0, 0).
        .origin = {-3200.0F, -2688.0F},
        .tile_size = 16.0F,
        .tiles =
            std::vector(continent_land::rows, std::vector(continent_land::columns, TileData{sea})),
        .spawn = {0.0F, 0.0F, 32.0F},
        .patches = {},
        .blocks = {},
        .regions = {{plain, "Mainland and Islands", RegionType::Plain}, {sea, "Open Sea", RegionType::Sea}},
        .locations =
            {{city, "Longwind City", LocationType::City, plain, {{-160, -384}, {160, 0}}, {0, 0}},
             {village, "Southfield Village", LocationType::Village, plain, {{-64, 80}, {64, 160}}, {0, 112}},
             {shore,
              "South Bay Coast",
              LocationType::Wilderness,
              plain,
              {{-24, 224}, {24, 256}},
              {0, 240}}},
        .roads = {{RoadId{1}, "City South Road", city, village, 8, {{0, 0}, {0, 112}}},
                  {RoadId{6}, "South Bay Trail", village, shore, 4, {{0, 112}, {0, 240}}}},
    };
    for (auto const &run : continent_land::runs)
        for (auto column = run[1]; column < run[2]; ++column)
            data.tiles[run[0]][column].region = plain;

    add_starter_buildings(data);
    for (auto const &extra : spawn_blocks)
        data.blocks.push_back(
            {extra.min + data.spawn, extra.max + data.spawn, extra.surface, extra.solid});
    return Continent{std::move(data)};
}

} // namespace lc1
