#pragma once

#include "lc1/game/map-camera-controller.hpp"
#include "lc1/game/player-controller.hpp"
#include "lc1/scene/camera.hpp"

namespace lc1 {

// Both views share one character. The application supplies focus-gated input and
// applies cursor capture when the view changes; this class owns no window.
class PlayerView {
  public:
    PlayerView(Continent const &continent, glm::vec3 player_position);
    void toggle(Character &player);
    void update(Character &player, PlayerInput const &input, MapCameraInput const &camera_input,
                float delta_seconds);

    void set_aspect_ratio(float aspect_ratio) { camera_.set_aspect_ratio(aspect_ratio); }

    bool oblique() const { return oblique_; }

    scene::FpsCamera const &camera() const { return camera_; }

  private:
    void follow_first_person(Character const &player);

    Continent const *continent_; // Borrowed; the game session owns the world.
    FirstPersonCharacterController first_person_controller_;
    ThirdPersonCharacterController third_person_controller_;
    MapCameraController map_camera_;
    scene::FpsCamera camera_;
    bool oblique_ = true;
    glm::vec2 first_person_angles_{};
};

} // namespace lc1
