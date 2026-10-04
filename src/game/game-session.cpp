#include "lc1/game/game-session.hpp"

#include "lc1/image.hpp"
#include "lc1/model.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace lc1 {
namespace {

constexpr std::array lighting_names{"All",  "Directional", "Point",
                                    "Spot", "Sphere",      "Emission Only"};

// A visible, body-sized placeholder for the player now that the camera is above them.
Model make_player_model()
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    auto box = [&](float radius, float bottom, float top, glm::vec3 color) {
        // Separate vertices at hard edges so each box face has its own normal.
        std::array const points{
            glm::vec3{-radius, bottom, -radius}, glm::vec3{radius, bottom, -radius},
            glm::vec3{radius, top, -radius},     glm::vec3{-radius, top, -radius},
            glm::vec3{-radius, bottom, radius},  glm::vec3{radius, bottom, radius},
            glm::vec3{radius, top, radius},      glm::vec3{-radius, top, radius}};
        for (auto const face : std::array{std::array{4U, 5U, 6U, 7U}, std::array{1U, 0U, 3U, 2U},
                                          std::array{0U, 4U, 7U, 3U}, std::array{5U, 1U, 2U, 6U},
                                          std::array{3U, 7U, 6U, 2U}, std::array{0U, 1U, 5U, 4U}}) {
            auto const base = static_cast<std::uint32_t>(vertices.size());
            auto const normal = glm::normalize(
                glm::cross(points[face[1]] - points[face[0]], points[face[2]] - points[face[0]]));
            for (auto const index : face)
                vertices.push_back({points[index], normal, color, {0.5F, 0.5F}});
            for (auto const index : {0U, 1U, 2U, 0U, 2U, 3U})
                indices.push_back(base + index);
        }
    };
    constexpr CharacterConfig config;
    box(config.half_width, 0.0F, 1.2F, {0.06F, 0.25F, 0.8F});
    box(0.22F, 1.2F, config.height, {0.95F, 0.65F, 0.15F});
    return Model{std::move(vertices), std::move(indices)};
}

} // namespace

GameSession::GameSession(Device const &device, Renderer &renderer,
                         std::filesystem::path const &asset_root, OutputSettings output)
    : white_{device, Image::load_from_file(asset_root / "textures/prototype-white.png")},
      continent_{Continent::make_present(demo::PbrShowcase::spawn_blocks())},
      continent_view_{device, renderer, white_, continent_},
      showcase_{device, renderer, continent_.spawn()},
      player_mesh_{make_player_model().gen_mesh(device)},
      player_material_{renderer.make_material(white_)},
      player_{continent_,
              {.transform = {.position = continent_.spawn()},
               .mesh = &player_mesh_,
               .material = &player_material_}},
      player_view_{continent_, player_.position()}, output_{output}
{
    auto const terrain_draws = continent_view_.draw_items();
    draws_.assign(terrain_draws.begin(), terrain_draws.end());
    showcase_offset_ = draws_.size();
    draws_.insert(draws_.end(), showcase_.draws().begin(), showcase_.draws().end());
    player_offset_ = draws_.size();
    draws_.push_back(player_.draw_item());

    scene::DirectionalLight const sun{
        .base = {.color = glm::vec3{1.0F, 0.95F, 0.9F}, .intensity = 0.3F},
        .direction = glm::normalize(glm::vec3{-1.0F, -1.0F, -1.0F})};
    scene::SphereLight const lamp{.base = {.color = glm::vec3{1.0F, 0.5F, 0.5F}, .intensity = 128},
                                  .position = continent_.spawn() + glm::vec3{3.0F, 10.0F, 0.0F},
                                  .radius = 1.0F,
                                  .shadow_sample_count = 2};
    scene::SpotLight const spotlight{
        .base = {.color = glm::vec3{0.5F, 0.5F, 1.0F}, .intensity = 64},
        .position = continent_.spawn() + glm::vec3{-3.0F, 10.0F, 0.0F},
        .direction = glm::normalize(glm::vec3{1.0F, -1.0F, 0.0F}),
        .inner_angle = glm::radians(15.0F),
        .outer_angle = glm::radians(30.0F)};
    lights_ = {to_light(sun), to_light(lamp)};
    lights_.insert(lights_.end(), showcase_.lights().begin(), showcase_.lights().end());
    flashlight_index_ = lights_.size();
    lights_.push_back(to_light(spotlight));
    active_lights_ = lights_;

    characters_.push_back(std::make_unique<Character>(
        continent_,
        scene::Object{.transform = {.position = continent_.spawn() + glm::vec3{0.0F, 0.0F, 5.0F}},
                      .mesh = &player_mesh_,
                      .material = &player_material_}));
    controllers_.push_back(std::make_unique<AiCharacterController>(
        continent_, continent_.spawn() + glm::vec3{2.0F, 0.0F, 0.0F},
        continent_.spawn() + glm::vec3{5.0F, 0.0F, 8.0F}));

    ch_index_ = draws_.size();
    draws_.push_back(DrawItem{.mesh = &player_mesh_, .material = &player_material_});

    spdlog::info(
        "[world] {} regions, {} locations, {} roads, {} terrain chunks; fixed oblique view",
        continent_.regions().size(), continent_.locations().size(), continent_.roads().size(),
        terrain_draws.size());
}

