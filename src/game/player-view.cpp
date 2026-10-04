#include "lc1/game/player-view.hpp"

#include "lc1/game/character.hpp"

#include <spdlog/spdlog.h>

namespace lc1 {

PlayerView::PlayerView(Continent const &continent, glm::vec3 player_position)
    : continent_{continent}, first_person_controller_{continent},
      third_person_controller_{continent}, map_camera_{player_position}
{
    camera_.set_zfar(800.0F);
    map_camera_.apply(camera_);
}

void PlayerView::follow_first_person(Character const &player)
{
    camera_.set_position(player.eye_position());
    camera_.look_at(player.eye_position() + player.look_direction());
}

void PlayerView::toggle(Character &player)
{
    if (!oblique_)
        first_person_angles_ = {player.body_yaw() + player.look_yaw(), player.look_pitch()};
    oblique_ = !oblique_;
    if (oblique_) {
        player.set_look_angles(0.0F, 0.0F);
        // Recenter while preserving zoom and the Y lock state.
        map_camera_.update(camera_, player.position(), continent_.bounds(), {.recenter = true},
                           0.0F);
    }
    else {
        player.set_body_yaw(first_person_angles_.x);
        player.set_look_angles(0.0F, first_person_angles_.y);
        follow_first_person(player);
    }
    spdlog::info("[camera] {}",
                 oblique_ ? "update_player2: oblique" : "update_player: first-person");
}

void PlayerView::update(Character &player, PlayerInput const &input,
                        MapCameraInput const &camera_input, float delta_seconds)
{
    if (!oblique_) {
        if (camera_input.enabled)
            first_person_controller_.update(delta_seconds, input, player);
        follow_first_person(player);
        return;
    }

    map_camera_.apply(camera_);
    if (camera_input.enabled)
        third_person_controller_.update(delta_seconds, input, camera_.heading(), player);
    map_camera_.update(camera_, player.position(), continent_.bounds(), camera_input,
                       delta_seconds);
    if (camera_input.enabled && camera_input.toggle_lock)
        spdlog::info("[camera] {}", map_camera_.locked() ? "locked to player" : "free camera");
}

} // namespace lc1
