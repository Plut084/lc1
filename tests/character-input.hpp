#pragma once

#include "lc1/game/character/character.hpp"

class TestCharacterController : public lc1::CharacterController {
  public:
    explicit TestCharacterController(lc1::CharacterIntent intent) : intent_(intent) {}

    lc1::CharacterIntent decide(float) override { return intent_; }

  private:
    lc1::CharacterIntent intent_;
};

struct RestoreCharacterController {
    lc1::Character &character;
    lc1::CharacterController *previous;

    ~RestoreCharacterController() { character.set_controller(previous); }
};

inline void apply_character_intent(lc1::Character &character, float delta_seconds,
                                   lc1::Continent const &continent, lc1::CharacterIntent intent)
{
    TestCharacterController controller{intent};
    RestoreCharacterController restore{character, character.controller()};
    character.set_controller(&controller);
    character.update(delta_seconds, continent);
}
