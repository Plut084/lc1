#include "lc1/game/continent.hpp"
#include "lc1/game/map-camera-controller.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/scene/character.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

bool near(float a, float b)
{
    return std::abs(a - b) < 0.001F;
}

lc1::ContinentData flat_world()
{
    return {.origin = {-32.0F, -32.0F},
            .tile_size = 16.0F,
            .tiles = std::vector(4, std::vector<lc1::TileData>(4)),
            .spawn = {0.0F, 0.0F, 0.0F},
            .patches = {},
            .blocks = {},
            .regions = {{lc1::RegionId{1}, "平原", lc1::RegionType::Plain},
                        {lc1::RegionId{2}, "海域", lc1::RegionType::Sea}},
            .locations = {},
            .roads = {}};
}

void grid_and_terrain()
{
    auto data = flat_world();
    data.tiles[2][3].region = lc1::RegionId{2};
    lc1::Continent const world{std::move(data)};
    check(world.rows() == 4 && world.columns() == 4, "grid dimensions");
    check(!world.tile_at({-32.01F, 0.0F}), "negative out-of-bounds query");
    check(!world.tile_at({32.0F, 0.0F}), "upper bound is exclusive");
    check(world.tile_at({std::nextafter(32.0F, 0.0F), 0.0F}).has_value(),
          "a point immediately inside the upper bound remains a valid query");
    check(world.region_at({16.0F, 0.0F})->type == lc1::RegionType::Sea, "tile/region lookup");
    check(!world.ground_height({17.0F, 1.0F}), "sea is not walkable");
    auto const shore = world.move({0, 0, 1}, {100, 0}, 0.3F, 1.8F);
    check(near(shore.x, 15.7F), "movement stops at shoreline with body clearance");
    auto const edge = world.move({0, 0, -10}, {-100, 0}, 0.3F, 1.8F);
    check(near(edge.x, -31.7F), "body stays within continent bounds");

    data = flat_world();
    data.tiles[1].pop_back();
    bool rejected = false;
    try {
        lc1::Continent invalid{std::move(data)};
    }
    catch (std::exception const &) {
        rejected = true;
    }
    check(rejected, "ragged terrain grid must be rejected");
}

void collision_and_sliding()
{
    auto data = flat_world();
    data.blocks.push_back({{5, 0, -20}, {5.1F, 4, 20}});
    lc1::Continent const world{std::move(data)};
    auto const hit = world.move({0, 0, 0}, {100, 0}, 0.3F, 1.8F);
    check(near(hit.x, 4.7F), "high-speed sweep must not tunnel through thin walls");
    auto const slide = world.move({0, 0, 0}, {10, 4}, 0.3F, 1.8F);
    check(near(slide.x, 4.7F) && near(slide.z, 4.0F), "diagonal movement slides along wall");
    auto const away = world.move(slide, {-2, 0}, 0.3F, 1.8F);
    check(near(away.x, 2.7F), "contact must allow movement away from the wall");
    auto const tangent = world.move(hit, {0, 8}, 0.3F, 1.8F);
    check(near(tangent.z, 8.0F), "contact must allow tangential movement");

    data = flat_world();
    data.blocks.push_back({{5, 0, -20}, {6, 4, 20}});
    data.blocks.push_back({{-20, 0, 5}, {20, 4, 6}});
    lc1::Continent const corner{std::move(data)};
    auto const stopped = corner.move({0, 0, 0}, {10, 10}, 0.3F, 1.8F);
    check(near(stopped.x, 4.7F) && near(stopped.z, 4.7F), "two contact planes stop at corner");
    check(corner.can_stand(stopped, 0.3F, 1.8F), "corner resolution leaves a valid body");

    data = flat_world();
    data.blocks.push_back({{-2, 2, -2}, {2, 5, -1}});
    lc1::Continent const lintel{std::move(data)};
    check(near(lintel.move({0, 0, 2}, {0, -6}, 0.3F, 1.8F).z, -4),
          "player passes below an overhead lintel");
    check(near(lintel.move({0, 0, 2}, {0, -6}, 0.3F, 2.5F).z, -0.7F),
          "a taller body collides with the same lintel");
}

