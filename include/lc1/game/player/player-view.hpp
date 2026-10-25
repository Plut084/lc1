#pragma once

#include "lc1/game/actions.hpp"
#include "lc1/game/camera/map-camera-controller.hpp"
#include "lc1/game/player/player-controller.hpp"
#include "lc1/scene/camera.hpp"

namespace lc1 {

// Both views share one character. GameSession supplies input gates; Application
// applies cursor capture when the view changes. This class owns no window or input router.
class PlayerView {
  public:
    PlayerView(Continent const &continent, glm::vec3 player_position);
    void toggle(Character const &player);
    // Resolves the action-to-intent mapping without mutating the character; GameWorld applies it.
    // Discards look motion on a view switch, before Application changes cursor capture.
    CharacterIntent resolve_input(Character const &player, GameInputRouter const &input,
                                  glm::vec2 look_delta, bool keyboard_enabled, bool mouse_enabled);
    // Call only while mouse input is enabled; pointer_active additionally gates edge pan.
    void update_pointer(InputState const &devices, glm::vec2 viewport_size, bool pointer_active,
                        float delta_seconds);
    void update_camera(Character const &player, float delta_seconds);

    void set_aspect_ratio(float aspect_ratio) { camera_.set_aspect_ratio(aspect_ratio); }

    bool oblique() const { return oblique_; }

    scene::FpsCamera const &camera() const { return camera_; }

  private:
    void follow_first_person(Character const &player);

    Continent const *continent_; // Borrowed; the game session owns the world.
    MapCameraController map_camera_;
    scene::FpsCamera camera_;
    bool oblique_ = true;
    glm::vec2 first_person_angles_{};
    bool view_changed_ = false;
    bool recenter_ = false;
};

} // namespace lc1
