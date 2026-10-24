#include "lc1/game/session/game-session.hpp"
#include "lc1/error.hpp"
#include "lc1/game/actions.hpp"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <utility>

namespace lc1 {
GameSession::GameSession(Device const &device, std::filesystem::path const &asset_root,
                         Save const &save, OutputSettings output, Window const *window)
    : window_(window), world_(save, Continent::make_present(demo::PbrShowcase::spawn_blocks())),
      view_(device, asset_root, world_, output),
      player_view_(world_.continent(), world_.player().position()), music_(view_.audio_context())
{
    if (!window_)
        fail("game session requires a window");
    spdlog::info("[save] {}: {} factions, {} parties, {} characters, {} equipment, {} weapons",
                 save.directory().string(), save.factions().size(), save.parties().size(),
                 save.characters().size(), save.equipments().size(), save.weapons().size());
    spdlog::info("[world] {} regions, {} locations, {} roads; fixed oblique view",
                 world_.continent().regions().size(), world_.continent().locations().size(),
                 world_.continent().roads().size());
}

void GameSession::update_view_mode(GameInputRouter const &input)
{
    if (ui_.gameplay_enabled() && window_->focused() &&
        input.active_context() == GameInputContext::Gameplay &&
        !ImGui::GetIO().WantCaptureKeyboard && input.pressed(GameAction::ToggleCameraView))
        player_view_.toggle(world_.player());
}

void GameSession::update(GameInputRouter const &input, float delta_seconds)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("invalid session update time");
    float const elapsed = std::min(delta_seconds, 0.1F);
    auto const &devices = window_->input();
    auto const &ui = ImGui::GetIO();
    bool const controls_enabled = ui_.gameplay_enabled() && window_->focused() &&
                                  input.active_context() == GameInputContext::Gameplay;
    bool const keyboard_enabled = controls_enabled && !ui.WantCaptureKeyboard;
    bool const mouse_enabled = controls_enabled && !ui.WantCaptureMouse;
    if (keyboard_enabled && input.pressed(GameAction::SaveGame))
        save_requested_ = true;
    // The controller supplies intent; GameWorld applies it through Character and then
    // handles the world-level dialog interaction. Whether an idle player accepts ambient
    // requests is a Character rule, not an input rule.
    world_.update(elapsed, WorldInteraction{
                               .player_intent = player_view_.resolve_input(
                                   world_.player(), input, devices.cursor_delta(), keyboard_enabled,
                                   mouse_enabled),
                               .player_input_enabled = keyboard_enabled,
                               .response_index = !keyboard_enabled                            ? -1
                                                 : input.pressed(GameAction::DialogResponse0) ? 0
                                                 : input.pressed(GameAction::DialogResponse1) ? 1
                                                 : input.pressed(GameAction::DialogResponse2) ? 2
                                                 : input.pressed(GameAction::DialogResponse3) ? 3
                                                                                              : -1,
                           });
    if (mouse_enabled)
        player_view_.update_pointer(devices, window_->content_size(),
                                    window_->hovered() && !window_->cursor_captured(), elapsed);
    player_view_.update_camera(world_.player(), elapsed);
    view_.update(
        {
            .cycle_lighting = keyboard_enabled && input.pressed(GameAction::CycleLighting),
            .toggle_showcase_motion =
                keyboard_enabled && input.pressed(GameAction::ToggleShowcaseMotion),
            .exposure_steps =
                keyboard_enabled ? static_cast<int>(input.pressed(GameAction::IncreaseExposure)) -
                                       static_cast<int>(input.pressed(GameAction::DecreaseExposure))
                                 : 0,
        },
        player_view_.camera(), elapsed);

    auto const *target = world_.interaction_target();
    auto const actions = ui_.update(
        {
            .extent = window_->framebuffer_extent(),
            .lighting_name = view_.lighting_name(),
            .exposure = view_.output().exposure,
            .showcase_animated = view_.showcase_animated(),
            .interaction_target =
                target ? std::string_view{target->config().name} : std::string_view{},
        },
        music_);
    if (actions.start)
        ui_.start_game();
    quit_requested_ = quit_requested_ || actions.quit;
    save_requested_ = save_requested_ || actions.save;
    if (actions.volume)
        view_.set_volume(*actions.volume);
}

bool GameSession::take_save_request()
{
    return std::exchange(save_requested_, false);
}

} // namespace lc1
