#include "lc1/game/player-view.hpp"

#include "lc1/game/character.hpp"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace lc1 {

namespace {
glm::vec2 edge_motion(glm::vec2 cursor, glm::vec2 size)
{
    if (size.x <= 0.0F || size.y <= 0.0F || !std::isfinite(size.x) || !std::isfinite(size.y) ||
        !std::isfinite(cursor.x) || !std::isfinite(cursor.y) || cursor.x < 0.0F ||
        cursor.y < 0.0F || cursor.x >= size.x || cursor.y >= size.y)
        return glm::vec2{0.0F};

    // A logical-point edge band keeps the feel consistent on HiDPI displays.
    float const band = std::min({24.0F, size.x * 0.5F, size.y * 0.5F});
    auto strength = [band](float distance) {
        return std::clamp((band - distance) / band, 0.0F, 1.0F);
    };
    glm::vec2 direction{strength(size.x - cursor.x) - strength(cursor.x),
                        strength(cursor.y) - strength(size.y - cursor.y)};
    // Corners must not move faster than the maximum speed along a single edge.
    return direction / std::max(glm::length(direction), 1.0F);
}

} // namespace

PlayerView::PlayerView(Continent const &continent, glm::vec3 player_position)
    : continent_{&continent}, map_camera_{player_position}
{
    camera_.set_zfar(12800.0F);
    map_camera_.apply(camera_);
}

void PlayerView::follow_first_person(Character const &player)
{
    camera_.set_position(player.eye_position());
    camera_.look_at(player.eye_position() + player.look_direction());
}

void PlayerView::toggle(Character const &player)
{
    if (!oblique_)
        first_person_angles_ = {player.body_yaw() + player.look_yaw(), player.look_pitch()};
    oblique_ = !oblique_;
    view_changed_ = true;
    if (oblique_) {
        // Recenter while preserving zoom and the Y lock state.
        map_camera_.recenter(player.position());
        map_camera_.update(camera_, player.position(), 0.0F);
    }
    else {
        follow_first_person(player);
    }
    spdlog::info("[camera] {}",
                 oblique_ ? "update_player2: oblique" : "update_player: first-person");
}

PlayerCommand PlayerView::resolve_input(Character const &player, InputRouter const &input,
                                        glm::vec2 look_delta, bool keyboard_enabled,
                                        bool mouse_enabled)
{
    auto const movement = keyboard_enabled
                              ? glm::vec2{static_cast<float>(input.held(Action::MoveRight)) -
                                              static_cast<float>(input.held(Action::MoveLeft)),
                                          static_cast<float>(input.held(Action::MoveForward)) -
                                              static_cast<float>(input.held(Action::MoveBackward))}
                              : glm::vec2{};
    bool const enabled = keyboard_enabled || mouse_enabled;
    recenter_ = oblique_ && keyboard_enabled && input.held(Action::RecenterCamera);
    if (oblique_ && keyboard_enabled && input.pressed(Action::ToggleCameraLock)) {
        map_camera_.toggle_lock();
        spdlog::info("[camera] {}", map_camera_.locked() ? "locked to player" : "free camera");
    }
    PlayerCommand command;
    if (!oblique_) {
        if (enabled || view_changed_)
            command = resolve_first_person_input(
                player, movement, mouse_enabled ? look_delta : glm::vec2{},
                view_changed_ ? std::optional{first_person_angles_} : std::nullopt);
    }
    else {
        if (enabled) {
            map_camera_.apply(camera_);
            command = resolve_third_person_input(movement, camera_.heading());
        }
        if (view_changed_)
            command.look_angles = glm::vec2{};
    }
    command.running = keyboard_enabled && input.held(Action::Sprint);
    command.jumping = keyboard_enabled && input.pressed(Action::Jump);
    view_changed_ = false;
    return command;
}

void PlayerView::update_pointer(InputState const &devices, glm::vec2 viewport_size,
                                bool pointer_active, float delta_seconds)
{
    if (!oblique_)
        return;
    map_camera_.zoom(devices.scroll().y);
    if (pointer_active && !recenter_)
        map_camera_.pan(edge_motion(devices.cursor(), viewport_size), continent_->bounds(),
                        delta_seconds);
}

void PlayerView::update_camera(Character const &player, float delta_seconds)
{
    if (!oblique_) {
        follow_first_person(player);
        return;
    }
    if (recenter_)
        map_camera_.recenter(player.position());
    map_camera_.update(camera_, player.position(), delta_seconds);
}

} // namespace lc1
