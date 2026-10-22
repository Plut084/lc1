#pragma once

#include "lc1/game/game-world.hpp"
#include "lc1/vk/render/text-pass.hpp"
#include "lc1/vk/resources/gpu-font.hpp"
#include "lc1/window.hpp"
#include <optional>

namespace lc1 {
class MusicBrowser;

struct GameUiInfo {
    FrameExtent extent;
    std::string_view lighting_name;
    float exposure;
    bool showcase_animated;
    std::string_view interaction_target;
};

struct GameUiActions {
    bool save = false;
    bool start = false;
    bool quit = false;
    std::optional<float> volume;
};

class GameUi {
  public:
    GameUiActions update(GameUiInfo const &info, MusicBrowser &music);

    bool gameplay_enabled() const { return state_ == State::InGame; }

    void start_game() { state_ = State::InGame; }

    void set_save_status(std::string status) { save_status_ = std::move(status); }

    std::vector<ScreenTextItem> screen_labels(GameWorld const &world, GpuFont const &font,
                                              FrameExtent extent) const;

  private:
    enum class State { MainMenu, InSettings, InGame };
    State state_ = State::MainMenu;
    float volume_ = 50.0F;
    std::string save_status_ = "F6: Save game";
};

} // namespace lc1
