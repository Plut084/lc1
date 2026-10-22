#pragma once

namespace lc1 {

class Character;

class CharacterController {
  public:
    CharacterController() = default;
    CharacterController(CharacterController const &) = delete;
    CharacterController(CharacterController &&) = delete;
    CharacterController &operator=(CharacterController const &) = delete;
    CharacterController &operator=(CharacterController &&) = delete;
    virtual ~CharacterController() = default;

    // An interface, not a default: a controller that does nothing would silently stop
    // driving its character. Every concrete controller must decide what update does.
    virtual void update(float delta_seconds, Character &character) = 0;
};

} // namespace lc1
