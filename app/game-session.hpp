#pragma once

#include "pbr-showcase.hpp"
#include "player-view.hpp"

#include "lc1/game/continent-view.hpp"
#include "lc1/scene/character.hpp"
#include "lc1/vk/gpu-texture.hpp"

#include <filesystem>
#include <string>

namespace lc1 {
class InputRouter;
class Window;
} // namespace lc1

namespace lc1::app {

// The playable prototype: world resources, player, camera and exhibit state.
// Device and Renderer must outlive the session; wait for rendering before teardown.
class GameSession {
  public:
    GameSession(Window &window, Device const &device, Renderer &renderer,
                std::filesystem::path const &asset_root, OutputSettings output);
    GameSession(GameSession const &) = delete;
    GameSession &operator=(GameSession const &) = delete;
    GameSession(GameSession &&) = delete;
    GameSession &operator=(GameSession &&) = delete;

    void update(Window &window, InputRouter const &router, float delta_seconds);
    void set_aspect_ratio(float aspect_ratio) { player_view_.set_aspect_ratio(aspect_ratio); }

    scene::FpsCamera const &camera() const { return player_view_.camera(); }
    std::span<DrawItem const> draws() const { return draws_; }
    std::span<scene::Light const> lights() const { return active_lights_; }
    OutputSettings const &output() const { return output_; }
    std::uint32_t object_capacity() const { return static_cast<std::uint32_t>(draws_.size()); }

  private:
    void update_showcase(InputRouter const &router, bool focused, float delta_seconds);
    void update_player(Window &window, InputRouter const &router, float delta_seconds);
    void update_lighting();
    void update_title(Window &window);

    // Owners precede their borrowers: materials reference textures, draws reference
    // meshes/materials, and the character/controllers reference the continent.
    GpuTexture const white_;
    Continent const continent_;
    ContinentView const continent_view_;
    demo::PbrShowcase showcase_;
    GpuMesh const player_mesh_;
    GpuMaterial const player_material_;
    scene::Character player_;
    PlayerView player_view_;
    std::vector<DrawItem> draws_;
    std::size_t showcase_offset_ = 0;
    std::vector<scene::Light> lights_;
    std::vector<scene::Light> active_lights_;
    OutputSettings output_;
    std::size_t lighting_mode_ = 0;
    bool showcase_animated_ = true;
    float showcase_time_ = 0.0F;
    std::string current_title_;
};

} // namespace lc1::app