void terrain_collision_candidates()
{
    auto grid_data = flat_world();
    grid_data.origin = {-512, -384};
    grid_data.tiles = std::vector(48, std::vector<lc1::TileData>(64));
    auto reference_data = grid_data;
    // The reference uses the original all-blocks sweep, so it cannot accidentally
    // share a bug in the new terrain candidate selection.
    for (int z = 0; z < 48; ++z) {
        for (int x = 0; x < 64; ++x) {
            if ((x * 7 + z * 11) % 13 != 0 || (x == 32 && z == 24))
                continue;
            grid_data.tiles[z][x].region = lc1::RegionId{2};
            auto const corner = grid_data.origin + glm::vec2{x, z} * 16.0F;
            reference_data.blocks.push_back(
                {{corner.x, 0, corner.y}, {corner.x + 16, 8, corner.y + 16}});
        }
    }
    lc1::Continent const grid{std::move(grid_data)};
    lc1::Continent const reference{std::move(reference_data)};
    std::mt19937 random{41};
    std::uniform_real_distribution<float> x_position{-512, 512};
    std::uniform_real_distribution<float> z_position{-384, 384};
    std::uniform_real_distribution<float> travel{-1200, 1200};
    int sweeps = 0;
    for (int trial = 0; trial < 1600; ++trial) {
        glm::vec3 const start{x_position(random), 0, z_position(random)};
        float const half_width = trial % 3 == 0 ? 8.0F : 0.3F;
        bool const valid = reference.can_stand(start, half_width, 1.8F);
        check(grid.can_stand(start, half_width, 1.8F) == valid,
              "nearby tile selection agrees with full body overlap tests");
        if (!valid)
            continue;
        glm::vec2 const delta{trial % 5 == 0 ? 0.0F : travel(random), travel(random)};
        auto const expected = reference.move(start, delta, half_width, 1.8F);
        auto const actual = grid.move(start, delta, half_width, 1.8F);
        if (glm::distance(expected, actual) >= 0.001F)
            std::cerr << "trial " << trial << " start " << start.x << ',' << start.z << " delta "
                      << delta.x << ',' << delta.y << " half " << half_width << " expected "
                      << expected.x << ',' << expected.z << " actual " << actual.x << ','
                      << actual.z << '\n';
        check(glm::distance(expected, actual) < 0.001F,
              "local terrain candidates preserve long, diagonal and sliding sweeps");
        ++sweeps;
    }
    check(sweeps > 500, "exercise enough valid terrain sweeps");
}

void player_and_city()
{
    lc1::Continent const flat{flat_world()};
    lc1::scene::Character straight{flat};
    lc1::scene::Character diagonal{flat};
    straight.walk(flat, {1, 0, 0}, 0.1F, false);
    diagonal.walk(flat, {1, 0, 1}, 0.1F, false);
    check(near(glm::length(straight.position()), glm::length(diagonal.position())),
          "diagonal movement must not be faster");
    check(near(straight.position().y, 0) && near(straight.eye_position().y, 1.65F),
          "feet and camera remain at ground level");
    lc1::scene::Character resumed{flat};
    resumed.walk(flat, {1, 0, 0}, 10.0F, false);
    check(near(resumed.position().x, 0.45F), "long stalls do not teleport the player");

    lc1::Continent const city = lc1::Continent::make_prototype();
    lc1::scene::Character player{city};
    for (int frame = 0; frame < 600; ++frame)
        player.walk(city, {0, 0, -1}, 1.0F / 60.0F, false);
    check(player.position().z < -10.0F, "WASD walk crosses the actual city gate");
    check(near(player.eye_position().y, 1.65F), "entering city does not change camera height");
    for (int frame = 0; frame < 600; ++frame)
        player.walk(city, {0, 0, 1}, 1.0F / 60.0F, false);
    check(near(player.position().z, city.spawn().z), "the gate also allows exiting the city");
    auto const wall = city.move({40, 0, 32}, {0, -100}, 0.3F, 1.8F);
    check(near(wall.z, 0.3F), "city wall blocks movement beside the gate");

    std::mt19937 random{29};
    std::uniform_real_distribution<float> direction{-1.0F, 1.0F};
    for (int step = 0; step < 4000; ++step) {
        player.walk(city, {direction(random), 0, direction(random)}, 0.1F, true);
        check(city.can_stand(player.position(), player.config().half_width, player.config().height),
              "movement must preserve the nonpenetration invariant");
    }
}

