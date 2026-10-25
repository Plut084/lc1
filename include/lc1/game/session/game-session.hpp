#pragma once

#include "lc1/game/actions.hpp"
#include "lc1/game/player/player-view.hpp"
#include "lc1/game/ui/game-ui.hpp"
#include "lc1/game/ui/music-browser.hpp"
#include "lc1/game/world/game-world.hpp"
#include "lc1/game/world/world-view.hpp"

namespace lc1 {

// Coordinates a local playable session, not a server GameMode or a network replica.
// Application owns window/input/backend lifetime. Device and Window outlive this session;
// Application waits for submitted GPU work before destroying it.
class GameSession {
  public:
    GameSession(Device const &device, std::filesystem::path const &asset_root, Save const &save,
                OutputSettings output, Window *window);
    GameSession(GameSession const &) = delete;
    GameSession &operator=(GameSession const &) = delete;
    GameSession(GameSession &&) = delete;
    GameSession &operator=(GameSession &&) = delete;
    ~GameSession() = default;

    // Resolves actions once, then updates view mode, simulation and presentation.
    // Application applies captures_cursor() after this returns.
    void update(InputState const &input_state, float delta_seconds);

    void set_aspect_ratio(float aspect_ratio) { player_view_.set_aspect_ratio(aspect_ratio); }

    bool take_save_request();

    bool quit_requested() const { return quit_requested_; }

    // Read-only borrows; valid for this session's lifetime. Mutations use session operations.
    GameWorld const &world() const { return world_; }

    WorldView const &world_view() const { return view_; }

    PlayerView const &player_view() const { return player_view_; }

    GameUi const &ui() const { return ui_; }

  private:
    bool should_captures_cursor() const
    {
        return ui_.gameplay_enabled() && !player_view_.oblique();
    }

    Window *window_; // Required non-null borrow; Application's Window outlives this.
    GameInputRouter input_router_;
    // Owners precede borrowers: view/player view borrow world, music borrows view's audio context.
    GameWorld world_;
    WorldView view_;
    PlayerView player_view_;
    MusicBrowser music_;
    GameUi ui_;

    std::filesystem::path save_directory_;
    bool save_requested_ = false;

    bool quit_requested_ = false;
};

} // namespace lc1
