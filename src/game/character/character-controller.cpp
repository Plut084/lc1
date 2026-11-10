#include "lc1/game/character/character-controller.hpp"

#include <cassert>

namespace lc1 {

CharacterController::~CharacterController() = default;

void CharacterController::possess(Character const &character)
{
    character_ = &character;
}

Character const &CharacterController::character() const
{
    assert(character_ && "possess a character before deciding");
    return *character_;
}

} // namespace lc1
