#include "lc1/game/player-controller.hpp"
#include "lc1/error.hpp"
#include "lc1/game/character.hpp"

#include <algorithm>
#include <cmath>

namespace lc1 {
namespace {
bool finite(glm::vec2 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

PlayerCommand resolve_first_person_input(Character const &character, glm::vec2 movement,
                                         glm::vec2 look_delta,
                                         std::optional<glm::vec2> restored_angles)
{
    if (!finite(movement) || !finite(look_delta))
        fail("invalid player input");
    auto const angles = restored_angles.value_or(
        glm::vec2{character.body_yaw() + character.look_yaw(), character.look_pitch()});
    if (!finite(angles))
        fail("invalid restored look angles");
    constexpr float look_sensitivity = 0.05F;
    float const yaw = angles.x - look_delta.x * look_sensitivity;
    float const pitch = std::clamp(angles.y - look_delta.y * look_sensitivity, -89.0F, 89.0F);
    float const radians = glm::radians(yaw);
    glm::vec3 const heading{-std::sin(radians), 0, -std::cos(radians)};
    auto const right = glm::cross(heading, glm::vec3{0, 1, 0});
    return {.direction = right * movement.x + heading * movement.y,
            .body_yaw = yaw,
            .look_angles = glm::vec2{0, pitch},
            .interaction_direction = heading,
            .face_movement = false};
}

PlayerCommand resolve_third_person_input(glm::vec2 movement, glm::vec3 view_heading)
{
    if (!finite(movement))
        fail("invalid player movement");
    if (!finite(view_heading))
        fail("invalid third-person movement basis");
    double const length =
        std::hypot(static_cast<double>(view_heading.x), static_cast<double>(view_heading.z));
    if (length == 0)
        fail("third-person movement basis must have a horizontal direction");
    view_heading = {static_cast<float>(view_heading.x / length), 0,
                    static_cast<float>(view_heading.z / length)};
    auto const right = glm::cross(view_heading, glm::vec3{0, 1, 0});
    auto const direction = right * movement.x + view_heading * movement.y;
    return {.direction = direction,
            .body_yaw = {},
            .look_angles = direction != glm::vec3{} ? std::optional{glm::vec2{}} : std::nullopt};
}

void apply_player_command(Continent const &continent, Character &character,
                          PlayerCommand const &command, float delta_seconds)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0 || !finite(command.direction) ||
        (command.body_yaw && !std::isfinite(*command.body_yaw)) ||
        (command.look_angles && !finite(*command.look_angles)) ||
        (command.interaction_direction && !finite(*command.interaction_direction)))
        fail("invalid player command");
    if (command.body_yaw)
        character.set_body_yaw(*command.body_yaw);
    if (command.look_angles)
        character.set_look_angles(command.look_angles->x, command.look_angles->y);
    character.walk(continent, command.direction, delta_seconds, command.running,
                   command.face_movement);
    if (command.jumping)
        character.request_jump();
}
} // namespace lc1
