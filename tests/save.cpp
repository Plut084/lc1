#include "lc1/game/save.hpp"

#include "lc1/error.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error{std::string{message}};
}

// The seed directory as the build tree sees it: ctest runs from the build tree, where
// copy_saves reproduced saves/01.
std::filesystem::path seed_directory()
{
    auto const directory = std::filesystem::current_path() / "saves/01";
    if (!std::filesystem::exists(directory / "session.yaml"))
        throw std::runtime_error{"run this test from the build tree: " + directory.string()};
    return directory;
}

// A scratch save directory holding the seed world files, so a test can replace any one of
// them and still load a complete save.
class ScratchSave {
  public:
    explicit ScratchSave(std::string_view name)
    {
        directory_ = std::filesystem::temp_directory_path() /
                     std::format("lc1-save-tests-{}-{:p}", name, static_cast<void const *>(this));
        std::filesystem::remove_all(directory_);
        std::filesystem::copy(seed_directory(), directory_);
    }

    ~ScratchSave()
    {
        // A destructor must not throw, so a cleanup failure is cosmetic.
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    // Rewrites or removes one save file. An empty content removes it, so a test can prove
    // that a save is only complete when every file is there.
    void write(std::string_view file, std::string_view content)
    {
        auto const path = directory_ / file;
        if (content.empty()) {
            std::filesystem::remove(path);
            return;
        }
        auto stream = std::ofstream{path};
        stream << content;
        if (!stream)
            throw std::runtime_error{"cannot write " + path.string()};
    }

    // Patches one spot in a copied seed file, so a fixture stays the seed world plus exactly
    // the defect under test. An unmatched needle means the seed changed and the test can no
    // longer claim to test what it says.
    void patch(std::string_view file, std::string_view needle, std::string_view replacement)
    {
        auto const path = directory_ / file;
        auto stream = std::ifstream{path};
        auto content = std::string{std::istreambuf_iterator<char>{stream}, {}};
        auto const at = content.find(needle);
        if (at == std::string::npos)
            throw std::runtime_error{
                std::format("{} does not contain \"{}\"", path.string(), needle)};
        content.replace(at, needle.size(), replacement);
        write(file, content);
    }

    std::filesystem::path const &directory() const { return directory_; }

  private:
    std::filesystem::path directory_;
};

// A save that the loader must reject with our own diagnostic. A rejection is not enough:
// a parse library complaining about the same file would pass a bare "did it throw" check
// while the rule under test never ran, so the expected message is part of the test.
// The rejection check on an already prepared save directory.
void check_rejected_in(ScratchSave &scratch, std::string_view expected)
{
    std::string message;
    try {
        lc1::Save const save{scratch.directory()};
    }
    catch (lc1::Error const &error) {
        message = error.what();
    }
    check(message.contains(expected), std::format("{} must report \"{}\", got \"{}\"",
                                                  scratch.directory().string(), expected, message));
}

// Writes one file into a fresh scratch save, then applies the same check.
void check_rejected(std::string_view name, std::string_view file, std::string_view content,
                    std::string_view expected)
{
    ScratchSave scratch{name};
    scratch.write(file, content);
    check_rejected_in(scratch, expected);
}

void seed_names_the_player()
{
    lc1::Save const save{seed_directory()};
    check(save.characters().contains(save.player()), "the seed player must be a character record");
    check(save.characters().at(save.player()).name == "player",
          "the seed save is played as the player record");
    check(save.has_example(), "the seed save has an AI demonstration");
    check(save.characters().contains(save.example()),
          "the seed example must be a character record");
    check(save.example() != save.player(), "the example must not be the player");
}

void an_existing_character_can_be_the_player()
{
    ScratchSave scratch{"any-character"};
    scratch.write("session.yaml", "- player: 100024\n  example: 0\n");
    lc1::Save const save{scratch.directory()};
    check(save.player() == 100024, "the player keyid comes from the session file");
    check(!save.has_example(), "example 0 means the save has no AI demonstration");
}

// The session decides who the player is; no keyid has to be special for this to work.
void a_save_without_an_example_loads()
{
    ScratchSave scratch{"no-example"};
    scratch.write("session.yaml", "- player: 100024\n  example: 0\n");
    lc1::Save const save{scratch.directory()};
    check(save.player() == 100024 && !save.has_example(), "an omitted example is not required");
}

void invalid_sessions_are_rejected()
{
    // The expected substrings are the diagnostics save.cpp prints, or the parser's message
    // where the record itself is malformed: a bare "did it throw" check would also pass if
    // the rule under test never ran.
    check_rejected("missing-player", "session.yaml", "- example: 2\n",
                   "Field named 'player' not found");
    check_rejected("zero-player", "session.yaml", "- player: 0\n  example: 2\n",
                   "session.yaml must name the player");
    check_rejected("unknown-player", "session.yaml", "- player: 4711\n  example: 2\n",
                   "session player refers to unknown character 4711");
    check_rejected("unknown-example", "session.yaml", "- player: 1\n  example: 4711\n",
                   "session example refers to unknown character 4711");
    check_rejected("example-is-player", "session.yaml", "- player: 1\n  example: 1\n",
                   "session example 1 is also the player");
    check_rejected("two-sessions", "session.yaml",
                   "- player: 1\n  example: 2\n- player: 2\n  example: 0\n", "exactly one record");
    check_rejected("missing-session-file", "session.yaml", "", "cannot open save file");
}

// The session file is only one reference holder: the world files must keep their own
// guarantees, and all of them fail before any runtime object exists.
void dangling_world_references_are_rejected()
{
    check_rejected("party-unknown-member", "parties.yaml",
                   "- keyid: 1\n  faction: 1\n  leader: 1\n  members: [1, 2, 4711]\n"
                   "  captives: []\n"
                   "- keyid: 3\n  faction: 1\n  leader: 100024\n"
                   "  members: [100024, 100025, 100026, 100027, 100028]\n  captives: []\n",
                   "party 1 member refers to unknown character 4711");

    // Each of these patched saves keeps every other reference resolvable, so only the rule
    // under test can reject it.
    ScratchSave unknown_party{"character-unknown-party"};
    unknown_party.patch("characters.yaml", "  party: 1\n", "  party: 4711\n");
    check_rejected_in(unknown_party, "character 1 refers to unknown party 4711");

    ScratchSave duplicate{"duplicate-character"};
    duplicate.patch("characters.yaml", "- keyid: 100028\n  name: Anna\n",
                    "- keyid: 1\n  name: impostor\n  sex: true\n  age: 30\n  party: 1\n"
                    "  level: 1\n  equipment: []\n  weapon: []\n"
                    "- keyid: 100028\n  name: Anna\n");
    check_rejected_in(duplicate, "duplicate character keyid 1");
}

} // namespace

int main()
{
    try {
        seed_names_the_player();
        an_existing_character_can_be_the_player();
        a_save_without_an_example_loads();
        invalid_sessions_are_rejected();
        dangling_world_references_are_rejected();
        std::cout << "save data checks passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
