#pragma once

#include "lc1/game/resources/resource.hpp"

namespace lc1::game {

class PigIron : public Resource {
  public:
    ResourceType type() const override { return ResourceType::PigIron; }

    ResourceCategory category() const override { return ResourceCategory::Material; }
};

} // namespace lc1::game
