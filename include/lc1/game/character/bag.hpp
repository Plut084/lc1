#pragma once

#include "lc1/game/resources/resource-stack.hpp"

#include <limits>
#include <unordered_map>

namespace lc1::game {

struct Recipe;

class Bag {
    friend bool craft(Bag &bag, Recipe const &recipe);

  public:
    explicit Bag(std::uint32_t max_volume = 100) : max_volume_(max_volume) {}

    // Borrows a non-null definition that must outlive this bag. Each unit uses one volume.
    // Returns false for insufficient capacity.
    bool add(Resource const &resource, std::uint32_t quantity)
    {
        if (quantity > max_volume_ - volume_)
            return false;

        if (quantity == 0)
            return true;

        auto [result, inserted] = stacks.try_emplace(
            resource.type(), ResourceStack{.resource = &resource, .quantity = 0});
        result->second.quantity += quantity;
        volume_ += quantity;
        return true;
    }

    bool remove(Resource const &resource, std::uint32_t quantity)
    {
        if (quantity == 0)
            return true;

        auto const found = stacks.find(resource.type());
        if (found == stacks.end() || found->second.quantity < quantity)
            return false;

        found->second.quantity -= quantity;
        volume_ -= quantity;
        if (found->second.quantity == 0)
            stacks.erase(found);
        return true;
    }

    std::uint32_t quantity_of(ResourceType type) const
    {
        auto const found = stacks.find(type);
        return found == stacks.end() ? 0 : found->second.quantity;
    }

  private:
    std::unordered_map<game::ResourceType, ResourceStack> stacks;
    std::uint32_t volume_ = 0;
    std::uint32_t max_volume_;
};

} // namespace lc1::game
