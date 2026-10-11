#pragma once

#include "lc1/game/equipment.hpp"
#include "lc1/game/weapon.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lc1 {

// The records below are the file format, one per file, plus the session record that says
// which character the player controls. Every record holds keyids where the runtime object
// holds a borrowed pointer, so a file never depends on address or on load order. See
// docs/save-data.md.
struct SerializedFaction {
    std::uint32_t keyid;
    std::string name;
};

struct SerializedEquipment {
    std::uint32_t keyid;
    EquipmentType type;
    float armor;
};

struct SerializedWeapon {
    std::uint32_t keyid;
    WeaponType type;
    float attack;
    float range;
};

struct SerializedParty {
    std::uint32_t keyid;
    std::uint32_t faction;
    std::uint32_t leader; // 0 means the party has no leader yet.
    std::vector<std::uint32_t> members;
    std::vector<std::uint32_t> captives;
};

// Session state, not world data: a save names the character the player controls and,
// optionally, the character an AI demonstration drives. The rules of the world (parties,
// factions) stay in their own files.
struct SerializedSession {
    std::uint32_t player;
    std::uint32_t example; // 0 means this save has no AI demonstration.
};

struct SerializedCharacterConfig {
    std::uint32_t keyid;
    std::string name;
    bool sex;
    int age;
    std::uint32_t party;
    int level;
    // An enum cannot be a YAML mapping key, so reflect-cpp writes each map as a sequence
    // of [key, value] pairs: "- [Head, 10]".
    std::map<EquipmentType, std::uint32_t> equipment;
    std::map<WeaponType, std::uint32_t> weapon;
};

// The saved world as data: one directory of YAML files, decoded once and checked for
// duplicate ids and dangling references. The session file names the player, so no character
// keyid is special to the game code. Save owns no runtime object, so it can be loaded
// without a device, a continent or a window, and the world that GameSession builds from it
// borrows nothing back.
class Save {
  public:
    explicit Save(std::filesystem::path directory);

    std::filesystem::path const &directory() const { return directory_; }

    std::uint32_t player() const { return session_.player; }

    // 0 when the save has no AI demonstration; never a valid character keyid.
    std::uint32_t example() const { return session_.example; }

    bool has_example() const { return session_.example != 0; }

    auto const &factions() const { return factions_; }

    auto const &equipments() const { return equipments_; }

    auto const &weapons() const { return weapons_; }

    auto const &parties() const { return parties_; }

    auto const &characters() const { return characters_; }

  private:
    void read_factions();
    void read_equipments();
    void read_weapons();
    void read_parties();
    void read_characters();
    void read_session();
    // Every keyid a record names must exist, checked before any runtime object is built:
    // a dangling reference is a data error, not a half-built world.
    void validate_references() const;
    // Shared by the session record and the world records, so a dangling keyid always
    // reports the same way no matter which file named it.
    SerializedCharacterConfig const &character(std::uint32_t keyid, std::string_view what) const;

    std::filesystem::path directory_;
    SerializedSession session_;
    std::unordered_map<std::uint32_t, SerializedFaction> factions_;
    std::unordered_map<std::uint32_t, SerializedEquipment> equipments_;
    std::unordered_map<std::uint32_t, SerializedWeapon> weapons_;
    std::unordered_map<std::uint32_t, SerializedParty> parties_;
    std::unordered_map<std::uint32_t, SerializedCharacterConfig> characters_;
};

} // namespace lc1
