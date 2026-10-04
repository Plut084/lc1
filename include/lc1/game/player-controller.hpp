#pragma once

#include <glm/glm.hpp>

namespace lc1 {

class Continent;
class Character;

// Resolved gameplay input. The application supplies zero input when UI/focus gates it.
struct PlayerInput {
    glm::vec2 movement{};   // x: right, y: forward.
    glm::vec2 look_delta{}; // Cursor motion, supplied only while mouse look is enabled.
    bool running = false;
};

class FirstPersonCharacterController {
  public:
    explicit FirstPersonCharacterController(Continent const &continent) : continent_(continent) {}
    void update(float delta_seconds, PlayerInput const &input, Character &character) const;

  private:
    Continent const &continent_; // Borrowed; the world outlives this controller.
};

class ThirdPersonCharacterController {
  public:
    explicit ThirdPersonCharacterController(Continent const &continent) : continent_(continent) {}
    void update(float delta_seconds, PlayerInput const &input, glm::vec3 view_heading,
                Character &character) const;

  private:
    Continent const &continent_; // Borrowed; the world outlives this controller.
};

} // namespace lc1
