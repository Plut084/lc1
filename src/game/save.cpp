#include "lc1/game/save.hpp"

#include "lc1/error.hpp"

#include <rfl/yaml.hpp>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

namespace lc1 {
namespace {

// A save file is a sequence of records. A missing or malformed file is fatal: a partial
// world would silently drop entities and leave references dangling.
template <typename Record>
std::vector<Record> read_save_file(std::filesystem::path const &directory, std::string_view name)
{
    auto const path = directory / name;
    auto stream = std::ifstream{path};
    if (!stream)
        fail("cannot open save file {}", path.string());
    auto records = rfl::yaml::read<std::vector<Record>, rfl::NoOptionals>(stream);
    if (!records)
        fail("cannot read save file {}: {}", path.string(), records.error().what());
    return *records;
}

// A duplicate keyid means two records claim the same identity; keeping either one
// silently would make every reference ambiguous.
template <typename Map> void insert_unique(Map &map, auto const &record, std::string_view kind)
{
    if (!map.try_emplace(record.keyid, record).second)
        fail("duplicate {} keyid {}", kind, record.keyid);
}

} // namespace

Save::Save(std::filesystem::path directory) : directory_{std::move(directory)}
{
    read_factions();
    read_equipments();
    read_weapons();
    read_parties();
    read_characters();
    read_session();
    validate_references();
}

void Save::read_factions()
{
    for (auto const &record : read_save_file<SerializedFaction>(directory_, "factions.yaml"))
        insert_unique(factions_, record, "faction");
}

void Save::read_equipments()
{
    for (auto const &record : read_save_file<SerializedEquipment>(directory_, "equipments.yaml"))
        insert_unique(equipments_, record, "equipment");
}

void Save::read_weapons()
{
    for (auto const &record : read_save_file<SerializedWeapon>(directory_, "weapons.yaml"))
        insert_unique(weapons_, record, "weapon");
}

void Save::read_parties()
{
    for (auto const &record : read_save_file<SerializedParty>(directory_, "parties.yaml"))
        insert_unique(parties_, record, "party");
}

void Save::read_characters()
{
    for (auto const &record :
         read_save_file<SerializedCharacterConfig>(directory_, "characters.yaml"))
        insert_unique(characters_, record, "character");
}

void Save::read_session()
{
    auto records = read_save_file<SerializedSession>(directory_, "session.yaml");
    if (records.size() != 1)
        fail("session.yaml must hold exactly one record, found {}", records.size());
    session_ = std::move(records.front());
    if (session_.player == 0)
        fail("session.yaml must name the player character");
}

void Save::validate_references() const
{
    // The session goes first: a save that cannot say who the player is has no business
    // building a world, and the zero sentinel would otherwise report character 0.
    character(session_.player, "session player");
    if (session_.example != 0) {
        if (session_.example == session_.player)
            fail("session example {} is also the player", session_.example);
        character(session_.example, "session example");
    }
    for (auto const &entry : parties_) {
        auto const &party = entry.second;
        if (!factions_.contains(party.faction))
            fail("party {} refers to unknown faction {}", party.keyid, party.faction);
        if (party.leader != 0)
            character(party.leader, std::format("party {} leader", party.keyid));
        for (auto const member : party.members)
            character(member, std::format("party {} member", party.keyid));
        for (auto const captive : party.captives)
            character(captive, std::format("party {} captive", party.keyid));
    }
    for (auto const &entry : characters_) {
        auto const &character_record = entry.second;
        if (!parties_.contains(character_record.party))
            fail("character {} refers to unknown party {}", character_record.keyid,
                 character_record.party);
        for (auto const &slot : character_record.equipment)
            if (!equipments_.contains(slot.second))
                fail("character {} refers to unknown equipment {}", character_record.keyid,
                     slot.second);
        for (auto const &slot : character_record.weapon)
            if (!weapons_.contains(slot.second))
                fail("character {} refers to unknown weapon {}", character_record.keyid,
                     slot.second);
    }
}

SerializedCharacterConfig const &Save::character(std::uint32_t keyid, std::string_view what) const
{
    auto const record = characters_.find(keyid);
    if (record == characters_.end())
        fail("{} refers to unknown character {}", what, keyid);
    return record->second;
}

} // namespace lc1
