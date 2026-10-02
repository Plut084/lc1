#pragma once

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace lc1::scene {

class FpsCamera {
  public:
    FpsCamera() = default;

    glm::mat4 view_matrix() const
    {
        return glm::lookAt(position_, position_ + forward(), world_up);
    }

    glm::mat4 projection_matrix() const
    {
        return glm::perspective(glm::radians(fov_y_), aspect_ratio_, znear_, zfar_);
    }

    glm::mat4 view_projection_matrix() const { return projection_matrix() * view_matrix(); }

    glm::vec3 forward() const
    {
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(yaw_)) * glm::cos(glm::radians(pitch_));
        direction.y = glm::sin(glm::radians(pitch_));
        direction.z = glm::sin(glm::radians(yaw_)) * glm::cos(glm::radians(pitch_));
        return glm::normalize(direction);
    }
    glm::vec3 right() const { return glm::normalize(glm::cross(forward(), world_up)); }
    glm::vec3 up() const { return glm::normalize(glm::cross(right(), forward())); }

    // Without pitch
    glm::vec3 heading() const
    {
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(yaw_));
        direction.y = 0.0F;
        direction.z = glm::sin(glm::radians(yaw_));
        return glm::normalize(direction);
    }

    void add_yaw(float delta)
    {
        yaw_ = std::fmod(yaw_ + delta + 180.0F, 360.0F);
        if (yaw_ < 0.0F)
            yaw_ += 360.0F;
        yaw_ -= 180.0F;
    }

    void add_pitch(float delta)
    {
        pitch_ += delta;
        pitch_ = std::clamp(pitch_, -89.0F, 89.0F);
    }

    /// @return False if the target is too close to the camera to compute a direction. Otherwise,
    /// returns true.
    bool look_at(glm::vec3 target)
    {
        glm::vec3 diff = target - position_;
        float len = glm::length(diff);
        if (len < 1e-6F)
            return false;
        glm::vec3 direction = diff / len;

        // 防止浮点误差让 direction.y 略超 ±1，asin 返回 NaN
        float y = glm::clamp(direction.y, -1.0F, 1.0F);
        pitch_ = glm::clamp(glm::degrees(std::asin(y)), -89.0F, 89.0F);

        // 水平分量接近零时，yaw 退化，保持原值
        float horizontal_sq = direction.x * direction.x + direction.z * direction.z;
        if (horizontal_sq > 1e-12F)
            yaw_ = glm::degrees(std::atan2(direction.z, direction.x));
        return true;
    }

    void set_znear(float znear) { znear_ = znear; }
    void set_zfar(float zfar) { zfar_ = zfar; }
    void set_fov_y(float fov_y) { fov_y_ = fov_y; }
    void set_aspect_ratio(float aspect_ratio) { aspect_ratio_ = aspect_ratio; }

    glm::vec3 position() const { return position_; }
    void set_position(glm::vec3 position) { position_ = position; }

    // +x->right +y->world_up -z->heading, in camera space.
    void move_groud(glm::vec3 groud_delta)
    {
        position_ += right() * groud_delta.x + world_up * groud_delta.y + heading() * groud_delta.z;
    }

    void move_local(glm::vec3 local_delta)
    {
        position_ += right() * local_delta.x + up() * local_delta.y + forward() * local_delta.z;
    }

  private:
    static constexpr glm::vec3 world_up{0, 1, 0};
    // initialized to look along the negative Z-axis
    float yaw_{-90.0F};
    float pitch_{0.0F};

    float znear_{0.1F};
    float zfar_{10.0F};
    float fov_y_{45.0F};
    float aspect_ratio_{16.0F / 9.0F};

    glm::vec3 position_{};
};

} // namespace lc1::scene
