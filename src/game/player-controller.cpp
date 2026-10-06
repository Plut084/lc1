#include "lc1/game/player-controller.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character.hpp"

#include <cmath>

namespace lc1 {
namespace {

void validate_input(float delta_seconds, PlayerInput const &input)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F || !std::isfinite(input.movement.x) ||
        !std::isfinite(input.movement.y) || !std::isfinite(input.look_delta.x) ||
        !std::isfinite(input.look_delta.y))
        fail("invalid player input");
}

} // namespace

void FirstPersonCharacterController::update(float delta_seconds, PlayerInput const &input,
                                            Character &character) const
{
    validate_input(delta_seconds, input);
    constexpr float look_sensitivity = 0.05F;
    // Mouse-right turns clockwise; body yaw uses right-handed rotation about +Y.
    character.set_body_yaw(character.body_yaw() + character.look_yaw() -
                           input.look_delta.x * look_sensitivity);
    character.set_look_angles(0.0F, character.look_pitch() - input.look_delta.y * look_sensitivity);
    auto const direction =
        character.right() * input.movement.x + character.heading() * input.movement.y;
    character.walk(*continent_, direction, delta_seconds, input.running);
}

void ThirdPersonCharacterController::update(float delta_seconds, PlayerInput const &input,
                                            glm::vec3 view_heading, Character &character) const
{
    validate_input(delta_seconds, input);
    if (!std::isfinite(view_heading.x) || !std::isfinite(view_heading.y) ||
        !std::isfinite(view_heading.z))
        fail("invalid third-person movement basis");
    double const length =
        std::hypot(static_cast<double>(view_heading.x), static_cast<double>(view_heading.z));
    if (length == 0.0)
        fail("third-person movement basis must have a horizontal direction");
    view_heading = {static_cast<float>(view_heading.x / length), 0.0F,
                    static_cast<float>(view_heading.z / length)};
    auto const right = glm::cross(view_heading, glm::vec3{0.0F, 1.0F, 0.0F});
    auto const direction = right * input.movement.x + view_heading * input.movement.y;
    if (direction.x != 0.0F || direction.z != 0.0F) {
        character.set_body_direction(direction);
        character.set_look_angles(0.0F, 0.0F);
    }
    character.walk(*continent_, direction, delta_seconds, input.running);
}

} // namespace lc1
