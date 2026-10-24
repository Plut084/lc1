#include "lc1/game/ui/game-ui.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/ui/music-browser.hpp"
#include "lc1/text/text-layout.hpp"
#include <format>
#include <imgui.h>
#include <ranges>

namespace lc1 {
GameUiActions GameUi::update(GameUiInfo const &info, MusicBrowser &music)
{
    GameUiActions actions;
    ImGui::Begin("lc1 debug info");
    auto [w, h] = info.extent;
    ImGui::TextUnformatted(std::format("Window size: {}x{}", w, h).c_str());
    ImGui::TextUnformatted(std::format("FPS: {}", ImGui::GetIO().Framerate).c_str());
    ImGui::TextUnformatted(std::format("Lighting: {}", info.lighting_name).c_str());
    ImGui::Text("Exposure: %.2f", info.exposure);
    if (ImGui::Button("Save game (F6)"))
        actions.save = true;
    ImGui::TextUnformatted(save_status_.c_str());
    ImGui::TextUnformatted(
        std::format("Showcase motion: {}", info.showcase_animated ? "on" : "off").c_str());
    ImGui::TextUnformatted(std::format("interaction_target: {}", info.interaction_target.empty()
                                                                     ? "not found"
                                                                     : info.interaction_target)
                               .c_str());

    if (state_ == State::InGame && ImGui::Button("Main menu"))
        state_ = State::MainMenu;

    music.controls();

    ImGui::End();
    music.update();

    switch (state_) {
    case State::MainMenu:
        ImGui::Begin("lc1 main menu##menu");
        if (ImGui::Button("Start Game"))
            actions.start = true; // TODO
        if (ImGui::Button("Settings"))
            state_ = State::InSettings;
        if (ImGui::Button("Quit"))
            actions.quit = true;
        ImGui::End();
        break;
    case State::InSettings:
        ImGui::Begin("lc1 settings##menu");
        if (ImGui::SliderFloat("Volume", &volume_, 0, 100))
            actions.volume = volume_;
        if (ImGui::Button("Back to Main Menu"))
            state_ = State::MainMenu;
        ImGui::End();
        break;
    case State::InGame:
        break;
    }
    return actions;
}

std::vector<ScreenTextItem> GameUi::screen_labels(GameWorld const &world, GpuFont const &font,
                                                  FrameExtent extent) const
{

    std::vector<ScreenTextItem> labels;

    labels.push_back(ScreenTextItem{
        .text =
            std::format("Party: {} companions", world.player().config().party->members().size()),
        .position = {24.0F, 78.0F},
        .line_height = 22.0F,
    });
    labels.push_back(
        ScreenTextItem{.text = save_status_, .position = {24.0F, 108.0F}, .line_height = 22.0F});

    auto const *dialog = world.dialogs().find(world.player());
    if (world.interaction_target() && !dialog) {
        ScreenTextItem interact_text{
            .text = std::format("Press F to interact with {}",
                                world.interaction_target()->config().name),
            .line_height = font.atlas().line_height_for_em(32),
        };
        auto const metrics =
            text::measure_text(font.atlas(), interact_text.text, interact_text.line_height);
        interact_text.position = {(extent.width - metrics.width) * 0.5F,
                                  (extent.height - metrics.height) * 0.5F};
        labels.push_back(interact_text);
    }

    if (dialog) {
        ScreenTextItem item{
            .text = std::format("{}: {}", dialog->speaker()->config().name,
                                dialog->current_speech_text()),
            .line_height = font.atlas().line_height_for_em(32),
        };
        auto const metrics = text::measure_text(font.atlas(), item.text, item.line_height);
        item.position = {(extent.width - metrics.width) * 0.5F, extent.height * 0.7};
        labels.push_back(item);

        labels.push_back(ScreenTextItem{
            .text = "F: End conversation", .position = {24.0F, 138.0F}, .line_height = 22.0F});
        if (dialog->respondent() == &world.player())
            for (auto const &[index, response] :
                 std::views::zip(std::views::iota(0), dialog->candidate_responses())) {
                ScreenTextItem response_item{
                    .text = std::format("{}. {}", index + 1, response),
                    .line_height = font.atlas().line_height_for_em(24),
                };
                auto const response_metrics =
                    font.atlas().measure(response_item.text, response_item.line_height);
                response_item.position = {(extent.width - response_metrics.width) * 0.5F,
                                          extent.height * 0.7 + metrics.height +
                                              index * response_metrics.height};
                labels.push_back(response_item);
            }
    }

    return labels;
}

} // namespace lc1
