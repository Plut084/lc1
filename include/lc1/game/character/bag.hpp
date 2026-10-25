#pragma once

#include "lc1/game/resources/resource-stack.hpp"

#include <limits>
#include <unordered_map>

namespace lc1::game {

struct Bag {
    std::unordered_map<game::ResourceType, ResourceStack> stacks;
    std::uint32_t max_num_stacks;

    std::uint32_t quantity(ResourceType type) const
    {
        auto const found = stacks.find(type);
        return found == stacks.end() ? 0 : found->second.quantity;
    }

    bool can_forge_iron_helmet() const
    {
        return quantity(ResourceType::PigIron) >= 5 && quantity(ResourceType::Wood) >= 2 &&
               quantity(ResourceType::Moneta) >= 20 &&
               quantity(ResourceType::IronHelmet) < std::numeric_limits<std::uint32_t>::max() &&
               (stacks.contains(ResourceType::IronHelmet) || stacks.size() < max_num_stacks);
    }

    bool forge_iron_helmet(Resource &helmet)
    {
        if (helmet.type() != ResourceType::IronHelmet || !can_forge_iron_helmet())
            return false;
        // Allocate the result before consuming anything, so allocation failure preserves inputs.
        auto [result, inserted] =
            stacks.try_emplace(ResourceType::IronHelmet, ResourceStack{&helmet, 0});
        ++result->second.quantity;
        stacks.at(ResourceType::PigIron).quantity -= 5;
        stacks.at(ResourceType::Wood).quantity -= 2;
        stacks.at(ResourceType::Moneta).quantity -= 20;
        return true;
    }
};

} // namespace lc1::game
