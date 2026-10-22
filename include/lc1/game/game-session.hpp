#pragma once

#include "lc1/audio/audio.hpp"
#include "lc1/game/dialog.hpp"
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
#include "lc1/vk/resources/gpu-font.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lc1 {

class Window;
class InputRouter;

struct GameLocation {
    std::string_view region;
    std::string_view place;
};

// Local playable prototype: gameplay updates and client-side presentation resources in one owner.
// This is not the future authoritative server GameMode or a networked ClientGameState.
// Device must outlive the session; wait for rendering before teardown.
class GameSession {
  public:
    GameSession(Device const &device, std::filesystem::path const &asset_root, Save const &save,
                OutputSettings output, Window const *window);
    GameSession(GameSession const &) = delete;
    GameSession &operator=(GameSession const &) = delete;
    GameSession(GameSession &&) = delete;
    GameSession &operator=(GameSession &&) = delete;

    ~GameSession() { dialogs_.clear(); }

    void update(InputRouter const &input, float delta_seconds);
    SerializedWorld snapshot() const;
    bool take_save_request();

    void set_save_status(std::string status) { save_status_ = std::move(status); }

    // Call before applying cursor capture and update(): capture changes reset the mouse baseline.
    void update_view_mode(InputRouter const &input);

    bool first_person() const { return !player_view_->oblique(); }

    void set_aspect_ratio(float aspect_ratio) { player_view_->set_aspect_ratio(aspect_ratio); }

    GameLocation location() const;
    std::string_view lighting_name() const;

    scene::FpsCamera const &camera() const { return player_view_->camera(); }

    // Build a frame snapshot from the current world, showcase and character state.
    std::vector<DrawItem> draws() const;

    std::span<scene::Light const> lights() const { return active_lights_; }

    OutputSettings const &output() const { return output_; }

    // The world's face, shared by panel labels and the application's HUD text.
    GpuFont const &font() const { return font_; }

    std::vector<WorldTextItem> world_labels() const;
    std::vector<ScreenTextItem> screen_labels() const;

    std::string_view player_name() const { return player_->config().name; }

    std::uint32_t object_capacity() const;

    Character *find_interaction_target() const;

  private:
    enum class State : std::uint8_t {
        MainMenu,
        InSettings,
        InGame,
    };

    // The runtime world is built once, from the save data. Factions, equipment and weapons
    // come first because parties and characters borrow them; a character names its party,
    // and party membership names characters, so it is linked last.
    void build_world(Save const &save);
    void link_party_members(Save const &save);
    Character *character_by_id(std::uint32_t keyid, std::string_view what) const;

    void update_showcase(InputRouter const &input, bool controls_enabled, float delta_seconds);
    void update_lighting();
    void update_audio_listener();
    void update_ui();
    void update_music_browser();
    void refresh_music_browser();
    void play_music(std::filesystem::path const &path);

    // Non-owning and non-null; Application's Window outlives this session. Window provides input
    // state and dimensions; Application owns event polling and cursor capture.
    Window const *window_;

    // Owners precede their borrowers: materials reference textures, draws reference
    // meshes/materials, and the character/controllers reference the continent.
    GpuFont const font_;
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
    // Interactions borrow this owner; it does not dereference participants during teardown.
    DialogSystem dialogs_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Character>> characters_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Party>> parties_;
    // Borrows the fixed character roster; rebuild if runtime spawning/removal is introduced.
    std::vector<Character *> dialog_participants_;

    // Prototype in-session UI state and music-browser state; ImGui's Vulkan backend stays in app.
    State state_;
    bool save_requested_ = false;
    std::string save_status_ = "F6: Save game";
    float volume_;

    struct MusicBrowserEntry {
        std::filesystem::path path;
        bool is_directory{};
    };

    std::filesystem::path music_browser_path_;
    std::vector<MusicBrowserEntry> music_browser_entries_;
    std::string music_error_;
    std::string current_music_name_;
    std::filesystem::path current_music_path_;
    bool music_browser_open_ = false;
    bool music_browser_loaded_ = false;
    // Borrowed from the character table once the world is built: every character, the
    // player included, comes from save data. The view is built at the same point, because
    // it needs the player's start position.
    Character *player_ = nullptr;
    std::unique_ptr<PlayerView> player_view_;

    Character *example_ = nullptr;

    Character *interaction_target_{};

    audio::AudioContext audio_context_;
    audio::Listener audio_listener_{audio_context_};
    audio::Source audio_source_{audio_context_};
    audio::Sound audio_music_;
};

} // namespace lc1
