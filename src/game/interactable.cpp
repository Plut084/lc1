#include "lc1/game/interactable.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character.hpp"

namespace lc1 {

void InteractableCharacter::interact(Character &actor)
{
    if (!can_interact(actor))
        fail("cannot interact with this character");
    (void)actor;
    me_->request_jump();
}

bool InteractableCharacter::can_interact(Character &actor) const
{
    (void)actor;
    return true;
}

} // namespace lc1
