#include "lc1/game/interactable.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character.hpp"

namespace lc1 {

void InteractableCharacter::interact(Character &actor)
{
    if (!can_interact(actor))
        fail("cannot interact with this character");
    (void)actor;

    dialog_->said(me_, "Hello, I am " + me_->config().name + "!",
                  {
                      std::format("Hi, {}. My name is {}. Glad to see you.", me_->config().name,
                                  actor.config().name),
                      std::format("I don't want to talk to you, {}.", me_->config().name),
                  });
}

bool InteractableCharacter::can_interact(Character &actor) const
{
    (void)actor;
    return true;
}

} // namespace lc1
