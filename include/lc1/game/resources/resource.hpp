#pragma once

#include <cstdint>
#include <format>
#include <string_view>

namespace lc1::game {

enum class ResourceType : std::uint8_t {
    Moneta,
    Wood,
    PigIron,
    IronHelmet,
};

enum class ResourceCategory : std::uint8_t {
    Currency,
    Material,
    Equipment,
};

class Resource {
  public:
    Resource() = default;

    Resource(Resource const &) = default;
    Resource(Resource &&) = delete;
    Resource &operator=(Resource const &) = default;
    Resource &operator=(Resource &&) = delete;

    virtual ~Resource() = default;

    // Concrete type of a resource, e.g. Moneta, Wood, Iron.
    virtual ResourceType type() const = 0;

    // General category of a resource, e.g. Currency, Material.
    virtual ResourceCategory category() const = 0;
};

} // namespace lc1::game

template <> struct std::formatter<lc1::game::ResourceType> : std::formatter<std::string_view> {
    auto format(lc1::game::ResourceType value, std::format_context &ctx) const
    {
        std::string_view name;

        switch (value) {
        case lc1::game::ResourceType::Moneta:
            name = "Moneta";
            break;
        case lc1::game::ResourceType::Wood:
            name = "Wood";
            break;
        case lc1::game::ResourceType::IronHelmet:
            name = "Iron Helmet";
            break;
        case lc1::game::ResourceType::PigIron:
            name = "PigIron";
            break;
        }

        return std::formatter<std::string_view>::format(name, ctx);
    }
};

template <> struct std::formatter<lc1::game::ResourceCategory> : std::formatter<std::string_view> {
    auto format(lc1::game::ResourceCategory value, std::format_context &ctx) const
    {
        std::string_view name;

        switch (value) {
        case lc1::game::ResourceCategory::Currency:
            name = "Currency";
            break;
        case lc1::game::ResourceCategory::Equipment:
            name = "Equipment";
            break;
        case lc1::game::ResourceCategory::Material:
            name = "Material";
            break;
        }

        return std::formatter<std::string_view>::format(name, ctx);
    }
};
