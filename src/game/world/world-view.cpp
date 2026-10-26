#include "lc1/game/world/world-view.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/image.hpp"
#include "lc1/model.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"
#include "lc1/text/utf8.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace lc1 {
namespace {
constexpr std::array lighting_names{"All",  "Directional", "Point",
                                    "Spot", "Sphere",      "Emission Only"};

// UI volume is linear in slider position, but AL_GAIN is a linear amplitude.
// Squaring gives the low end finer control, which feels closer to a normal volume knob.
float volume_to_gain(float volume_percent)
{
    float const normalized = std::clamp(volume_percent / 100.0F, 0.0F, 1.0F);
    return normalized * normalized;
}

// The player's body. The placeholder mesh below and the collision body are built from the
// same numbers, so the two cannot silently disagree.
constexpr CharacterBody player_body{};

// A visible, body-sized placeholder for the player now that the camera is above them.
Model make_player_model(CharacterBody const &body)
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
                vertices.push_back({.position = points[index],
                                    .normal = normal,
                                    .color = color,
                                    .uv = {0.5F, 0.5F}});
            for (auto const index : {0U, 1U, 2U, 0U, 2U, 3U})
                indices.push_back(base + index);
        }
    };
    box(body.half_width, 0.0F, 1.2F, {0.06F, 0.25F, 0.8F});
    box(0.22F, 1.2F, body.height, {0.95F, 0.65F, 0.15F});
    return Model{std::move(vertices), std::move(indices)};
}

// One font per frame, and its atlas is fixed once constructed: the face has to cover every
// script save data can hold, because the atlas requests only the codepoints that will be
// drawn. `runtime/assets/` is not tracked, so this is a runtime input; see
// docs/text-rendering.md.
constexpr std::string_view font_file = "fonts/SourceHanSansCN-Regular.otf";

// The atlas holds one fixed set of glyphs and layout silently skips what it does not hold,
// so every character the world or the HUD can draw has to be requested up front. Names come
// from save records, which is where non-ASCII text enters the game; the remaining drawn text
// is ASCII (`ascii_codepoints()`, the map's own names and the debug UI).
std::u32string font_codepoints(GameWorld const &world)
{
    std::u32string codepoints = text::ascii_codepoints();
    auto const append = [&codepoints](std::string_view name) {
        codepoints.append(text::decode_utf8(name));
    };
    for (auto const *character : world.characters()) {
        append(character->config().name);
        append(character->faction()->name());
    }
    return codepoints;
}

} // namespace

WorldView::WorldView(Device const &device, std::filesystem::path const &asset_root,
                     GameWorld const &world, OutputSettings output)
    : world_(&world), font_{device, text::GlyphAtlas{asset_root / font_file,
                                                     {.codepoints = font_codepoints(world)}}},
      white_{device, Image::load_from_file(asset_root / "textures/prototype-white.png")},
      continent_view_{device, white_, world.continent()},
      showcase_{device, world.continent().spawn()},
      player_mesh_{make_player_model(player_body).gen_mesh(device)},
      player_material_{
          MaterialInfo{.parameters = {.metallic_factor = 0.0F, .roughness_factor = 0.8F},
                       .base_color = {.texture = &white_}}},
      output_(output)
{
    scene::DirectionalLight const sun{
        .base = {.color = glm::vec3{1.0F, 0.95F, 0.9F}, .intensity = 0.3F},
        .direction = glm::normalize(glm::vec3{-1.0F, -1.0F, -1.0F})};
    scene::SphereLight const lamp{.base = {.color = glm::vec3{1.0F, 0.5F, 0.5F}, .intensity = 128},
                                  .position =
                                      world_->continent().spawn() + glm::vec3{3.0F, 10.0F, 0.0F},
                                  .radius = 1.0F,
                                  .shadow_sample_count = 2};
    scene::SpotLight const spotlight{
        .base = {.color = glm::vec3{0.5F, 0.5F, 1.0F}, .intensity = 64},
        .position = world_->continent().spawn() + glm::vec3{-3.0F, 10.0F, 0.0F},
        .direction = glm::normalize(glm::vec3{1.0F, -1.0F, 0.0F}),
        .inner_angle = glm::radians(15.0F),
        .outer_angle = glm::radians(30.0F)};
    lights_ = {to_light(sun), to_light(lamp)};
    lights_.insert(lights_.end(), showcase_.lights().begin(), showcase_.lights().end());
    flashlight_index_ = lights_.size();
    lights_.push_back(to_light(spotlight));
    active_lights_ = lights_;

    set_volume(50.0F);
}

