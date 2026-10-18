#pragma once

#include <string>
#include <vector>

namespace lc1 {

class Character;

class Dialog {
  public:
    void said(Character *speaker, std::string text, std::vector<std::string> candidate_responses)
    {
        current_speaker_ = speaker;
        current_speech_text_ = std::move(text);
        candidate_responses_ = std::move(candidate_responses);
    }

    Character *current_speaker() const { return current_speaker_; }

    std::string_view current_speech_text() const { return current_speech_text_; }

    std::vector<std::string> const &candidate_responses() const { return candidate_responses_; }

    void clear() { current_speaker_ = nullptr; }

  private:
    // 当前说话的角色和内容
    Character *current_speaker_{};
    std::string current_speech_text_;
    std::vector<std::string> candidate_responses_;
};

} // namespace lc1
