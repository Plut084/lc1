#include "lc1/game/session/save.hpp"

#include "lc1/error.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
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

// CTest runs from tests/fixtures, independently of the editable runtime saves.
std::filesystem::path seed_directory()
{
    auto const directory = std::filesystem::current_path() / "saves/01";
    if (!std::filesystem::exists(directory / "session.yaml"))
        throw std::runtime_error{"run this test from tests/fixtures: " + directory.string()};
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
    scratch.write("session.yaml", "player: 100024\nexample: 0\n");
    lc1::Save const save{scratch.directory()};
    check(save.player() == 100024, "the player keyid comes from the session file");
    check(!save.has_example(), "example 0 means the save has no AI demonstration");
}

// The session decides who the player is; no keyid has to be special for this to work.
void a_save_without_an_example_loads()
{
    ScratchSave scratch{"no-example"};
    scratch.write("session.yaml", "player: 100024\nexample: 0\n");
    lc1::Save const save{scratch.directory()};
    check(save.player() == 100024 && !save.has_example(), "an omitted example is not required");
}

void invalid_sessions_are_rejected()
{
    // The expected substrings are the diagnostics save.cpp prints, or the parser's message
    // where the record itself is malformed: a bare "did it throw" check would also pass if
    // the rule under test never ran.
    check_rejected("missing-player", "session.yaml", "example: 2\n",
                   "Field named 'player' not found");
    check_rejected("zero-player", "session.yaml", "player: 0\nexample: 2\n",
                   "session.yaml must name the player");
    check_rejected("unknown-player", "session.yaml", "player: 4711\nexample: 2\n",
                   "session player refers to unknown character 4711");
    check_rejected("unknown-example", "session.yaml", "player: 1\nexample: 4711\n",
                   "session example refers to unknown character 4711");
    check_rejected("example-is-player", "session.yaml", "player: 1\nexample: 1\n",
                   "session example 1 is also the player");
    check_rejected("sequence-session", "session.yaml",
                   "- player: 1\n  example: 2\n- player: 2\n  example: 0\n",
                   "cannot read save file");
    check_rejected("missing-session-file", "session.yaml", "", "cannot open save file");
}

