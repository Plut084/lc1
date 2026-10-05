#pragma once

#include "lc1/game/ai-character-controller.hpp"
#include "lc1/game/character.hpp"
#include "lc1/game/continent-view.hpp"
#include "lc1/game/debug/pbr-showcase.hpp"
#include "lc1/game/player-view.hpp"
#include "lc1/vk/render/output-settings.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"

#include <filesystem>
#include <string_view>

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
// Device and Renderer must outlive the session; wait for rendering before teardown.
class GameSession {
  public:
    GameSession(Device const &device, Renderer &renderer, std::filesystem::path const &asset_root,
                OutputSettings output);
    GameSession(GameSession const &) = delete;
    GameSession &operator=(GameSession const &) = delete;
    GameSession(GameSession &&) = delete;
    GameSession &operator=(GameSession &&) = delete;

    void update(GameInput const &input, float delta_seconds);
    // Apply cursor capture after toggling, before collecting this frame's look delta.
    void toggle_camera_view() { player_view_.toggle(player_); }
    bool first_person() const { return !player_view_.oblique(); }
    void set_aspect_ratio(float aspect_ratio) { player_view_.set_aspect_ratio(aspect_ratio); }

    GameLocation location() const;
    std::string_view lighting_name() const;

    scene::FpsCamera const &camera() const { return player_view_.camera(); }
    std::span<DrawItem const> draws() const { return draws_; }
    std::span<scene::Light const> lights() const { return active_lights_; }
    OutputSettings const &output() const { return output_; }
    std::uint32_t object_capacity() const { return static_cast<std::uint32_t>(draws_.size()); }

    // std::vector<DrawItem> draws() const
    // {
    //     std::vector<DrawItem> items;
    //
    //     return items;
    // }

  private:
    void update_showcase(GameInput const &input, float delta_seconds);
    void update_lighting();

    // Owners precede their borrowers: materials reference textures, draws reference
    // meshes/materials, and the character/controllers reference the continent.
    GpuTexture const white_;
    Continent const continent_;
    ContinentView const continent_view_;
    demo::PbrShowcase showcase_;
    GpuMesh const player_mesh_;
    GpuMaterial const player_material_;
    Character player_;
    PlayerView player_view_;
    std::vector<DrawItem> draws_;
    // Slots inside draws_, written where the constructor appends. Both are
    // rewritten in place every frame: DrawItem bakes a model matrix, so the
    // player's copy goes stale as it moves. Named offsets rather than back() --
    // an append after them would silently overwrite the wrong draw.
    std::size_t showcase_offset_ = 0;
    std::size_t player_offset_ = 0;
    std::vector<scene::Light> lights_;
    // The flashlight follows the player; same refresh-in-place contract.
    std::size_t flashlight_index_ = 0;
    std::vector<scene::Light> active_lights_;
    OutputSettings output_;
    std::size_t lighting_mode_ = 0;
    bool showcase_animated_ = true;
    float showcase_time_ = 0.0F;

    std::vector<std::unique_ptr<Character>> characters_;
    std::vector<std::unique_ptr<AiCharacterController>> controllers_;
    std::size_t ch_index_;
    // std::vector;
};

} // namespace lc1
