#include "continent-layout.hpp"

#include "lc1/game/continent.hpp"

#include <cmath>

namespace lc1 {

void add_starter_buildings(ContinentData &data)
{
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
}

} // namespace lc1
