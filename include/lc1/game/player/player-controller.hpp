#pragma once

#include "lc1/game/actions.hpp"
#include "lc1/game/character/character-intent.hpp"

#include <glm/glm.hpp>

#include <optional>

namespace lc1 {

class Character;

// Turns player actions into a CharacterIntent. It owns device-to-action mapping, mouse
// look and the choice between camera-relative and body-relative movement. It does not
// know whether the character may move, sprint or jump: Character applies those rules.
// Camera lock, recenter and view switching stay in PlayerView because they change the
// camera rather than the character.
class PlayerController {
  public:
    // Pure resolution for the first-person view. `restored_angles` supplies the remembered
    // absolute yaw/pitch after a view switch; std::nullopt derives them from the character.
    static CharacterIntent first_person(Character const &character, glm::vec2 movement,
                                        glm::vec2 look_delta,
                                        std::optional<glm::vec2> restored_angles = {});
    // WASD in the horizontal camera basis; the body turns toward the requested movement.
    static CharacterIntent third_person(glm::vec2 movement, glm::vec3 view_heading);

    // Full one-frame resolution. Input gating stays with the session: a false
    // keyboard_enabled/mouse_enabled suppresses that device's contribution.
    static CharacterIntent update(Character const &character, GameInputRouter const &input,
                                  glm::vec2 look_delta, bool keyboard_enabled, bool mouse_enabled,
                                  bool oblique, bool view_changed,
                                  std::optional<glm::vec2> restored_first_person_angles,
                                  glm::vec3 view_heading);
};

} // namespace lc1
