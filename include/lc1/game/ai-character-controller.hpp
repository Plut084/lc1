#pragma once

#include "lc1/game/character-controller.hpp"
#include "lc1/game/character.hpp"

namespace lc1 {

struct Waypoint {
    glm::vec3 position;
    float wait_seconds = 0.0F; // 到达该点后停留多久，再走向下一个点
};

class AiCharacterController : public CharacterController {
  public:
    AiCharacterController(Continent const &continent, std::vector<Waypoint> path,
                          float arrival_radius = 0.5F)
        : continent_(&continent), path_(std::move(path)), arrival_radius_(arrival_radius)
    {
        assert(!path_.empty() && "path must have at least one waypoint");
        current_target_ = path_.size() > 1 ? 1 : 0;
    }

    void update(float delta_seconds, Character &character) override
    {
        if (path_.size() == 1) {
            wait_remaining_ = std::max(0.0F, wait_remaining_ - delta_seconds);
            if (wait_remaining_ == 0.0F)
                wait_remaining_ = path_[0].wait_seconds;
            return;
        }

        if (wait_remaining_ > 0.0F) {
            wait_remaining_ = std::max(0.0F, wait_remaining_ - delta_seconds);
            if (wait_remaining_ == 0.0F)
                current_target_ = (current_target_ + 1) % path_.size();
            return;
        }

        auto const target_pos = path_[current_target_].position;
        if (glm::distance(character.position(), target_pos) <= arrival_radius_) {
            wait_remaining_ = path_[current_target_].wait_seconds;
            if (wait_remaining_ == 0.0F)
                current_target_ = (current_target_ + 1) % path_.size();
            return;
        }

        auto const to_target = target_pos - character.position();
        if (glm::length(to_target) > 1e-12F)
            character.walk(*continent_, glm::normalize(to_target), delta_seconds, false);
    }

  private:
    Continent const *continent_ = nullptr;
    std::vector<Waypoint> path_;
    std::size_t current_target_ = 0;
    float arrival_radius_ = 0.5F;
    float wait_remaining_ = 0.0F;
};

} // namespace lc1
