#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace lc1 {

// One concrete political faction. Design separates faction categories (empire, tribe,
// independent, secret) from concrete factions; only concrete factions exist here, and
// the category is data, not a separate type.
class Faction {
  public:
    Faction(std::uint32_t keyid, std::string name) : keyid_(keyid), name_(std::move(name)) {}

    std::uint32_t keyid() const { return keyid_; }

    std::string const &name() const { return name_; }

  private:
    std::uint32_t keyid_;
    std::string name_;
};

} // namespace lc1
