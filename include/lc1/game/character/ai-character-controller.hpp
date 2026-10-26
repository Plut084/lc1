#pragma once

#include "lc1/game/character/character-controller.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace lc1 {

class Character;

struct Waypoint {
    glm::vec3 position;
    float wait_seconds = 0.0F; // Seconds to stay at this point before moving on.
};

// One AI controller with two explicit behaviors. The patrol constructor owns a waypoint
// list; the companion constructor owns a party order and follows the current leader.
// Both produce the same CharacterIntent contract and borrow the character read-only.
class AiCharacterController : public CharacterController {
  public:
    explicit AiCharacterController(std::vector<Waypoint> path, float arrival_radius = 0.5F);
    explicit AiCharacterController(CompanionOrder order);

    CharacterIntent update(float delta_seconds, Character const &character) override;
    std::optional<CompanionOrder> companion_order() const override;

  private:
    CharacterIntent patrol_update(float delta_seconds, Character const &character);
    CharacterIntent companion_update(float delta_seconds, Character const &character);

    // Engaged only for companion orders; a patrol controller reports no party order.
    std::optional<CompanionOrder> companion_order_;
    std::vector<Waypoint> path_;
    std::size_t current_target_ = 0;
    float arrival_radius_ = 0.5F;
    float wait_remaining_ = 0.0F;
};

} // namespace lc1
