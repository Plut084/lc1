#pragma once

#include <cstdint>

namespace lc1 {

enum class EquipmentType : std::uint8_t {
    Head,
    Body,
    Foot,
};

struct EquipmentProperty {
    float armor;
};

class Equipment {
  public:
    Equipment(std::uint32_t keyid, EquipmentType type, EquipmentProperty property)
        : keyid_(keyid), type_(type), property_(property)
    {
    }

    auto keyid() const { return keyid_; }

    EquipmentType type() const { return type_; }

    EquipmentProperty property() const { return property_; }

  private:
    std::uint32_t keyid_;
    EquipmentType type_;
    EquipmentProperty property_;
};

} // namespace lc1
