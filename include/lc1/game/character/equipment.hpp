#pragma once

#include <cstdint>
#include <string_view>

namespace lc1 {

enum class EquipmentType : std::uint8_t {
    Head,
    Body,
    Foot,
};

inline std::string_view to_string(EquipmentType type)
{
    switch (type) {
    case EquipmentType::Head:
        return "Head";
    case EquipmentType::Body:
        return "Body";
    case EquipmentType::Foot:
        return "Foot";
    default:
        return "Unknown";
    }
}

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
