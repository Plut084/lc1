#pragma once

#include "lc1/game/character/character-intent.hpp"

namespace lc1 {

class Character;

// Shared intent-producing interface for AI and player controllers.
class CharacterController {
  public:
    CharacterController() = default;

    CharacterController(CharacterController const &) = delete;
    CharacterController(CharacterController &&) = delete;
    CharacterController &operator=(CharacterController const &) = delete;
    CharacterController &operator=(CharacterController &&) = delete;

    virtual ~CharacterController();

    // Read-only borrow: the character must outlive this binding.
    void possess(Character const &character);
    virtual CharacterIntent decide(float delta_seconds) = 0;

  protected:
    Character const &character() const;

  private:
    Character const *character_ = nullptr;
};

} // namespace lc1
