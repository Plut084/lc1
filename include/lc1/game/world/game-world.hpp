#pragma once

#include "lc1/game/character/character.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/dialog/dialog.hpp"
#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/session/save.hpp"

#include <memory>
#include <span>
#include <unordered_map>

namespace lc1 {

struct WorldInteraction {
    // What the player controller asked for this frame. GameWorld applies it through
    // Character, then uses interact/interaction_direction for the world-level action.
    CharacterIntent player_intent{};
    // False while UI, focus or ImGui capture owns the input. This gates player-initiated
    // conversations; whether the character is otherwise idle is a Character rule.
    bool player_input_enabled = false;
    int response_index = -1;
};

// Owns the local simulation. No window, renderer, audio or input-device dependencies.
// Not an authoritative server or a network replica. The roster is fixed after construction.
class GameWorld {
  public:
    GameWorld(Save const &save, Continent continent,
              std::filesystem::path const &dialog_catalog = "runtime/dialogs/catalog.yaml");
    GameWorld(GameWorld const &) = delete;
    GameWorld &operator=(GameWorld const &) = delete;
    GameWorld(GameWorld &&) = delete;
    GameWorld &operator=(GameWorld &&) = delete;

    ~GameWorld() { dialogs_.clear(); }

    Continent const &continent() const { return continent_; }

    Character &player() { return *player_; }

    Character const &player() const { return *player_; }

    // Stable, keyid-ordered borrows; all characters live for the lifetime of this world.
    std::span<Character const *const> characters() const { return visible_characters_; }

    DialogSystem const &dialogs() const { return dialogs_; }

    Character const *interaction_target() const { return interaction_target_; }

    // Applies player controls, then physics and interactions; update presentation afterward.
    // Both physics and conversations advance by the same capped simulation time.
    void update(float delta_seconds, WorldInteraction const &interaction);
    SerializedWorld snapshot() const;

  private:
    void build_world(Save const &save);
    void link_party_members(Save const &save);
    Character *character_by_id(std::uint32_t keyid, std::string_view what) const;
    Character *find_interaction_target(glm::vec3 forward) const;

    Continent const continent_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Faction>> factions_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Equipment>> equipments_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Weapon>> weapons_;
    // Interactions borrow dialogs_; clear active sessions before destroying characters.
    DialogSystem dialogs_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Character>> characters_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Party>> parties_;
    std::vector<Character *> dialog_participants_;
    std::vector<Character const *> visible_characters_;
    Character *player_ = nullptr;
    Character *example_ = nullptr;
    Character *interaction_target_ = nullptr;
};

} // namespace lc1
