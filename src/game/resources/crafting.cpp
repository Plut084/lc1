#include "lc1/game/resources/crafting.hpp"

namespace lc1::game {

Recipe const &iron_helmet_recipe()
{
    static Recipe const recipe{{{&pig_iron, 1}, {&wood, 1}, {&moneta, 5}}, {{&iron_helmet, 1}}};
    return recipe;
}

} // namespace lc1::game
