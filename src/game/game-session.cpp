#include "lc1/game/game-session.hpp"

#include "lc1/audio/audio.hpp"
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
#include "lc1/text/text-layout.hpp"
#include "lc1/text/utf8.hpp"
#include <lc1/window.hpp>

#include <imgui.h>
#include <random>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

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

// The debug UI is the only place that needs a filesystem location before the user picks one.
// Prefer the platform's home directory; fall back to the current working directory.
std::filesystem::path default_music_directory()
{
    if (char const *const home = std::getenv("HOME"); home != nullptr && *home != '\0')
        return home;
    if (char const *const profile = std::getenv("USERPROFILE");
        profile != nullptr && *profile != '\0')
        return profile;

    std::error_code error;
    auto current = std::filesystem::current_path(error);
    return error ? std::filesystem::path{} : current;
}

bool is_supported_audio_file(std::filesystem::path const &path)
{
    auto extension = path.extension().string();
    for (char &character : extension)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return extension == ".wav" || extension == ".mp3" || extension == ".ogg";
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

// A runtime object must not collide with another of its kind: a duplicate keyid would make
// every reference to it ambiguous.
template <typename Map, typename Value>
void insert_unique(Map &map, std::uint32_t keyid, Value value, std::string_view kind)
{
    if (!map.try_emplace(keyid, std::move(value)).second)
        fail("duplicate {} keyid {}", kind, keyid);
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
std::u32string font_codepoints(Save const &save)
{
    std::u32string codepoints = text::ascii_codepoints();
    auto const append = [&codepoints](std::string_view name) {
        codepoints.append(text::decode_utf8(name));
    };
    for (auto const &[_, faction] : save.factions())
        append(faction.name);
    for (auto const &[_, character] : save.characters())
        append(character.name);
    return codepoints;
}

} // namespace

GameSession::GameSession(Device const &device, std::filesystem::path const &asset_root,
                         Save const &save, OutputSettings output, Window const *window)
    : window_{window}, font_{device, text::GlyphAtlas{asset_root / font_file,
                                                      {.codepoints = font_codepoints(save)}}},
      white_{device, Image::load_from_file(asset_root / "textures/prototype-white.png")},
      continent_{Continent::make_present(demo::PbrShowcase::spawn_blocks())},
      continent_view_{device, white_, continent_}, showcase_{device, continent_.spawn()},
      player_mesh_{make_player_model(player_body).gen_mesh(device)},
      player_material_{
          MaterialInfo{.parameters = {.metallic_factor = 0.0F, .roughness_factor = 0.8F},
                       .base_color = {.texture = &white_}}},
      output_{output}, state_(State::MainMenu), volume_(50),
      music_browser_path_{default_music_directory()}
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
    spdlog::info("[save] {}: {} factions, {} parties, {} characters, {} equipment, {} weapons",
                 save.directory().string(), save.factions().size(), save.parties().size(),
                 save.characters().size(), save.equipments().size(), save.weapons().size());

    // Which character is the player comes from the save, so no keyid is special here. The
    // view needs the player's starting position, hence it is built after the world that
    // owns the character.
    player_ = character_by_id(save.player(), "player");
    player_view_ = std::make_unique<PlayerView>(continent_, player_->position());
    if (save.has_example()) {
        example_ = character_by_id(save.example(), "AI example");
        std::vector<Waypoint> waypoints{
            {.position = continent_.spawn() + glm::vec3{3.27F, 0.0F, -1.84F},
             .wait_seconds = 0.83F},
            {.position = continent_.spawn() + glm::vec3{-4.12F, 0.0F, 2.65F},
             .wait_seconds = 1.47F},
            {.position = continent_.spawn() + glm::vec3{1.58F, 0.0F, 4.03F}, .wait_seconds = 1.92F},
            {.position = continent_.spawn() + glm::vec3{-2.96F, 0.0F, -3.41F},
             .wait_seconds = 0.61F},
        };
        example_->set_controller(std::make_unique<AiCharacterController>(continent_, waypoints));
        example_->set_interactable(std::make_unique<InteractableCharacter>(example_));
    }

    spdlog::info(
        "[world] {} regions, {} locations, {} roads, {} terrain chunks; fixed oblique view",
        continent_.regions().size(), continent_.locations().size(), continent_.roads().size(),
        continent_view_.draw_items().size());

    audio_listener_.set_gain(volume_to_gain(volume_));
    try {
        play_music("/home/shelpam/Music/Adele - Make You Feel My Love.mp3");
    }
    catch (std::runtime_error const &error) {
        ; // Left unhandled. just usable on my machine. Never mind.
    }
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
        auto actual = prototype;
        // Randomize in x,z [-5,5]
        std::uint32_t const seed = record.keyid * 0x9E3779B9U;
        std::mt19937_64 rng{seed};
        std::uniform_real_distribution<float> dist{-5.0F, 5.0F};
        actual.transform.position = continent_.spawn() + glm::vec3{dist(rng), 0.0F, dist(rng)};
        insert_unique(characters_, record.keyid,
                      std::make_unique<Character>(continent_, actual, std::move(config)),
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

std::vector<WorldTextItem> GameSession::world_labels() const
{
    std::vector<WorldTextItem> labels;
    labels.append_range(showcase_.labels());

    for (auto const &[_, character] : characters_)
        labels.push_back(WorldTextItem{
            .text = std::format("{} from {}", character->config().name,
                                character->config().party->faction()->name()),
            .center = character->position() + glm::vec3{0, character->height() + 0.5, 0},
            .line_height = 0.3F,
            .normal = camera().position() -
                      (character->position() +
                       glm::vec3{0, character->height() + 0.5, 0}), // from center to cam
        });

    return labels;
}

std::vector<ScreenTextItem> GameSession::screen_labels() const
{
    auto const extent = window_->framebuffer_extent();

    std::vector<ScreenTextItem> labels;

    if (interaction_target_) {
        ScreenTextItem interact_text{
            .text = std::format("Press F to interact with {}", interaction_target_->config().name),
            .line_height = font_.atlas().line_height_for_em(32),
        };
        auto const metrics =
            text::measure_text(font_.atlas(), interact_text.text, interact_text.line_height);
        interact_text.position = {(extent.width - metrics.width) * 0.5F,
                                  (extent.height + metrics.height) * 0.5F};
        labels.push_back(interact_text);
    }

    return labels;
}

std::uint32_t GameSession::object_capacity() const
{
    return static_cast<std::uint32_t>(continent_view_.draw_items().size() +
                                      showcase_.draws().size() + characters_.size());
}

Character *GameSession::find_interaction_target() const
{
    // return example_;

    constexpr float max_distance = 3.0F;
    constexpr float min_alignment = 0.70710678F;

    // 斜视角使用身体朝向；第一人称使用水平观察方向。
    auto const forward = first_person() ? player_->heading() : player_->body_heading();

    Character *best = nullptr;
    float best_alignment = -1.0F;
    float best_distance_squared = 0.0F;

    for (auto const &[id, character] : characters_) {
        auto *candidate = character.get();

        if (candidate == player_)
            continue;

        auto const *interaction = candidate->interactable();

        if (!interaction || !interaction->can_interact(*player_))
            continue;

        auto const offset = candidate->position() - player_->position();

        // 用完整距离限制，避免选择楼上或楼下的远处角色。
        float const distance_squared = glm::dot(offset, offset);

        if (distance_squared > max_distance * max_distance)
            continue;

        // 用水平方向判断“面向”，不受角色高低差影响。
        glm::vec3 const horizontal_offset{offset.x, 0.0F, offset.z};
        float const horizontal_distance_squared = glm::dot(horizontal_offset, horizontal_offset);

        if (horizontal_distance_squared < 1e-6F)
            continue;

        auto const direction = horizontal_offset / std::sqrt(horizontal_distance_squared);

        float const alignment = glm::dot(forward, direction);

        if (alignment < min_alignment)
            continue;

        // 正式使用时在这里加入遮挡检测：
        // 从玩家眼睛到 NPC 躯干检查墙体等障碍。
        // if (!has_line_of_sight(*player_, *candidate))
        //     continue;

        bool const better =
            !best || alignment > best_alignment ||
            (alignment == best_alignment && distance_squared < best_distance_squared) ||
            (alignment == best_alignment && distance_squared == best_distance_squared &&
             id < best->config().keyid);

        if (better) {
            best = candidate;
            best_alignment = alignment;
            best_distance_squared = distance_squared;
        }
    }

    return best;
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
    update_audio_listener();
    update_lighting();

    interaction_target_ = find_interaction_target();
    if (input.player.interacting && interaction_target_) {
        interaction_target_->interactable()->interact(*player_);
    }

    for (auto const &[_, character] : characters_)
        character->update(delta_seconds);

    update_ui();
}

void GameSession::update_audio_listener()
{
    auto const &camera = player_view_->camera();
    auto const position = camera.position();
    auto const forward = camera.forward();
    auto const up = camera.up();

    audio_listener_.set_position(position.x, position.y, position.z);
    audio_listener_.set_orientation(forward.x, forward.y, forward.z, up.x, up.y, up.z);
}

void GameSession::update_ui()
{
    ImGui::Begin("lc1 debug info");
    auto [w, h] = window_->framebuffer_extent();
    ImGui::TextUnformatted(std::format("Window size: {}x{}", w, h).c_str());
    ImGui::TextUnformatted(std::format("FPS: {}", ImGui::GetIO().Framerate).c_str());
    ImGui::TextUnformatted(std::format("Lighting: {}", lighting_name()).c_str());
    ImGui::Text("Exposure: %.2f", output_.exposure);
    ImGui::TextUnformatted(
        std::format("Showcase motion: {}", showcase_animated_ ? "on" : "off").c_str());
    ImGui::TextUnformatted(
        std::format("interaction_target: {}",
                    interaction_target_ ? interaction_target_->config().name : "not found")
            .c_str());

    ImGui::SeparatorText("Music");
    ImGui::Text("Now playing: %s",
                current_music_name_.empty() ? "(none)" : current_music_name_.c_str());
    if (!current_music_path_.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", current_music_path_.string().c_str());
    if (ImGui::Button("Choose music..."))
        music_browser_open_ = true;
    if (audio_music_) {
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
            audio_source_.stop();
    }
    if (!music_error_.empty())
        ImGui::TextWrapped("Music error: %s", music_error_.c_str());

    ImGui::End();
    update_music_browser();

    switch (state_) {
    case State::MainMenu:
        ImGui::Begin("lc1 main menu##menu");
        ImGui::Button("Start Game");
        // ImGui::Button("Load Game");
        if (ImGui::Button("Settings"))
            state_ = State::InSettings;
        ImGui::Button("Quit");
        ImGui::End();
        break;
    case State::InSettings:
        ImGui::Begin("lc1 settings##menu");
        if (ImGui::SliderFloat("Volume", &volume_, 0, 100))
            audio_listener_.set_gain(volume_to_gain(volume_));
        if (ImGui::Button("Back to Main Menu"))
            state_ = State::MainMenu;
        ImGui::End();
        break;
    case State::InGame:
        break;
    }
}

void GameSession::update_music_browser()
{
    if (!music_browser_open_)
        return;

    if (!music_browser_loaded_) {
        refresh_music_browser();
        music_browser_loaded_ = true;
    }

    ImGui::SetNextWindowSize({480.0F, 360.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Choose music", &music_browser_open_)) {
        ImGui::End();
        return;
    }

    bool const has_parent = music_browser_path_.has_parent_path() &&
                            music_browser_path_.parent_path() != music_browser_path_;
    if (!has_parent)
        ImGui::BeginDisabled();
    if (ImGui::Button("Up")) {
        music_browser_path_ = music_browser_path_.parent_path();
        refresh_music_browser();
    }
    if (!has_parent)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Home")) {
        music_browser_path_ = default_music_directory();
        refresh_music_browser();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        refresh_music_browser();

    if (music_browser_path_.empty()) {
        ImGui::TextUnformatted("(no directory)");
    }
    else {
        ImGui::TextWrapped("%s", music_browser_path_.string().c_str());
    }

    if (!music_error_.empty())
        ImGui::TextWrapped("%s", music_error_.c_str());

    ImGui::Separator();
    if (ImGui::BeginChild("music files", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_Borders)) {
        bool navigated = false;
        for (std::size_t index = 0; index < music_browser_entries_.size(); ++index) {
            auto const &entry = music_browser_entries_[index];
            std::string const display_name = entry.path.filename().string();
            std::string const label =
                entry.is_directory ? std::format("[dir] {}", display_name) : display_name;

            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(label.c_str())) {
                if (entry.is_directory) {
                    music_browser_path_ = entry.path;
                    refresh_music_browser();
                    navigated = true;
                }
                else {
                    play_music(entry.path);
                }
            }
            ImGui::PopID();
            if (navigated)
                break;
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void GameSession::refresh_music_browser()
{
    music_browser_entries_.clear();
    music_error_.clear();

    if (music_browser_path_.empty()) {
        music_error_ = "No directory selected";
        return;
    }

    std::error_code error;
    std::filesystem::directory_iterator iterator{
        music_browser_path_,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    if (error) {
        music_error_ =
            std::format("Cannot open {}: {}", music_browser_path_.string(), error.message());
        return;
    }

    std::filesystem::directory_iterator const end;
    while (iterator != end) {
        auto const &entry = *iterator;
        std::error_code type_error;
        bool const directory = entry.is_directory(type_error);
        if (!type_error) {
            if (directory || is_supported_audio_file(entry.path()))
                music_browser_entries_.push_back({.path = entry.path(), .is_directory = directory});
        }

        iterator.increment(error);
        if (error) {
            music_error_ =
                std::format("Cannot read {}: {}", music_browser_path_.string(), error.message());
            break;
        }
    }

    std::ranges::sort(music_browser_entries_,
                      [](MusicBrowserEntry const &left, MusicBrowserEntry const &right) {
                          if (left.is_directory != right.is_directory)
                              return left.is_directory;
                          return left.path.filename() < right.path.filename();
                      });
}

void GameSession::play_music(std::filesystem::path const &path)
{
    try {
        audio::Sound music = audio::Sound::load_from_file(audio_context_, path);
        audio_source_.play(music);
        audio_music_ = std::move(music);
        current_music_path_ = path;
        current_music_name_ = path.filename().string();
        music_error_.clear();
        spdlog::info("[music] playing {}", path.string());
    }
    catch (std::exception const &error) {
        music_error_ = error.what();
        spdlog::warn("[music] cannot play {}: {}", path.string(), error.what());
    }
}

} // namespace lc1
