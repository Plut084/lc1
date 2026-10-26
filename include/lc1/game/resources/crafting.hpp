#pragma once

#include "lc1/game/resources/iron-helmet.hpp"
#include "lc1/game/resources/moneta.hpp"
#include "lc1/game/resources/pig-iron.hpp"
#include "lc1/game/resources/recipe.hpp"
#include "lc1/game/resources/wood.hpp"

namespace lc1::game {

inline Recipe const &iron_helmet_recipe()
{
    // Process-lifetime definitions outlive every bag that borrows a crafted helmet.
    static PigIron const iron;
    static Wood const wood;
    static Moneta const moneta;
    static IronHelmet const helmet;
    static Recipe const recipe{{{&iron, 5}, {&wood, 2}, {&moneta, 20}}, {{&helmet, 1}}};
    return recipe;
}

} // namespace lc1::game
