#pragma once

#include <cstdint>

namespace lc1 {

enum class WeaponType : std::uint8_t {
    MeleeWeapon,
    RangedWeapon,
};

struct WeaponProperty {
    float attack;
    float range;
};

class Weapon {
  public:
    Weapon(std::uint32_t keyid, WeaponType type, WeaponProperty property)
        : keyid_(keyid), type_(type), property_(property)
    {
    }

    auto keyid() const { return keyid_; }

    WeaponType type() const { return type_; }

    WeaponProperty property() const { return property_; }

  private:
    std::uint32_t keyid_;
    WeaponType type_;
    WeaponProperty property_;
};

} // namespace lc1
