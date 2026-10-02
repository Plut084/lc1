#pragma once

#include "lc1/scene/camera.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/lights/point-light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"
#include "lc1/scene/object.hpp"

#include <ranges>
#include <variant>

namespace lc1::scene {

class Renderer;

class Scene {
  public:
    // void render(Renderer &renderer);
    FpsCamera const *active_camera() const { return active_camera_; }

    std::vector<Light> lights() const
    {
        return lights_ | std::views::transform([](LightType const &light) {
                   return std::visit([](auto const &l) { return to_light(l); }, light);
               }) |
               std::ranges::to<std::vector>();
    }

    std::vector<DrawItem> draws() const
    {
        return objects_ | std::views::transform([](Object const &obj) { return obj.draw_item(); }) |
               std::ranges::to<std::vector>();
    }

  private:
    using LightType = std::variant<PointLight, SpotLight, SphereLight, DirectionalLight>;
    std::vector<FpsCamera> cameras_;
    std::vector<LightType> lights_;
    std::vector<Object> objects_;

    FpsCamera *active_camera_ = nullptr;
};

} // namespace lc1::scene
