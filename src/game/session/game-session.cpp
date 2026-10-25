#include "lc1/game/session/game-session.hpp"
#include "lc1/error.hpp"
#include "lc1/game/actions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <imgui.h>
#include <ranges>
#include <spdlog/spdlog.h>

namespace lc1 {
GameSession::GameSession(Device const &device, std::filesystem::path const &asset_root,
                         Save const &save, OutputSettings output, Window *window)
    : window_(window), input_router_(default_game_bindings(), GameInputContext::Gameplay),
      world_(save), world_view_(device, asset_root, world_, output),
      player_view_(world_.continent(), world_.player().position()),
      music_(world_view_.audio_context()), save_directory_(save.directory())
{
    if (!window_)
        fail("game session requires a window");

    window_->set_cursor_captured(should_captures_cursor());
    update_title();

    spdlog::info("[save] {}: {} factions, {} parties, {} characters, {} equipment, {} weapons",
                 save.directory().string(), save.factions().size(), save.parties().size(),
                 save.characters().size(), save.equipments().size(), save.weapons().size());
    spdlog::info("[world] {} regions, {} locations, {} roads; fixed oblique view",
                 world_.continent().regions().size(), world_.continent().locations().size(),
                 world_.continent().roads().size());
}

void GameSession::update(InputState const &input_state, float delta_seconds)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("invalid session update time");

    input_router_.update(input_state);

    float const elapsed = std::min(delta_seconds, 0.1F);
    auto const &ui = ImGui::GetIO();
    if (window_->focused() && !ui.WantCaptureKeyboard &&
        input_router_.pressed(GameAction::QuitGame))
        quit_requested_ = true;

    bool const controls_enabled = ui_.gameplay_enabled() && window_->focused() &&
                                  input_router_.active_context() == GameInputContext::Gameplay;
    bool const keyboard_enabled = controls_enabled && !ui.WantCaptureKeyboard;
    bool const mouse_enabled = controls_enabled && !ui.WantCaptureMouse;

    player_view_.update_view_mode(input_router_, keyboard_enabled, world_.player());
    bool const keyboard_save_requested =
        keyboard_enabled && input_router_.pressed(GameAction::SaveGame);

    auto const response_index = [&]() -> int {
        if (!keyboard_enabled)
            return -1;
        constexpr std::array actions{GameAction::DialogResponse0, GameAction::DialogResponse1,
                                     GameAction::DialogResponse2, GameAction::DialogResponse3};
        for (auto const [index, action] : std::views::zip(std::views::iota(0), actions)) {
            if (input_router_.pressed(action))
                return index;
        }
        return -1;
    };
    // The controller supplies intent; GameWorld applies it through Character and then
    // handles the world-level dialog interaction. Whether an idle player accepts ambient
    // requests is a Character rule, not an input rule.
    world_.update(elapsed, WorldInteraction{
                               .player_intent = player_view_.resolve_input(
                                   world_.player(), input_router_, input_state.cursor_delta(),
                                   keyboard_enabled, mouse_enabled),
                               .player_input_enabled = keyboard_enabled,
                               .response_index = response_index(),
                           });
    if (mouse_enabled)
        player_view_.update_pointer(input_state, window_->content_size(),
                                    window_->hovered() && !window_->cursor_captured(), elapsed);
    player_view_.update_camera(world_.player(), elapsed);
    world_view_.update(
        {
            .cycle_lighting = keyboard_enabled && input_router_.pressed(GameAction::CycleLighting),
            .toggle_showcase_motion =
                keyboard_enabled && input_router_.pressed(GameAction::ToggleShowcaseMotion),
            .exposure_steps =
                keyboard_enabled
                    ? static_cast<int>(input_router_.pressed(GameAction::IncreaseExposure)) -
                          static_cast<int>(input_router_.pressed(GameAction::DecreaseExposure))
                    : 0,
        },
        player_view_.camera(), elapsed);

    auto const *target = world_.interaction_target();
    auto const ui_actions = ui_.update(
        {
            .extent = window_->framebuffer_extent(),
            .lighting_name = world_view_.lighting_name(),
            .exposure = world_view_.output().exposure,
            .showcase_animated = world_view_.showcase_animated(),
            .interaction_target =
                target ? std::string_view{target->config().name} : std::string_view{},
        },
        music_);
    if (ui_actions.start)
        ui_.start_game();
    quit_requested_ |= ui_actions.quit;
    if (ui_actions.volume)
        world_view_.set_volume(*ui_actions.volume);

    window_->set_cursor_captured(should_captures_cursor());

    if (keyboard_save_requested || ui_actions.save)
        save();

    update_title();
}

void GameSession::update_title()
{
    auto const location = world_view_.location();
    auto const title =
        std::format("lc1 | {} | {} | Lighting: {} | Exposure: {:.2f}", location.region,
                    location.place, world_view_.lighting_name(), world_view_.output().exposure);
    if (window_->set_title(title))
        spdlog::info("[world] {}", title);
}

void GameSession::save()
{
    try {
        Save::write(save_directory_, world_.snapshot());
        ui_.set_save_status("Game saved. F6: Save again");
    }
    catch (Error const &error) {
        spdlog::error("[save] {}", error.diagnostic());
        ui_.set_save_status("Save failed. Please try again.");
    }
    catch (std::exception const &error) {
        spdlog::error("[save] {}", error.what());
        ui_.set_save_status("Save failed. Please try again.");
    }
}

} // namespace lc1
