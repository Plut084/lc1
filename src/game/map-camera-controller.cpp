#include "lc1/game/map-camera-controller.hpp"

#include "lc1/camera.hpp"

#include <algorithm>
#include <cmath>

namespace lc1 {
namespace {

constexpr glm::vec3 offset{32.0F, 48.0F, 32.0F};
constexpr glm::vec3 aim_height{0.0F, 0.9F, 0.0F};
constexpr float fov_y = 45.0F;

glm::vec2 edge_motion(MapCameraInput const &input)
{
    auto const size = input.viewport_size;
    auto const cursor = input.cursor;
    if (!input.pointer_active || size.x <= 0.0F || size.y <= 0.0F || !std::isfinite(size.x) ||
        !std::isfinite(size.y) || !std::isfinite(cursor.x) || !std::isfinite(cursor.y) ||
        cursor.x < 0.0F || cursor.y < 0.0F || cursor.x >= size.x || cursor.y >= size.y)
        return glm::vec2{0.0F};

    // A logical-point edge band keeps the feel consistent on HiDPI displays.
    float const band = std::min({24.0F, size.x * 0.5F, size.y * 0.5F});
    auto strength = [band](float distance) {
        return std::clamp((band - distance) / band, 0.0F, 1.0F);
    };
    glm::vec2 direction{strength(size.x - cursor.x) - strength(cursor.x),
                        strength(cursor.y) - strength(size.y - cursor.y)};
    // Corners must not move faster than the maximum speed along a single edge.
    return direction / std::max(glm::length(direction), 1.0F);
}

} // namespace

MapCameraController::MapCameraController(glm::vec3 player_position)
    : center_(player_position + aim_height), smoothed_center_(center_)
{
}

void MapCameraController::apply(FpsCamera &camera) const
{
    // Frame the target at the requested span using distance, with a fixed lens and angle.
    float const distance = view_height_ / (2.0F * std::tan(glm::radians(fov_y) * 0.5F));
    camera.set_position(smoothed_center_ + glm::normalize(offset) * distance);
    camera.look_at(smoothed_center_);
    camera.set_fov_y(fov_y);
}

void MapCameraController::update(FpsCamera &camera, glm::vec3 player_position, GroundBounds bounds,
                                 MapCameraInput const &input, float delta_seconds)
{
    if (input.enabled) {
        if (input.toggle_lock)
            locked_ = !locked_;
        float const scroll =
            std::isfinite(input.scroll) ? std::clamp(input.scroll, -20.0F, 20.0F) : 0.0F;
        view_height_ = std::clamp(view_height_ * std::exp(-scroll * 0.12F), 16.0F, 96.0F);
    }

    if (locked_ || (input.enabled && input.recenter)) {
        center_ = player_position + aim_height;
    }
    else if (input.enabled) {
        // Establish the fixed screen basis even on the first unlocked frame.
        apply(camera);
        glm::vec2 const direction = edge_motion(input);
        float const elapsed =
            std::isfinite(delta_seconds) ? std::clamp(delta_seconds, 0.0F, 0.1F) : 0.0F;
        float const speed = view_height_ * 0.7F;
        center_ +=
            (camera.right() * direction.x + camera.heading() * direction.y) * speed * elapsed;
        center_.x = std::clamp(center_.x, bounds.min.x, bounds.max.x);
        center_.z = std::clamp(center_.z, bounds.min.y, bounds.max.y);
    }
    smoothed_center_ =
        glm::mix(smoothed_center_, center_, std::clamp(delta_seconds * 12.0F, 0.0F, 1.0F));
    apply(camera);
}

} // namespace lc1
