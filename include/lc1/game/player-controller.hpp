#pragma once

#include <glm/glm.hpp>

namespace lc1 {

class Continent;

// One ground-level controller for countryside and cities. Camera placement is
// derived from feet position, so looking up cannot turn walking into flight.
class PlayerController {
  public:
    static constexpr float half_width = 0.3F;
    static constexpr float height = 1.8F;
    static constexpr float eye_height = 1.65F;
    static constexpr float walk_speed = 4.5F;
    static constexpr float run_speed = 9.0F;

    explicit PlayerController(Continent const &continent);
    void move(Continent const &continent, glm::vec2 direction, float delta_seconds, bool running);

    glm::vec3 position() const { return position_; }
    glm::vec3 eye_position() const { return position_ + glm::vec3{0.0F, eye_height, 0.0F}; }

  private:
    glm::vec3 position_;
};

} // namespace lc1
