#include "lc1/game/ui/game-ui.hpp"

#include "lc1/game/party/party.hpp"
#include "lc1/game/ui/music-browser.hpp"
#include "lc1/game/world/world-view.hpp"
#include "lc1/text/text-layout.hpp"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <ranges>

namespace lc1 {

void GameUi::open_save(std::string_view name)
{
    save_name_.fill('\0');
    std::ranges::copy(name.substr(0, save_name_.size() - 1), save_name_.begin());
    save_open_ = true;
    load_open_ = false;
}

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
        open_save(info.current_save);
    if (ImGui::Button("Load game")) {
        load_open_ = true;
        save_open_ = false;
        actions.refresh_saves = true;
    }
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
        if (ImGui::Button("Load Game")) {
            load_open_ = true;
            save_open_ = false;
            actions.refresh_saves = true;
        }
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
    if (save_open_) {
        ImGui::Begin("Save game##save", &save_open_, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::InputText("Save name", save_name_.data(), save_name_.size());
        auto const name = std::string_view{save_name_.data()};
        bool const overwrite = std::ranges::find(info.saves, name) != info.saves.end();
        if (overwrite)
            ImGui::TextUnformatted("This name already exists. Saving replaces its checkpoint.");
        ImGui::BeginDisabled(name.empty());
        if (ImGui::Button(overwrite ? "Overwrite save" : "Save"))
            actions.save = std::string{name};
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            save_open_ = false;
        ImGui::TextUnformatted(save_status_.c_str());
        ImGui::End();
    }
    if (load_open_) {
        ImGui::Begin("Load game##load", &load_open_, ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::Button("Refresh"))
            actions.refresh_saves = true;
        if (info.saves.empty())
            ImGui::TextUnformatted("No saves found.");
        for (auto const &name : info.saves) {
            ImGui::PushID(name.c_str());
            ImGui::TextUnformatted(name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Load"))
                actions.load = name;
            ImGui::PopID();
        }
        ImGui::TextUnformatted("Loading replaces the current unsaved game.");
        ImGui::TextUnformatted(save_status_.c_str());
        if (ImGui::Button("Cancel"))
            load_open_ = false;
        ImGui::End();
    }
    return actions;
}

std::vector<ScreenTextItem> GameUi::screen_labels(GameWorld const &world, GpuFont const &font,
                                                  FrameExtent extent, GameLocation location) const
{
    auto const [w, h] = extent;

    std::vector<ScreenTextItem> labels;

    labels.push_back(ScreenTextItem{
        .text = std::format("{} | {} / {}", world.player().config().name, location.region,
                            location.place),
        .position = {24.0F, 46.0F},
        .line_height = 22.0F,
    });

    labels.push_back(ScreenTextItem{
        .text =
            std::format("Party: {} companions", world.player().config().party->members().size()),
        .position = {24.0F, 78.0F},
        .line_height = 22.0F,
    });
    labels.push_back(
        ScreenTextItem{.text = save_status_, .position = {24.0F, 108.0F}, .line_height = 22.0F});

    std::string bag_str;
    for (auto const type : {game::ResourceType::Moneta, game::ResourceType::Wood,
                            game::ResourceType::PigIron, game::ResourceType::IronHelmet})
        bag_str += std::format("{} {}\n", type, world.player().bag().quantity_of(type));
    auto bag_metric = font.atlas().measure(bag_str, 32);
    labels.push_back(ScreenTextItem{
        .text = bag_str, .position = {w - bag_metric.width - 10, 10}, .line_height = 32});

    auto const *dialog = world.dialogs().find(world.player());
    if (world.interaction_target() && !dialog) {
        ScreenTextItem interact_text{
            .text = std::format("Press F to interact with {}",
                                world.interaction_target()->config().name),
            .line_height = font.atlas().line_height_for_em(32),
        };
        auto const metrics =
            text::measure_text(font.atlas(), interact_text.text, interact_text.line_height);
        interact_text.position = {(w - metrics.width) * 0.5F, (h - metrics.height) * 0.5F};
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
