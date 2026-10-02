#include "lc1/scene/character.hpp"

#include "lc1/error.hpp"
#include "lc1/game/continent.hpp"

#include <algorithm>
#include <cmath>

namespace lc1::scene {
namespace {

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

glm::vec3 horizontal_direction(float degrees)
{
    float const angle = glm::radians(degrees);
    return {-std::sin(angle), 0.0F, -std::cos(angle)};
}

} // namespace

Character::Character(Continent const &continent)
    : Character(continent, Object{.transform = {.position = continent.spawn()}})
{
}

Character::Character(Continent const &continent, Object object, CharacterConfig config)
    : config_(config), object_(object)
{
    if (!std::isfinite(config.half_width) || config.half_width <= 0.0F ||
        !std::isfinite(config.height) || config.height <= 0.0F ||
        !std::isfinite(config.eye_height) || config.eye_height < 0.0F ||
        config.eye_height > config.height || !std::isfinite(config.walk_speed) ||
        config.walk_speed < 0.0F || !std::isfinite(config.run_speed) || config.run_speed < 0.0F)
        fail("invalid character configuration");

    auto const &pose = object_.transform;
    if (!finite(pose.position) || !finite(pose.rotation) || pose.rotation.x != 0.0F ||
        pose.rotation.z != 0.0F || pose.scale != glm::vec3{1.0F})
        fail("ground character needs an upright, unscaled world transform");
    if (!continent.can_stand(position(), config_.half_width, config_.height))
        fail("character spawn is obstructed or outside the continent");
    if ((object_.mesh == nullptr) != (object_.material == nullptr))
        fail("character drawable needs both a mesh and a material");
    set_body_yaw(pose.rotation.y);
}

void Character::move(Continent const &continent, glm::vec3 displacement)
{
    if (!finite(displacement) || displacement.y != 0.0F)
        fail("ground character displacement must be finite and horizontal");
    object_.transform.position = continent.move(position(), {displacement.x, displacement.z},
                                                config_.half_width, config_.height);
}

void Character::walk(Continent const &continent, glm::vec3 direction, float delta_seconds,
                     bool running)
{
    if (!finite(direction) || !std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("invalid character walking input");
    direction.y = 0.0F;
    // double avoids overflow when normalizing large but finite input vectors.
    double const length =
        std::hypot(static_cast<double>(direction.x), static_cast<double>(direction.z));
    if (length == 0.0)
        return;
    direction *= static_cast<float>(1.0 / std::max(length, 1.0));
    float const speed = running ? config_.run_speed : config_.walk_speed;
    // A resize or debugger stall must not teleport the character.
    float const elapsed = std::min(delta_seconds, 0.1F);
    move(continent, direction * speed * elapsed);
}

void Character::set_body_yaw(float degrees)
{
    if (!std::isfinite(degrees))
        fail("character body yaw must be finite");
    object_.transform.rotation.y = std::remainder(degrees, 360.0F);
}

void Character::set_body_direction(glm::vec3 direction)
{
    if (!finite(direction))
        fail("character body direction must be finite");
    if (direction.x == 0.0F && direction.z == 0.0F)
        return;
    set_body_yaw(glm::degrees(std::atan2(-direction.x, -direction.z)));
}

glm::vec3 Character::body_heading() const
{
    return horizontal_direction(body_yaw());
}

void Character::set_look_angles(float yaw_degrees, float pitch_degrees)
{
    if (!std::isfinite(yaw_degrees) || !std::isfinite(pitch_degrees))
        fail("character look angles must be finite");
    look_yaw_ = std::clamp(yaw_degrees, -85.0F, 85.0F);
    look_pitch_ = std::clamp(pitch_degrees, -89.0F, 89.0F);
}

glm::vec3 Character::look_direction() const
{
    float const pitch = glm::radians(look_pitch_);
    return heading() * std::cos(pitch) + glm::vec3{0.0F, std::sin(pitch), 0.0F};
}

glm::vec3 Character::heading() const
{
    return horizontal_direction(body_yaw() + look_yaw_);
}

glm::vec3 Character::right() const
{
    return glm::cross(heading(), glm::vec3{0.0F, 1.0F, 0.0F});
}

glm::vec3 Character::eye_position() const
{
    // Character-space metres; independent of the source model's origin and units.
    return position() + glm::vec3{0.0F, config_.eye_height, 0.0F};
}

DrawItem Character::draw_item() const
{
    if (!object_.mesh || !object_.material)
        fail("cannot draw a simulation-only character");
    return object_.draw_item();
}

} // namespace lc1::scene
