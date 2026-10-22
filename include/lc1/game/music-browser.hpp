#pragma once

#include "lc1/audio/audio.hpp"
#include <string>
#include <vector>

namespace lc1 {

// In-session music tool. The borrowed audio context must outlive this immovable browser.
class MusicBrowser {
  public:
    explicit MusicBrowser(audio::AudioContext const &context);
    MusicBrowser(MusicBrowser const &) = delete;
    MusicBrowser &operator=(MusicBrowser const &) = delete;
    MusicBrowser(MusicBrowser &&) = delete;
    MusicBrowser &operator=(MusicBrowser &&) = delete;
    void controls();
    void update();

  private:
    void refresh_music_browser();
    void play_music(std::filesystem::path const &path);

    struct MusicBrowserEntry {
        std::filesystem::path path;
        bool is_directory{};
    };

    audio::AudioContext const *audio_context_; // Required non-null borrow.
    audio::Source audio_source_;
    audio::Sound audio_music_;
    std::filesystem::path music_browser_path_;
    std::vector<MusicBrowserEntry> music_browser_entries_;
    std::string music_error_;
    std::string current_music_name_;
    std::filesystem::path current_music_path_;
    bool music_browser_open_ = false;
    bool music_browser_loaded_ = false;
};

} // namespace lc1
