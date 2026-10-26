#pragma once

#include "lc1/game/character/bag.hpp"

#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace lc1::game {

struct Recipe {
  public:
    Recipe(std::vector<ResourceStack> inputs, std::vector<ResourceStack> outputs)
        : inputs(std::move(inputs)), outputs(std::move(outputs))
    {
        for (auto const &input : this->inputs)
            input_volume += input.quantity;

        for (auto const &output : this->outputs)
            output_volume += output.quantity;
    }

    // Definition borrows must outlive this recipe and any bag receiving its outputs.
    std::vector<ResourceStack> inputs;
    std::vector<ResourceStack> outputs;
    std::uint64_t input_volume{};
    std::uint64_t output_volume{};
};

namespace detail {
inline std::optional<Bag> prepare_recipe(Bag const &bag, Recipe const &recipe)
{
    // Work on a candidate so even allocation failure cannot partially consume inputs.
    auto candidate = bag;
    for (auto const &[resource, quantity] : recipe.inputs) {
        assert(resource);
        if (!candidate.remove(*resource, quantity))
            return std::nullopt;
    }
    for (auto const &[resource, quantity] : recipe.outputs) {
        assert(resource);
        if (!candidate.add(*resource, quantity))
            return std::nullopt;
    }
    return candidate;
}
} // namespace detail

inline bool can_craft(Bag const &bag, Recipe const &recipe)
{
    return detail::prepare_recipe(bag, recipe).has_value();
}

inline bool craft(Bag &bag, Recipe const &recipe)
{
    auto candidate = detail::prepare_recipe(bag, recipe);
    if (!candidate)
        return false;
    bag.stacks.swap(candidate->stacks);
    std::swap(bag.volume_, candidate->volume_);
    return true;
}

} // namespace lc1::game
