#pragma once

#include "lc1/scene/object.hpp"

namespace lc1 {

class Continent;

struct CharacterConfig {
    float half_width = 0.3F;
    float height = 1.8F;
    float eye_height = 1.65F;
    float walk_speed = 4.5F;
    float run_speed = 18.0F;
};

// One upright, ground-level character, independent of input devices and cameras.
// scene::Object::transform is the sole world pose; its origin is at the character's feet.
class Character {
  public:
    // Simulation-only character at the continent's spawn, with no drawable attached.
    explicit Character(Continent const &continent);
    Character(Continent const &continent, scene::Object object, CharacterConfig config = {});

    CharacterConfig const &config() const { return config_; }
    glm::vec3 position() const { return object_.transform.position; }
    scene::Transform const &transform() const { return object_.transform; }

    // World-space displacement in metres. This ground prototype requires y == 0.
    // Collision can shorten or slide the displacement; it never changes body facing.
    void move(Continent const &continent, glm::vec3 displacement);
    // Ground movement from a world-space direction, with speed and frame-time limits.
    void walk(Continent const &continent, glm::vec3 direction, float delta_seconds, bool running);

    // Right-handed degrees about +Y. Zero faces -Z, independently of asset correction.
    float body_yaw() const { return object_.transform.rotation.y; }
    void set_body_yaw(float degrees);
    void set_body_direction(glm::vec3 direction);
    glm::vec3 body_heading() const;

    // Relative to the body: positive yaw turns left, positive pitch looks up.
    // These are look targets, not a skeleton pose. Yaw/pitch are limited to +/-85/89.
    float look_yaw() const { return look_yaw_; }
    float look_pitch() const { return look_pitch_; }
    void set_look_angles(float yaw_degrees, float pitch_degrees);
    glm::vec3 look_direction() const;
    glm::vec3 heading() const; // Horizontal look direction, without pitch.
    glm::vec3 right() const;
    glm::vec3 eye_position() const;

    // Requires an attached mesh and material; both are borrowed through scene::Object.
    DrawItem draw_item() const;

  private:
    CharacterConfig config_;
    float look_yaw_ = 0.0F;
    float look_pitch_ = 0.0F;
    scene::Object object_;
};

} // namespace lc1
