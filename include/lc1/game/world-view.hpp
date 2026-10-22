#pragma once

#include "lc1/audio/audio.hpp"
#include "lc1/game/continent-view.hpp"
#include "lc1/game/debug/pbr-showcase.hpp"
#include "lc1/game/game-world.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/vk/render/output-settings.hpp"
#include "lc1/vk/resources/gpu-font.hpp"

namespace lc1 {

struct WorldViewInput {
    bool cycle_lighting = false;
    bool toggle_showcase_motion = false;
    int exposure_steps = 0;
};

struct GameLocation {
    std::string_view region;
    std::string_view place;
};

// Owns presentation resources. Device and world must outlive this immovable view;
// submitted GPU work must complete before teardown. Character transforms live only in GameWorld.
class WorldView {
  public:
    WorldView(Device const &device, std::filesystem::path const &asset_root, GameWorld const &world,
              OutputSettings output);
    WorldView(WorldView const &) = delete;
    WorldView &operator=(WorldView const &) = delete;
    WorldView(WorldView &&) = delete;
    WorldView &operator=(WorldView &&) = delete;

    void update(WorldViewInput const &input, scene::FpsCamera const &camera, float delta_seconds);
    std::vector<DrawItem> draws() const;
    std::vector<WorldTextItem> world_labels(scene::FpsCamera const &camera) const;

    std::span<scene::Light const> lights() const { return active_lights_; }

    OutputSettings const &output() const { return output_; }

    GpuFont const &font() const { return font_; }

    GameLocation location() const;
    std::string_view lighting_name() const;

    bool showcase_animated() const { return showcase_animated_; }

    std::uint32_t object_capacity() const;

    audio::AudioContext const &audio_context() const { return audio_context_; }

    void set_volume(float volume_percent);

  private:
    void update_lighting();
    GameWorld const *world_; // Required non-null borrow, outlives this view.
    GpuFont const font_;
    GpuTexture const white_;
    ContinentView const continent_view_;
    demo::PbrShowcase showcase_;
    GpuMesh const player_mesh_;
    MaterialInfo const player_material_;
    std::vector<scene::Light> lights_;
    std::size_t flashlight_index_ = 0;
    std::vector<scene::Light> active_lights_;
    OutputSettings output_;
    std::size_t lighting_mode_ = 0;
    bool showcase_animated_ = true;
    float showcase_time_ = 0.0F;
    audio::AudioContext audio_context_;
    audio::Listener audio_listener_{audio_context_};
};

} // namespace lc1