void GameSession::update_showcase(GameInput const &input, float delta_seconds)
{
    if (input.cycle_lighting)
        lighting_mode_ = (lighting_mode_ + 1) % lighting_names.size();
    if (input.toggle_showcase_motion)
        showcase_animated_ = !showcase_animated_;
    output_.exposure =
        std::clamp(output_.exposure * std::exp2(static_cast<float>(input.exposure_steps) * 0.5F),
                   0.03125F, 16.0F);
    if (showcase_animated_)
        showcase_time_ += std::clamp(delta_seconds, 0.0F, 0.1F);
    showcase_.update(showcase_time_);
    std::copy(showcase_.draws().begin(), showcase_.draws().end(),
              draws_.begin() + showcase_offset_);
}

void GameSession::update_lighting()
{
    // Hold the flashlight above ground and outside the mesh, on the aiming side.
    lights_[flashlight_index_].position = player_.position() + glm::vec3{0.0F, 1.3F, 0.0F} +
                                          player_.heading() * 0.5F + player_.right() * 0.5F;
    lights_[flashlight_index_].direction = player_.heading();
    active_lights_.clear();
    for (auto const &light : lights_)
        if (lighting_mode_ == 0 || (lighting_mode_ <= 4 && light.type == lighting_mode_ - 1))
            active_lights_.push_back(light);
}

GameLocation GameSession::location() const
{
    // Location follows the character even while the camera is panned elsewhere.
    glm::vec2 const position{player_.position().x, player_.position().z};
    auto const *region = continent_.region_at(position);
    auto const *place = continent_.location_at(position);
    auto const *road = continent_.road_at(position);
    auto const local = player_.position() - continent_.spawn();
    bool const in_showcase = std::abs(local.x) < 24 && std::abs(local.z) < 23;
    return {.region = region->name,
            .place = in_showcase ? "Material Showcase"
                     : place     ? std::string_view{place->name}
                     : road      ? std::string_view{road->name}
                                 : "Wilderness"};
}

std::string_view GameSession::lighting_name() const
{
    return lighting_names[lighting_mode_];
}

void GameSession::update(GameInput const &input, float delta_seconds)
{
    update_showcase(input, delta_seconds);
    player_view_.update(player_, input.player, input.camera, delta_seconds);
    draws_[player_offset_] = player_.draw_item();
    update_lighting();

    controllers_[0]->update(delta_seconds, *characters_[0]);
    draws_[ch_index_] = characters_[0]->draw_item();
}

} // namespace lc1