std::vector<DrawItem> WorldView::draws() const
{
    std::vector<DrawItem> items;
    items.reserve(object_capacity());
    items.append_range(continent_view_.draw_items());
    items.append_range(showcase_.draws());
    for (auto const *character : world_->characters())
        items.push_back({.mesh = &player_mesh_,
                         .material = &player_material_,
                         .model = character->transform().model_matrix(),
                         .casts_shadow = true});
    return items;
}

std::vector<WorldTextItem> WorldView::world_labels(scene::FpsCamera const &camera) const
{
    std::vector<WorldTextItem> labels;
    labels.append_range(showcase_.labels());

    if (world_->continent().identity().id == "present") {
        glm::vec3 const center{27.0F, 3.4F, 34.0F};
        labels.push_back({.text = "Patrol House",
                          .center = center,
                          .line_height = 0.5F,
                          .normal = camera.position() - center});
    }

    for (auto const *character : world_->characters())
        labels.push_back(WorldTextItem{
            .text = std::format("{} from {}", character->config().name,
                                character->config().party->faction()->name()),
            .center = character->position() + glm::vec3{0, character->height() + 0.5, 0},
            .line_height = 0.3F,
            .normal =
                camera.position() - (character->position() + glm::vec3{0, character->height() + 0.5,
                                                                       0}), // from center to cam
        });

    for (auto const *character : world_->characters()) {
        auto const *dialog = world_->dialogs().find(*character);
        if (!dialog || dialog->contains(world_->player()) || dialog->speaker() != character)
            continue;
        auto const center = character->position() + glm::vec3{0, character->height() + 1.0F, 0};
        labels.push_back(WorldTextItem{.text = std::string{dialog->current_speech_text()},
                                       .center = center,
                                       .line_height = 0.25F,
                                       .normal = camera.position() - center});
    }
    return labels;
}

std::uint32_t WorldView::object_capacity() const
{
    return static_cast<std::uint32_t>(continent_view_.draw_items().size() +
                                      showcase_.draws().size() + world_->characters().size());
}

void WorldView::update_lighting()
{
    // Hold the flashlight above ground and outside the mesh, on the aiming side.
    lights_[flashlight_index_].position =
        world_->player().position() + glm::vec3{0.0F, 1.3F, 0.0F} +
        world_->player().heading() * 0.5F + world_->player().right() * 0.5F;
    lights_[flashlight_index_].direction = world_->player().heading();
    active_lights_.clear();
    for (auto const &light : lights_)
        if (lighting_mode_ == 0 || (lighting_mode_ <= 4 && light.type == lighting_mode_ - 1))
            active_lights_.push_back(light);
}

GameLocation WorldView::location() const
{
    // Location follows the character even while the camera is panned elsewhere.
    glm::vec2 const position{world_->player().position().x, world_->player().position().z};
    auto const *region = world_->continent().region_at(position);
    auto const *place = world_->continent().location_at(position);
    auto const *road = world_->continent().road_at(position);
    auto const local = world_->player().position() - world_->continent().spawn();
    bool const in_showcase = std::abs(local.x) < 24 && std::abs(local.z) < 23;
    return {.region = region->name,
            .place = in_showcase ? "Material Showcase"
                     : place     ? std::string_view{place->name}
                     : road      ? std::string_view{road->name}
                                 : "Wilderness"};
}

std::string_view WorldView::lighting_name() const
{
    return lighting_names[lighting_mode_];
}

void WorldView::update(WorldViewInput const &input, scene::FpsCamera const &camera,
                       float delta_seconds)
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
    update_lighting();
    auto const position = camera.position();
    auto const forward = camera.forward();
    auto const up = camera.up();
    audio_listener_.set_position(position.x, position.y, position.z);
    audio_listener_.set_orientation(forward.x, forward.y, forward.z, up.x, up.y, up.z);
}

void WorldView::set_volume(float volume_percent)
{
    audio_listener_.set_gain(volume_to_gain(volume_percent));
}

} // namespace lc1
