#pragma once

#include "lc1/scene/lights/light.hpp"

#include "lc1/game/ai-character-controller.hpp"
#include "lc1/game/character.hpp"
#include "lc1/game/continent-view.hpp"
#include "lc1/game/debug/pbr-showcase.hpp"
#include "lc1/game/faction.hpp"
#include "lc1/game/party.hpp"
#include "lc1/game/player-view.hpp"
#include "lc1/game/save.hpp"
#include "lc1/vk/render/output-settings.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lc1 {

// Resolved game commands. The application supplies focus/context-gated input.
struct GameInput {
    PlayerInput player;
    MapCameraInput camera;
    bool cycle_lighting = false;
    bool toggle_showcase_motion = false;
    int exposure_steps = 0;
};

struct GameLocation {
    std::string_view region;
    std::string_view place;
};

// The playable prototype: world resources, player, camera and exhibit state.
// Device must outlive the session; wait for rendering before teardown.
class GameSession {
  public:
    GameSession(Device const &device, std::filesystem::path const &asset_root, Save const &save,
                OutputSettings output);
    GameSession(GameSession const &) = delete;
    GameSession &operator=(GameSession const &) = delete;
    GameSession(GameSession &&) = delete;
    GameSession &operator=(GameSession &&) = delete;
    ~GameSession() = default;

    void update(GameInput const &input, float delta_seconds);

    // Apply cursor capture after toggling, before collecting this frame's look delta.
    void toggle_camera_view() { player_view_->toggle(*player_); }

    bool first_person() const { return !player_view_->oblique(); }

    void set_aspect_ratio(float aspect_ratio) { player_view_->set_aspect_ratio(aspect_ratio); }

    GameLocation location() const;
    std::string_view lighting_name() const;

    scene::FpsCamera const &camera() const { return player_view_->camera(); }

    // Build a frame snapshot from the current world, showcase and character state.
    std::vector<DrawItem> draws() const;

    std::span<scene::Light const> lights() const { return active_lights_; }

    OutputSettings const &output() const { return output_; }

    std::uint32_t object_capacity() const;

  private:
    // The runtime world is built once, from the save data. Factions, equipment and weapons
    // come first because parties and characters borrow them; a character names its party,
    // and party membership names characters, so it is linked last.
    void build_world(Save const &save);
    void link_party_members(Save const &save);
    Character *character_by_id(std::uint32_t keyid, std::string_view what) const;

    void update_showcase(GameInput const &input, float delta_seconds);
    void update_lighting();

    // Owners precede their borrowers: materials reference textures, draws reference
    // meshes/materials, and the character/controllers reference the continent.
    GpuTexture const white_;
    Continent const continent_;
    ContinentView const continent_view_;
    demo::PbrShowcase showcase_;
    GpuMesh const player_mesh_;
    MaterialInfo const player_material_;
    std::vector<scene::Light> lights_;
    // Slot of the light that follows the player.
    std::size_t flashlight_index_ = 0;
    std::vector<scene::Light> active_lights_;
    OutputSettings output_;
    std::size_t lighting_mode_ = 0;
    bool showcase_animated_ = true;
    float showcase_time_ = 0.0F;

    std::unordered_map<std::uint32_t, std::unique_ptr<Faction>> factions_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Equipment>> equipments_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Weapon>> weapons_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Character>> characters_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Party>> parties_;

    // Borrowed from the character table once the world is built: every character, the
    // player included, comes from save data. The view is built at the same point, because
    // it needs the player's start position.
    Character *player_ = nullptr;
    std::unique_ptr<PlayerView> player_view_;

    Character *example_ = nullptr;
};

} // namespace lc1
