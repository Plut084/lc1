#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>

namespace lc1 {
class Continent;
class Character;

// World-space control intent. No camera, window, or device dependencies.
struct PlayerCommand {
    glm::vec3 direction{};
    std::optional<float> body_yaw = std::nullopt;
    std::optional<glm::vec2> look_angles = std::nullopt;
    // Optional horizontal aim for interactions; otherwise use the resolved body heading.
    std::optional<glm::vec3> interaction_direction = std::nullopt;
    bool running = false;
    bool jumping = false;
    bool face_movement = true;
};

PlayerCommand resolve_first_person_input(Character const &character, glm::vec2 movement,
                                         glm::vec2 look_delta,
                                         std::optional<glm::vec2> restored_angles = {});
PlayerCommand resolve_third_person_input(glm::vec2 movement, glm::vec3 view_heading);
// GameWorld applies control before advancing physics, using its capped simulation time.
void apply_player_command(Continent const &continent, Character &character,
                          PlayerCommand const &command, float delta_seconds);
} // namespace lc1
