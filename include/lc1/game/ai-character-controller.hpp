#pragma once

#include "lc1/game/character.hpp"

namespace lc1 {

class AiCharacterController {
  public:
    AiCharacterController(Continent const &continent, glm::vec3 pos_a, glm::vec3 pos_b)
        : continent_(&continent), current_source_(pos_a), current_target_(pos_b)
    {
    }

    void update(float delta_seconds, Character &character)
    {
        if (glm::distance(character.position(), current_target_) <= 0.5) {
            std::swap(current_target_, current_source_);
        }
        character.walk(*continent_, glm::normalize(current_target_ - character.position()),
                       delta_seconds, false);
    }

  private:
    Continent const *continent_ = nullptr; // Borrowed; the world outlives this controller.
    glm::vec3 current_source_;
    glm::vec3 current_target_;
};

} // namespace lc1
