#pragma once

#include "lc1/vk/render/draw-item.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace lc1 {

class Character;
class Faction;

// A party is a container and a map entity, not a pile of stats: it names concrete
// characters and owns none of them. The leader is optional because a save is loaded in
// two phases -- parties first, membership once every character exists.
class Party {
  public:
    Party(std::uint32_t keyid, Faction *faction, Character *leader = nullptr,
          std::vector<Character *> members = {}, std::vector<Character *> captives = {})
        : keyid_(keyid), faction_(faction), leader_(leader), members_(std::move(members)),
          captives_(std::move(captives))
    {
    }

    std::vector<DrawItem> draw_items() const;

    std::uint32_t keyid() const { return keyid_; }

    Faction *faction() { return faction_; }

    Faction const *faction() const { return faction_; }

    Character *leader() const { return leader_; }

    std::vector<Character *> const &members() const { return members_; }

    std::vector<Character *> const &captives() const { return captives_; }

    // Load-time wiring. A party record lists character keyids; the characters
    // themselves only exist after every party has been created.
    void set_leader(Character *leader) { leader_ = leader; }

    void add_member(Character *member) { members_.push_back(member); }

    void add_captive(Character *captive) { captives_.push_back(captive); }

  private:
    std::uint32_t keyid_;
    Faction *faction_ = nullptr;
    Character *leader_ = nullptr;
    std::vector<Character *> members_;
    std::vector<Character *> captives_;
    // Follow strategy, formation.
};

} // namespace lc1
