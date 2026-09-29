#include "lc1/game/player-controller.hpp"

#include "lc1/error.hpp"
#include "lc1/game/continent.hpp"

#include <algorithm>
#include <cmath>

namespace lc1 {

PlayerController::PlayerController(Continent const &continent) : position_(continent.spawn())
{
    if (!continent.can_stand(position_, half_width, height))
        fail("player spawn is obstructed or outside the continent");
}

void PlayerController::move(Continent const &continent, glm::vec2 direction, float delta_seconds,
                            bool running)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F || !std::isfinite(direction.x) ||
        !std::isfinite(direction.y))
        fail("invalid player movement input");
    float const length = glm::length(direction);
    if (length <= 0.0F)
        return;
    direction /= std::max(length, 1.0F);
    // Do not turn a pause, resize or breakpoint into a jump across the world.
    float const elapsed = std::min(delta_seconds, 0.1F);
    float const speed = running ? run_speed : walk_speed;
    position_ = continent.move(position_, direction * speed * elapsed, half_width, height);
}

} // namespace lc1
