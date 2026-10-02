#pragma once

#include "lc1/game/continent.hpp"

namespace lc1 {

namespace scene {
class FpsCamera;
} // namespace scene

struct MapCameraInput {
    bool enabled = true;
    bool toggle_lock = false; // Press edge, never held state.
    bool recenter = false;    // Holding temporarily follows without changing the lock.
    float scroll = 0.0F;
    glm::vec2 cursor{};
    glm::vec2 viewport_size{};   // Logical window coordinates, just like cursor.
    bool pointer_active = false; // Focused, hovered, and a visible cursor.
};

// Camera lock is independent of cursor capture. All state belongs to this scene.
class MapCameraController {
  public:
    explicit MapCameraController(glm::vec3 player_position);

    void update(scene::FpsCamera &camera, glm::vec3 player_position, GroundBounds bounds,
                MapCameraInput const &input, float delta_seconds);
    void apply(scene::FpsCamera &camera) const;

    bool locked() const { return locked_; }
    glm::vec3 center() const { return center_; }
    // Vertical span on the plane through center(), perpendicular to the viewing direction.
    // Perspective zoom changes camera distance; apparent size still varies with depth.
    float view_height() const { return view_height_; }

  private:
    bool locked_ = true;
    glm::vec3 center_;
    glm::vec3 smoothed_center_;
    float view_height_ = 48.0F;
};

} // namespace lc1