void camera_projection()
{
    lc1::scene::FpsCamera camera;
    camera.set_position({0, 0, 10});
    camera.look_at({0, 0, 0});
    camera.set_zfar(200.0F);
    camera.set_aspect_ratio(2.0F);
    camera.set_fov_y(90.0F);
    auto project = [&](glm::vec3 point) {
        glm::vec4 const clip = camera.view_projection_matrix() * glm::vec4{point, 1.0F};
        return glm::vec3{clip} / clip.w;
    };
    check(near(project({20, 10, 0}).x, 1.0F) && near(project({20, 10, 0}).y, 1.0F),
          "perspective field of view respects aspect ratio without a second Y flip");
    check(near(project({5, 0, 0}).x, 0.25F) && near(project({5, 0, -10}).x, 0.125F),
          "doubling depth halves the apparent size");
    check(near(project({0, 0, 9.9F}).z, 0.0F) && near(project({0, 0, -190}).z, 1.0F),
          "perspective depth maps the near/far planes to Vulkan's zero-to-one range");
}

void perspective_camera_zoom()
{
    lc1::GroundBounds const bounds{{-100, -100}, {100, 100}};
    glm::vec3 const player{0, 0, 0};
    lc1::scene::FpsCamera camera;
    camera.set_zfar(800.0F);
    lc1::MapCameraController controller{player};
    controller.apply(camera);
    auto project = [&](glm::vec3 point) {
        glm::vec4 const clip = camera.view_projection_matrix() * glm::vec4{point, 1.0F};
        return glm::vec3{clip} / clip.w;
    };
    auto const center = controller.center();
    auto const forward = camera.forward();
    auto const sample = center + camera.right() * 5.0F;
    float const initial_size = project(sample).x;
    float const initial_distance = glm::distance(camera.position(), center);
    check(initial_size > project(sample + forward * 20.0F).x,
          "the active oblique map camera has perspective foreshortening");
    check(near(project(center + camera.up() * 24.0F).y, 1.0F),
          "default framing covers 48 metres at the target depth");

    controller.update(camera, player, bounds, {.scroll = 1.0F}, 0.1F);
    check(glm::distance(camera.position(), center) < initial_distance &&
              project(sample).x > initial_size,
          "wheel zoom approaches the target and increases apparent size");
    check(glm::length(project(center) * glm::vec3{1, 1, 0}) < 0.001F &&
              glm::length(camera.forward() - forward) < 0.001F,
          "zoom preserves the target at screen center and the fixed viewing angle");
    controller.update(camera, player, bounds, {.scroll = -1.0F}, 0.1F);
    check(near(glm::distance(camera.position(), center), initial_distance),
          "reverse wheel input restores the camera distance");

    for (float scroll : {20.0F, -20.0F}) {
        controller.update(camera, player, bounds, {.scroll = scroll}, 0.1F);
        check(near(controller.view_height(), scroll > 0 ? 16.0F : 96.0F),
              "zoom stays within its framing limits");
        check(project(center).z > 0.0F && project(center).z < 1.0F &&
                  camera.position().y > player.y,
              "both zoom limits keep the target visible and camera above the ground");
    }
}

