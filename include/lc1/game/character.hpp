#pragma once

#include "lc1/game/character-controller.hpp"
#include "lc1/game/companion-order.hpp"
#include "lc1/game/equipment.hpp"
#include "lc1/game/interactable.hpp"
#include "lc1/game/party.hpp"
#include "lc1/game/physics.hpp"
#include "lc1/game/weapon.hpp"
#include "lc1/scene/transform.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace lc1 {

class Continent;
class Equipment;
class Weapon;
struct EquipmentProperty;

enum class CharacterStatus : std::uint8_t {
    Alive,
    Injured,
    Dead,
    Captive,
};

// Body proportions in metres, plus walking and running speed. The placeholder mesh and the
// collision body are both built from these numbers, so the two cannot silently disagree.
struct CharacterBody {
    float half_width = 0.3F;
    float height = 1.8F;
    float eye_height = 1.65F;
    float walk_speed = 4.5F;
    float run_speed = 18.0F;
    // Maximum ledge/ramp rise followed while grounded, and maximum distance the body
    // snaps downward to remain attached to a slope.
    float step_height = 0.35F;
    float ground_snap_distance = 0.35F;
};

struct CharacterConfig {
    std::uint32_t keyid;
    std::string name;
    bool sex;
    int age;
    Party *party;
    int level;

    std::map<EquipmentType, Equipment *> equipment;
    std::map<WeaponType, Weapon *> weapon;

    float health();
    float strenth();
    float agility();
};

// One upright, ground-level character, independent of input devices and cameras.
// transform_ is the sole world pose; its origin is at the character's feet.
class Character {
  public:
    Character(Continent const &continent, scene::Transform transform, CharacterConfig config,
              CharacterBody body = {});

    CharacterConfig &config() { return config_; }

    CharacterConfig const &config() const { return config_; }

    CharacterBody const &body() const { return body_; }

    float half_width() const { return body_.half_width; }

    float height() const { return body_.height; }

    glm::vec3 position() const { return transform_.position; }

    scene::Transform const &transform() const { return transform_; }

    void move(Continent const &continent, glm::vec3 displacement);
    void walk(Continent const &continent, glm::vec3 direction, float delta_seconds, bool running,
              bool face_movement = true);

    // Right-handed degrees about +Y. Zero faces -Z, independently of asset correction.
    float body_yaw() const { return transform_.rotation.y; }

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

    void request_jump() { jump_requested_ = true; }

    bool on_ground() const { return on_ground_; }

    // Pausing a controller keeps physics active and preserves its patrol state.
    void update(Continent const &continent, float delta_seconds, bool controller_enabled = true);

    CompanionOrder companion_order() const { return companion_order_; }

    CharacterController *controller() { return controller_.get(); }

    CharacterController const *controller() const { return controller_.get(); }

    void set_controller(std::unique_ptr<CharacterController> controller)
    {
        controller_ = std::move(controller);
    }

    Interactable *interactable() { return interactable_.get(); }

    void set_interactable(std::unique_ptr<Interactable> interactable)
    {
        interactable_ = std::move(interactable);
    }

    Faction const *faction() const { return this->config_.party->faction(); }

  private:
    // Party updates both sides of membership together during recruitment.
    friend class Party;

    CompanionOrder companion_order_ = CompanionOrder::None;
    CharacterConfig config_;
    CharacterBody body_;

    float look_yaw_ = 0.0F;
    float look_pitch_ = 0.0F;
    bool on_ground_;
    glm::vec3 velocity_;
    bool jump_requested_ = false;
    scene::Transform transform_;
    std::unique_ptr<CharacterController> controller_;
    std::unique_ptr<Interactable> interactable_;
};

} // namespace lc1
