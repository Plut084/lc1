#include "../app/player-view.hpp"

#include "lc1/scene/character.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

bool near(float left, float right)
{
    return std::abs(left - right) < 0.001F;
}

bool near(glm::vec3 left, glm::vec3 right)
{
    return glm::length(left - right) < 0.001F;
}

lc1::Continent make_world()
{
    return lc1::Continent{{
        .origin = {-128.0F, -128.0F},
        .tile_size = 128.0F,
        .tiles = std::vector(2, std::vector<lc1::TileData>(2)),
        .spawn = {0.0F, 0.0F, 0.0F},
        .patches = {},
        .blocks = {},
        .regions = {{lc1::RegionId{1}, "平原", lc1::RegionType::Plain}},
        .locations = {},
        .roads = {},
    }};
}

void shared_position_and_remembered_look()
{
    auto const world = make_world();
    lc1::scene::Character player{world};
    lc1::app::PlayerView view{world, player.position()};
    check(view.oblique(), "a session starts in the oblique view");
    check(view.camera().position().y > player.eye_position().y,
          "oblique camera is above the player");

    view.toggle(player);
    check(!view.oblique() && near(view.camera().position(), player.eye_position()),
          "first person uses the same character's eye position");
    view.update(player, {.movement = {0, 1}, .look_delta = {800, -200}}, {}, 0.1F);
    auto const first_person_yaw = player.body_yaw();
    auto const first_person_pitch = player.look_pitch();
    auto const first_person_position = player.position();
    check(!near(first_person_position, world.spawn()),
          "first-person movement advances the character");
    check(near(view.camera().forward(), player.look_direction()),
          "first-person camera tracks mouse look");

    view.toggle(player);
    check(near(player.position(), first_person_position),
          "view switching does not teleport the player");
    view.update(player, {.movement = {1, 0}}, {}, 0.1F);
    auto const oblique_position = player.position();
    check(!near(oblique_position, first_person_position),
          "oblique movement continues from that position");
    check(!near(player.body_yaw(), first_person_yaw), "oblique walking turns the body");

    view.toggle(player);
    check(near(player.position(), oblique_position),
          "returning to first person preserves world position");
    check(near(player.body_yaw(), first_person_yaw) &&
              near(player.look_pitch(), first_person_pitch),
          "returning to first person restores the previous mouse-look angles");
    check(near(view.camera().position(), player.eye_position()) &&
              near(view.camera().forward(), player.look_direction()),
          "restored first-person camera matches the character pose");
}

void zoom_and_unlock_survive_switching()
{
    auto const world = make_world();
    lc1::scene::Character player{world};
    lc1::app::PlayerView view{world, player.position()};
    auto const initial_camera = view.camera().position();
    view.update(player, {}, {.toggle_lock = true, .scroll = 2.0F}, 0.1F);
    auto const zoomed_camera = view.camera().position();
    check(!near(initial_camera, zoomed_camera), "wheel input changes oblique zoom");

    view.toggle(player);
    glm::vec3 const first_move{3, 0, 1};
    player.move(world, first_move);
    view.toggle(player);
    view.update(player, {}, {}, 0.1F);
    check(near(view.camera().position(), zoomed_camera + first_move),
          "returning to oblique recenters at the player while preserving zoom");

    auto const recentered_camera = view.camera().position();
    player.move(world, {2, 0, 0});
    view.update(player, {}, {}, 0.1F);
    check(near(view.camera().position(), recentered_camera),
          "the unlocked camera remains unlocked");
    view.update(player, {}, {.recenter = true}, 0.1F);
    check(near(view.camera().position(), recentered_camera + glm::vec3{2, 0, 0}),
          "holding recenter temporarily follows an unlocked player");
    auto const held_camera = view.camera().position();
    player.move(world, {2, 0, 0});
    view.update(player, {}, {}, 0.1F);
    check(near(view.camera().position(), held_camera),
          "releasing recenter restores free camera movement");
    view.update(player, {}, {.toggle_lock = true}, 0.1F);
    check(near(view.camera().position(), held_camera + glm::vec3{2, 0, 0}),
          "locking again follows the current player position");
}

void disabled_input_preserves_pose()
{
    auto const world = make_world();
    lc1::scene::Character player{world};
    lc1::app::PlayerView view{world, player.position()};
    for (int mode = 0; mode < 2; ++mode) {
        auto const position = player.position();
        auto const direction = player.look_direction();
        auto const camera = view.camera().position();
        view.update(player, {.movement = {1, 1}, .look_delta = {500, 500}, .running = true},
                    {.enabled = false, .toggle_lock = true, .scroll = 10.0F}, 0.1F);
        check(near(player.position(), position) && near(player.look_direction(), direction),
              "UI/focus gating prevents player movement and mouse look in either view");
        check(near(view.camera().position(), camera),
              "disabled input cannot zoom or pan the camera");
        view.toggle(player);
    }
}

} // namespace

int main()
{
    try {
        shared_position_and_remembered_look();
        zoom_and_unlock_survive_switching();
        disabled_input_preserves_pose();
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
