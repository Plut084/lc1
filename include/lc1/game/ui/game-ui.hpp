#pragma once

#include "lc1/game/world/game-world.hpp"
#include "lc1/vk/render/text-pass.hpp"
#include "lc1/vk/resources/gpu-font.hpp"
#include "lc1/window.hpp"
#include <array>
#include <optional>
#include <span>

namespace lc1 {
class MusicBrowser;
struct GameLocation;

struct GameUiInfo {
    std::span<std::string const> saves;
    std::string_view current_save;
    FrameExtent extent;
    std::string_view lighting_name;
    float exposure;
    bool showcase_animated;
    std::string_view interaction_target;
};

struct GameUiActions {
    std::optional<std::string> save;
    std::optional<std::string> load;
    bool refresh_saves = false;
    bool start = false;
    bool quit = false;
    std::optional<float> volume;
};

class GameUi {
  public:
    GameUiActions update(GameUiInfo const &info, MusicBrowser &music);

    bool gameplay_enabled() const { return state_ == State::InGame && !save_open_ && !load_open_; }

    void open_save(std::string_view name);

    void open_load()
    {
        load_open_ = true;
        save_open_ = false;
    }

    void saved() { save_open_ = false; }

    void start_game() { state_ = State::InGame; }

    void set_save_status(std::string status) { save_status_ = std::move(status); }

    std::vector<ScreenTextItem> screen_labels(GameWorld const &world, GpuFont const &font,
                                              FrameExtent extent, GameLocation location) const;

  private:
    enum class State { MainMenu, InSettings, InGame };
    State state_ = State::MainMenu;
    float volume_ = 50.0F;
    std::string save_status_ = "F6: Save game | F9: Load game";
    std::array<char, 129> save_name_{};
    bool save_open_ = false;
    bool load_open_ = false;
};

} // namespace lc1
