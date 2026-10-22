#pragma once

#include "lc1/game/game-ui.hpp"
#include "lc1/game/game-world.hpp"
#include "lc1/game/input.hpp"
#include "lc1/game/music-browser.hpp"
#include "lc1/game/player-view.hpp"
#include "lc1/game/world-view.hpp"

namespace lc1 {

// Coordinates a local playable session, not a server GameMode or a network replica.
// Application owns window/input/backend lifetime. Device and Window outlive this session;
// Application waits for submitted GPU work before destroying it.
class GameSession {
  public:
    GameSession(Device const &device, std::filesystem::path const &asset_root, Save const &save,
                OutputSettings output, Window const *window);
    GameSession(GameSession const &) = delete;
    GameSession &operator=(GameSession const &) = delete;
    GameSession(GameSession &&) = delete;
    GameSession &operator=(GameSession &&) = delete;
    ~GameSession() = default;

    void update(InputRouter const &input, float delta_seconds);
    // Call before cursor capture and update(): capture changes reset the mouse baseline.
    void update_view_mode(InputRouter const &input);

    void set_aspect_ratio(float aspect_ratio) { player_view_.set_aspect_ratio(aspect_ratio); }

    bool take_save_request();

    bool quit_requested() const { return quit_requested_; }

    bool captures_cursor() const { return ui_.gameplay_enabled() && !player_view_.oblique(); }

    void set_save_status(std::string status) { ui_.set_save_status(std::move(status)); }

    // Read-only borrows; valid for this session's lifetime. Mutations use session operations.
    GameWorld const &world() const { return world_; }

    WorldView const &world_view() const { return view_; }

    PlayerView const &player_view() const { return player_view_; }

    GameUi const &ui() const { return ui_; }

  private:
    Window const *window_; // Required non-null borrow; Application's Window outlives this.
    // Owners precede borrowers: view/player view borrow world, music borrows view's audio context.
    GameWorld world_;
    WorldView view_;
    PlayerView player_view_;
    MusicBrowser music_;
    GameUi ui_;
    bool save_requested_ = false;
    bool quit_requested_ = false;
};

} // namespace lc1
