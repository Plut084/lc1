#include "continent-layout.hpp"

#include "lc1/game/continent.hpp"

#include <cmath>

namespace lc1 {

void add_starter_buildings(ContinentData &data)
{
    auto patch = [&](glm::vec2 min, glm::vec2 max, ContinentSurface surface) {
        data.patches.push_back({{min, max}, surface});
    };
    auto block = [&](glm::vec3 min, glm::vec3 max, ContinentSurface surface, bool solid = true,
                     bool walkable_top = false) {
        data.blocks.push_back({min, max, surface, solid, walkable_top});
    };

    // A square spiral stair tower southeast of the spawn/showcase clearing. The route
    // wraps around a central stone core; each of the four sides rises 2.4 m, so one
    // full loop adds 9.6 m. Two loops give ground, 9.6 m and 19.2 m levels at the
    // same XZ columns, with upper flights stacked above lower ones.
    {
        constexpr float stair_x = 28.0F;
        constexpr float stair_z = 58.0F;
        constexpr float step_rise = 0.3F;
        constexpr float step_tread = 0.5F;
        constexpr int steps_per_flight = 8;
        constexpr int flight_count = 8;
        constexpr int flights_per_loop = 4;
        constexpr float flight_height = step_rise * static_cast<float>(steps_per_flight);
        constexpr float loop_height = flight_height * static_cast<float>(flights_per_loop);
        constexpr float landing_size = 2.0F;
        constexpr float walkway_width = 2.0F;
        constexpr float slab_thickness = 0.2F;

        patch({stair_x - 1.0F, stair_z - 1.0F}, {stair_x + 9.0F, stair_z + 9.0F},
              ContinentSurface::Stone);

        // Central core: 4 x 4 m, rising through both loops. It is not walkable and
        // keeps the route a ring rather than allowing shortcuts through the middle.
        block({stair_x + walkway_width, 0.0F, stair_z + landing_size},
              {stair_x + 6.0F, loop_height * 2.0F, stair_z + 6.0F}, ContinentSurface::Stone, true,
              false);

        for (int flight = 0; flight < flight_count; ++flight) {
            int const loop = flight / flights_per_loop;
            int const side = flight % flights_per_loop;
            float const flight_base =
                static_cast<float>(loop) * loop_height + static_cast<float>(side) * flight_height;
            float const first_level_base = static_cast<float>(side) * flight_height;

            for (int step = 0; step < steps_per_flight; ++step) {
                float const top = flight_base + static_cast<float>(step + 1) * step_rise;
                float const bottom = loop == 0 && side == 0 ? 0.0F : top - slab_thickness;
                float const offset = static_cast<float>(step) * step_tread;
                if (side == 0) {
                    // West side, walking north.
                    float const z = stair_z + 6.0F - offset - step_tread;
                    block({stair_x, bottom, z}, {stair_x + walkway_width, top, z + step_tread},
                          ContinentSurface::Stone, true, true);
                }
                else if (side == 1) {
                    // North side, walking east.
                    float const x = stair_x + walkway_width + offset;
                    block({x, bottom, stair_z}, {x + step_tread, top, stair_z + walkway_width},
                          ContinentSurface::Stone, true, true);
                }
                else if (side == 2) {
                    // East side, walking south.
                    float const z = stair_z + walkway_width + offset;
                    block({stair_x + 6.0F, bottom, z},
                          {stair_x + 6.0F + walkway_width, top, z + step_tread},
                          ContinentSurface::Stone, true, true);
                }
                else {
                    // South side, walking west.
                    float const x = stair_x + 6.0F - offset - step_tread;
                    block({x, bottom, stair_z + 6.0F},
                          {x + step_tread, top, stair_z + 6.0F + walkway_width},
                          ContinentSurface::Stone, true, true);
                }
            }

            // Corner landing after this flight. Its height completes the side's rise.
            float const top =
                first_level_base + static_cast<float>(loop) * loop_height + flight_height;
            float const bottom = loop == 0 && side == 0 ? 0.0F : top - slab_thickness;
            if (side == 0) {
                block({stair_x, bottom, stair_z},
                      {stair_x + landing_size, top, stair_z + landing_size},
                      ContinentSurface::Stone, true, true);
            }
            else if (side == 1) {
                block({stair_x + 6.0F, bottom, stair_z},
                      {stair_x + 6.0F + landing_size, top, stair_z + landing_size},
                      ContinentSurface::Stone, true, true);
            }
            else if (side == 2) {
                block({stair_x + 6.0F, bottom, stair_z + 6.0F},
                      {stair_x + 6.0F + landing_size, top, stair_z + 6.0F + landing_size},
                      ContinentSurface::Stone, true, true);
            }
            else {
                block({stair_x, bottom, stair_z + 6.0F},
                      {stair_x + landing_size, top, stair_z + 6.0F + landing_size},
                      ContinentSurface::Stone, true, true);
            }
        }
    }

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
