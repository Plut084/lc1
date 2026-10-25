#pragma once

#include "lc1/game/resources/resource.hpp"

namespace lc1::game {

class PigIron : public Resource {
  public:
    ResourceType type() override { return ResourceType::PigIron; }

    ResourceCategory category() override { return ResourceCategory::Material; }
};

} // namespace lc1::game
