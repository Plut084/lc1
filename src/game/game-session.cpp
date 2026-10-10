#include "lc1/game/game-session.hpp"

#include "lc1/error.hpp"
#include "lc1/game/equipment.hpp"
#include "lc1/game/faction.hpp"
#include "lc1/game/party.hpp"
#include "lc1/game/weapon.hpp"
#include "lc1/image.hpp"
#include "lc1/model.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace lc1 {
namespace {

constexpr std::array lighting_names{"All",  "Directional", "Point",
                                    "Spot", "Sphere",      "Emission Only"};

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

// A runtime object must not collide with another of its kind: a duplicate keyid would make
// every reference to it ambiguous.
template <typename Map, typename Value>
void insert_unique(Map &map, std::uint32_t keyid, Value value, std::string_view kind)
{
    if (!map.try_emplace(keyid, std::move(value)).second)
        fail("duplicate {} keyid {}", kind, keyid);
}

} // namespace

GameSession::GameSession(Device const &device, std::filesystem::path const &asset_root,
                         Save const &save, OutputSettings output)
    : white_{device, Image::load_from_file(asset_root / "textures/prototype-white.png")},
      continent_{Continent::make_present(demo::PbrShowcase::spawn_blocks())},
      continent_view_{device, white_, continent_}, showcase_{device, continent_.spawn()},
      player_mesh_{make_player_model(player_body).gen_mesh(device)},
      player_material_{
          MaterialInfo{.parameters = {.metallic_factor = 0.0F, .roughness_factor = 0.8F},
                       .base_color = {.texture = &white_}}},
      output_{output}
{
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

    build_world(save);

    // Which character is the player comes from the save, so no keyid is special here. The
    // view needs the player's starting position, hence it is built after the world that
    // owns the character.
    player_ = character_by_id(save.player(), "player");
    player_view_ = std::make_unique<PlayerView>(continent_, player_->position());
    if (save.has_example()) {
        example_ = character_by_id(save.example(), "AI example");
        example_->set_controller(std::make_unique<AiCharacterController>(
            continent_, continent_.spawn() + glm::vec3{-2.0F, 0.0F, 0.0F},
            continent_.spawn() + glm::vec3{-5.0F, 0.0F, -8.0F}));
    }

    spdlog::info(
        "[world] {} regions, {} locations, {} roads, {} terrain chunks; fixed oblique view",
        continent_.regions().size(), continent_.locations().size(), continent_.roads().size(),
        continent_view_.draw_items().size());
    spdlog::info("[save] {}: {} factions, {} parties, {} characters, {} equipment, {} weapons",
                 save.directory().string(), save.factions().size(), save.parties().size(),
                 save.characters().size(), save.equipments().size(), save.weapons().size());
}

void GameSession::build_world(Save const &save)
{
    // Save already proved every reference resolves, so the lookups below cannot miss: a
    // dangling keyid fails the load before this runs.
    for (auto const &entry : save.factions()) {
        auto const &record = entry.second;
        insert_unique(factions_, record.keyid, std::make_unique<Faction>(record.keyid, record.name),
                      "faction");
    }
    for (auto const &entry : save.equipments()) {
        auto const &record = entry.second;
        insert_unique(equipments_, record.keyid,
                      std::make_unique<Equipment>(record.keyid, record.type,
                                                  EquipmentProperty{.armor = record.armor}),
                      "equipment");
    }
    for (auto const &entry : save.weapons()) {
        auto const &record = entry.second;
        insert_unique(weapons_, record.keyid,
                      std::make_unique<Weapon>(
                          record.keyid, record.type,
                          WeaponProperty{.attack = record.attack, .range = record.range}),
                      "weapon");
    }

    // Parties precede characters because a character record names the party it belongs to.
    for (auto const &entry : save.parties()) {
        auto const &record = entry.second;
        insert_unique(parties_, record.keyid,
                      std::make_unique<Party>(record.keyid, factions_.at(record.faction).get()),
                      "party");
    }

    // Save records carry no pose yet, so every one of them uses the prototype's transform
    // and drawable.
    scene::Object const prototype{.transform = {.position = continent_.spawn()},
                                  .mesh = &player_mesh_,
                                  .material = &player_material_};
    for (auto const &entry : save.characters()) {
        auto const &record = entry.second;
        CharacterConfig config{
            .keyid = record.keyid,
            .name = record.name,
            .sex = record.sex,
            .age = record.age,
            .party = parties_.at(record.party).get(),
            .level = record.level,
            .equipment = {},
            .weapon = {},
        };
        for (auto const &slot : record.equipment)
            config.equipment[slot.first] = equipments_.at(slot.second).get();
        for (auto const &slot : record.weapon)
            config.weapon[slot.first] = weapons_.at(slot.second).get();
        insert_unique(characters_, record.keyid,
                      std::make_unique<Character>(continent_, prototype, std::move(config)),
                      "character");
    }

    link_party_members(save);
}

void GameSession::link_party_members(Save const &save)
{
    for (auto const &entry : save.parties()) {
        auto const &record = entry.second;
        Party *const party = parties_.at(record.keyid).get();
        // keyid 0 is "no leader": a party may exist before anyone leads it.
        if (record.leader != 0)
            party->set_leader(character_by_id(record.leader, "party leader"));
        for (auto const member : record.members)
            party->add_member(character_by_id(member, "party member"));
        for (auto const captive : record.captives)
            party->add_captive(character_by_id(captive, "party captive"));
    }
}

Character *GameSession::character_by_id(std::uint32_t keyid, std::string_view what) const
{
    auto const character = characters_.find(keyid);
    if (character == characters_.end())
        fail("{} refers to unknown character {}", what, keyid);
    return character->second.get();
}

std::vector<DrawItem> GameSession::draws() const
{
    std::vector<DrawItem> items;
    items.reserve(object_capacity());
    items.append_range(continent_view_.draw_items());
    items.append_range(showcase_.draws());
    for (auto const &entry : characters_)
        items.push_back(entry.second->draw_item());
    return items;
}

std::uint32_t GameSession::object_capacity() const
{
    return static_cast<std::uint32_t>(continent_view_.draw_items().size() +
                                      showcase_.draws().size() + characters_.size());
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
}

void GameSession::update_lighting()
{
    // Hold the flashlight above ground and outside the mesh, on the aiming side.
    lights_[flashlight_index_].position = player_->position() + glm::vec3{0.0F, 1.3F, 0.0F} +
                                          player_->heading() * 0.5F + player_->right() * 0.5F;
    lights_[flashlight_index_].direction = player_->heading();
    active_lights_.clear();
    for (auto const &light : lights_)
        if (lighting_mode_ == 0 || (lighting_mode_ <= 4 && light.type == lighting_mode_ - 1))
            active_lights_.push_back(light);
}

GameLocation GameSession::location() const
{
    // Location follows the character even while the camera is panned elsewhere.
    glm::vec2 const position{player_->position().x, player_->position().z};
    auto const *region = continent_.region_at(position);
    auto const *place = continent_.location_at(position);
    auto const *road = continent_.road_at(position);
    auto const local = player_->position() - continent_.spawn();
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
    assert(player_);
    assert(player_view_);

    update_showcase(input, delta_seconds);
    player_view_->update(*player_, input.player, input.camera, delta_seconds);
    update_lighting();

    for (auto const &entry : characters_)
        entry.second->update(delta_seconds);
}

} // namespace lc1