void camera_lock_and_edge_pan()
{
    lc1::GroundBounds const bounds{{-100, -100}, {100, 100}};
    glm::vec3 player{0, 0, 0};
    lc1::scene::FpsCamera camera;
    lc1::MapCameraController controller{player};
    auto same = [](glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 0.001F; };
    lc1::MapCameraInput edge{
        .cursor = {0, 350}, .viewport_size = {1000, 700}, .pointer_active = true};

    controller.update(camera, {3, 0, 4}, bounds, edge, 0.1F);
    check(controller.locked() && same(controller.center(), {3, 0.9F, 4}),
          "locked camera follows the player and ignores screen edges");

    auto const before_unlock = controller.center();
    controller.update(camera, {3, 0, 4}, bounds, {.toggle_lock = true}, 0.1F);
    check(!controller.locked() && same(controller.center(), before_unlock),
          "unlocking preserves the current camera center");
    controller.update(camera, {10, 0, 10}, bounds, {}, 0.1F);
    check(same(controller.center(), before_unlock),
          "unlocked camera does not follow WASD movement");

    controller.update(camera, player, bounds, edge, 0.1F);
    glm::vec3 const pan = controller.center() - before_unlock;
    check(glm::dot(pan, camera.right()) < -1.0F &&
              std::abs(glm::dot(pan, camera.heading())) < 0.001F,
          "left screen edge pans left in the fixed camera basis");
    auto const panned = controller.center();
    edge.pointer_active = false;
    controller.update(camera, player, bounds, edge, 0.1F);
    check(same(controller.center(), panned),
          "leaving or defocusing the window stops stale edge pan");
    edge.pointer_active = true;
    edge.cursor.x = -1.0F;
    controller.update(camera, player, bounds, edge, 0.1F);
    check(same(controller.center(), panned), "coordinates outside the content area do not pan");

    edge.cursor.x = 0.0F;
    edge.enabled = false;
    edge.toggle_lock = true;
    edge.recenter = true;
    edge.scroll = 5.0F;
    controller.update(camera, player, bounds, edge, 0.1F);
    check(!controller.locked() && same(controller.center(), panned) &&
              near(controller.view_height(), 48.0F),
          "UI context gates camera toggles, centering, zoom and edge input together");

    edge.enabled = true;
    edge.toggle_lock = false;
    edge.scroll = 0.0F;
    player = {10, 0, 10};
    controller.update(camera, player, bounds, edge, 0.1F);
    check(!controller.locked() && same(controller.center(), {10, 0.9F, 10}),
          "Space recenters without locking and takes priority over edge pan");
    controller.update(camera, {11, 0, 10}, bounds, edge, 0.1F);
    check(!controller.locked() && same(controller.center(), {11, 0.9F, 10}),
          "holding Space temporarily follows the player");
    controller.update(camera, {12, 0, 10}, bounds, {}, 0.1F);
    check(same(controller.center(), {11, 0.9F, 10}), "releasing Space leaves the camera unlocked");
    controller.update(camera, {12, 0, 10}, bounds, {.toggle_lock = true}, 0.1F);
    check(controller.locked() && same(controller.center(), {12, 0.9F, 10}),
          "locking again immediately returns to the player");

    lc1::MapCameraController side{player};
    lc1::MapCameraController corner{player};
    side.update(camera, player, bounds, {.toggle_lock = true}, 0.0F);
    corner.update(camera, player, bounds, {.toggle_lock = true}, 0.0F);
    auto const start = side.center();
    edge = {.cursor = {0, 350}, .viewport_size = {1000, 700}, .pointer_active = true};
    side.update(camera, player, bounds, edge, 0.1F);
    edge.cursor.y = 0.0F;
    corner.update(camera, player, bounds, edge, 0.1F);
    check(near(glm::length(side.center() - start), glm::length(corner.center() - start)),
          "corner pan does not exceed the maximum edge speed");
    for (int frame = 0; frame < 100; ++frame)
        corner.update(camera, player, {{-2, -2}, {2, 2}}, edge, 0.1F);
    check(std::abs(corner.center().x) <= 2 && std::abs(corner.center().z) <= 2,
          "free camera center stays within the continent");
}

} // namespace

int main()
{
    try {
        grid_and_terrain();
        collision_and_sliding();
        terrain_collision_candidates();
        player_and_city();
        camera_projection();
        perspective_camera_zoom();
        camera_lock_and_edge_pan();
        std::cout
            << "continent: terrain, continuous collision, sliding and city traversal passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << "continent test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