// The session file is only one reference holder: the world files must keep their own
// guarantees, and all of them fail before any runtime object exists.
void dangling_world_references_are_rejected()
{
    check_rejected("party-unknown-member", "parties.yaml",
                   "- keyid: 1\n  faction: 1\n  leader: 1\n  members: [2, 4711]\n"
                   "  captives: []\n"
                   "- keyid: 3\n  faction: 1\n  leader: 100024\n"
                   "  members: [100025, 100026, 100027, 100028]\n  captives: []\n",
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

lc1::SerializedWorld checkpoint_data()
{
    auto data = lc1::Save{seed_directory()}.data();
    for (auto const &character : data.characters)
        data.states.push_back({.keyid = character.keyid,
                               .x = 1.0F,
                               .y = 0.0F,
                               .z = 2.0F,
                               .body_yaw = 35.0F,
                               .look_yaw = 10.0F,
                               .look_pitch = -20.0F,
                               .patrol = character.keyid == data.session.example});
    return data;
}

template <typename Function> void rejects_write(Function &&function, std::string_view expected)
{
    std::string message;
    try {
        function();
    }
    catch (lc1::Error const &error) {
        message = error.what();
    }
    check(message.contains(expected), std::format("expected '{}', got '{}'", expected, message));
}

void checkpoint_round_trip()
{
    ScratchSave scratch{"checkpoint"};
    auto data = checkpoint_data();
    lc1::Save::write(scratch.directory(), data);
    auto const initial = lc1::Save{scratch.directory()};
    check(initial.states().at(1).x == 1.0F && initial.states().at(1).body_yaw == 35.0F &&
              initial.states().at(1).look_pitch == -20.0F && initial.states().at(2).patrol,
          "checkpoint restores poses and patrol state");
    check(initial.characters().at(100024).equipment ==
              lc1::Save{seed_directory()}.characters().at(100024).equipment,
          "equipment enum maps survive serialization");

    // Move the player and transfer an ordinary member between two parties.
    auto &player_state = *std::ranges::find(data.states, 1, &lc1::SerializedCharacterState::keyid);
    player_state.x = 12.5F;
    auto &example = *std::ranges::find(data.states, 2, &lc1::SerializedCharacterState::keyid);
    example.patrol = false;
    auto &recruit =
        *std::ranges::find(data.characters, 100025, &lc1::SerializedCharacterConfig::keyid);
    recruit.party = 1;
    recruit.name = "Recruit \u4f19\u4f34";
    auto &destination = *std::ranges::find(data.parties, 1, &lc1::SerializedParty::keyid);
    auto &source = *std::ranges::find(data.parties, 3, &lc1::SerializedParty::keyid);
    destination.members.push_back(100025);
    std::erase(source.members, 100025);
    lc1::Save::write(scratch.directory(), data);
    lc1::Save const saved{scratch.directory()};
    check(saved.characters().at(100025).party == 1 &&
              saved.characters().at(100025).name == recruit.name &&
              std::ranges::contains(saved.parties().at(1).members, 100025U) &&
              !std::ranges::contains(saved.parties().at(3).members, 100025U),
          "replacement checkpoint restores both sides of recruitment and UTF-8 names");
    check(saved.states().at(1).x == 12.5F && !saved.states().at(2).patrol,
          "replacement checkpoint keeps movement and the stopped patrol");

    auto &recruit_state =
        *std::ranges::find(data.states, 100025, &lc1::SerializedCharacterState::keyid);
    recruit_state.companion_order = lc1::CompanionOrder::Follow;
    lc1::Save::write(scratch.directory(), data);
    check(lc1::Save{scratch.directory()}.states().at(100025).companion_order ==
              lc1::CompanionOrder::Follow,
          "follow orders survive a checkpoint round trip");
    recruit_state.companion_order = lc1::CompanionOrder::Wait;
    lc1::Save::write(scratch.directory(), data);
    check(lc1::Save{scratch.directory()}.states().at(100025).companion_order ==
              lc1::CompanionOrder::Wait,
          "wait orders survive a replacement checkpoint");

    // Seed data cannot override committed progress, and abandoned staging is ignored.
    scratch.write("characters.yaml", "invalid seed");
    scratch.write("checkpoint.yaml.tmp", "interrupted write");
    check(lc1::Save{scratch.directory()}.states().at(1).x == 12.5F,
          "committed checkpoint wins over seed and unfinished staging");

    auto invalid = data;
    invalid.version = 3;
    rejects_write([&] { lc1::Save::write(scratch.directory(), invalid); },
                  "unsupported checkpoint version");
    invalid = data;
    invalid.parties.front().members.clear();
    rejects_write([&] { lc1::Save::write(scratch.directory(), invalid); },
                  "is not listed in its party");
    invalid = data;
    invalid.states.front().companion_order = lc1::CompanionOrder::Follow;
    rejects_write([&] { lc1::Save::write(scratch.directory(), invalid); },
                  "ineligible companion order");
    invalid = data;
    invalid.states.back().companion_order = static_cast<lc1::CompanionOrder>(999);
    rejects_write([&] { lc1::Save::write(scratch.directory(), invalid); },
                  "invalid companion order");
    invalid = data;
    auto &patrol = *std::ranges::find(invalid.states, invalid.session.example,
                                      &lc1::SerializedCharacterState::keyid);
    patrol.patrol = true;
    patrol.companion_order = lc1::CompanionOrder::Follow;
    rejects_write([&] { lc1::Save::write(scratch.directory(), invalid); },
                  "ineligible companion order");
    invalid = data;
    invalid.states.pop_back();
    rejects_write([&] { lc1::Save::write(scratch.directory(), invalid); },
                  "one state per character");
    invalid = data;
    invalid.states.front().x = std::numeric_limits<float>::quiet_NaN();
    rejects_write([&] { lc1::Save::write(scratch.directory(), invalid); }, "invalid saved pose");
    invalid = data;
    invalid.states.front().keyid = 999;
    rejects_write([&] { lc1::Save::write(scratch.directory(), invalid); }, "unknown character 999");
    check(lc1::Save{scratch.directory()}.states().at(1).x == 12.5F,
          "validation failure leaves the previous checkpoint readable");

    std::filesystem::remove(scratch.directory() / "checkpoint.yaml.tmp");
    std::filesystem::create_directory(scratch.directory() / "checkpoint.yaml.tmp");
    rejects_write([&] { lc1::Save::write(scratch.directory(), data); }, "cannot open checkpoint");
    check(lc1::Save{scratch.directory()}.states().at(1).x == 12.5F,
          "staging open failure preserves the committed checkpoint");

    // A corrupt committed file is diagnosed, never silently replaced with a fresh world.
    scratch.write("checkpoint.yaml", "broken: [");
    check_rejected_in(scratch, "cannot read save file");
}

void legacy_checkpoints_are_upgraded()
{
    ScratchSave scratch{"legacy"};
    auto const data = checkpoint_data();
    lc1::Save::write(scratch.directory(), data);
    auto stream = std::ifstream{scratch.directory() / "checkpoint.yaml"};
    std::string legacy;
    for (std::string line; std::getline(stream, line);) {
        if (!line.contains("companion_order:"))
            legacy += line + "\n";
    }
    stream.close();
    auto const version = legacy.find("version: 2");
    check(version != std::string::npos, "checkpoint writer emits version 2");
    legacy.replace(version, std::string_view{"version: 2"}.size(), "version: 1");
    scratch.write("checkpoint.yaml", legacy);
    auto const upgraded = lc1::Save{scratch.directory()}.data();
    check(upgraded.version == 2 &&
              upgraded.states.front().companion_order == lc1::CompanionOrder::None &&
              upgraded.states.at(1).patrol,
          "version 1 checkpoints preserve patrol and gain no implicit follow orders");
    lc1::Save::write(scratch.directory(), upgraded);
    check(lc1::Save{scratch.directory()}.data().version == 2,
          "upgraded saves can be written and reloaded");
    // Version 2 must supply the new field; migration never relaxes its required schema.
    legacy.replace(version, std::string_view{"version: 1"}.size(), "version: 2");
    scratch.write("checkpoint.yaml", legacy);
    check_rejected_in(scratch, "companion_order");
}

void inconsistent_world_records_are_rejected()
{
    ScratchSave zero{"zero-keyid"};
    zero.patch("factions.yaml", "keyid: 1", "keyid: 0");
    check_rejected_in(zero, "faction keyid must be nonzero");
    ScratchSave duplicate{"duplicate-member"};
    duplicate.patch("parties.yaml", "members: [2]", "members: [2, 2]");
    check_rejected_in(duplicate, "duplicate member 2");
    ScratchSave overlap{"member-captive"};
    overlap.patch("parties.yaml", "captives: []", "captives: [2]");
    check_rejected_in(overlap, "both a companion and captive");
    ScratchSave ownership{"wrong-owner"};
    ownership.patch("parties.yaml", "members: [2]", "members: [2, 100025]");
    check_rejected_in(ownership, "belonging to another party");
    ScratchSave leader_member{"leader-in-members"};
    leader_member.patch("parties.yaml", "members: [2]", "members: [1, 2]");
    check_rejected_in(leader_member, "leader 1 must not appear in members");
    ScratchSave unlisted_member{"unlisted-member"};
    unlisted_member.patch("parties.yaml", "members: [2]", "members: []");
    check_rejected_in(unlisted_member, "character 2 is not listed in its party 1");
    ScratchSave unlisted_leader{"unlisted-leader"};
    unlisted_leader.patch("parties.yaml", "leader: 1", "leader: 0");
    check_rejected_in(unlisted_leader, "character 1 is not listed in its party 1");
    // Captives and leaderless parties remain valid when every character is listed.
    ScratchSave captive{"listed-captive"};
    captive.patch("parties.yaml", "members: [2]", "members: []");
    captive.patch("parties.yaml", "captives: []", "captives: [2]");
    check(lc1::Save{captive.directory()}.parties().at(1).captives.size() == 1,
          "a captive counts as a listed party character");
    ScratchSave leaderless{"leaderless"};
    leaderless.patch("parties.yaml", "leader: 1", "leader: 0");
    leaderless.patch("parties.yaml", "members: [2]", "members: [1, 2]");
    check(lc1::Save{leaderless.directory()}.parties().at(1).leader == 0,
          "a fully listed party may have no leader");
    ScratchSave equipment{"equipment-slot"};
    equipment.patch("equipments.yaml", "type: Head", "type: Body");
    check_rejected_in(equipment, "does not match its slot");
    ScratchSave weapon{"weapon-slot"};
    weapon.patch("weapons.yaml", "type: MeleeWeapon", "type: RangedWeapon");
    check_rejected_in(weapon, "does not match its slot");
}

} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc == 2) {
            lc1::Save const save{argv[1]};
            check(save.characters().contains(save.player()),
                  "the runtime player must be a character record");
            std::cout << "runtime save checks passed\n";
            return EXIT_SUCCESS;
        }
        seed_names_the_player();
        an_existing_character_can_be_the_player();
        a_save_without_an_example_loads();
        invalid_sessions_are_rejected();
        dangling_world_references_are_rejected();
        inconsistent_world_records_are_rejected();
        checkpoint_round_trip();
        legacy_checkpoints_are_upgraded();
        std::cout << "save data checks passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
