#pragma once

#include "lc1/game/resources/resource.hpp"

namespace lc1::game {

class Wood : public Resource {
  public:
    ResourceType type() const override { return ResourceType::Wood; }

    ResourceCategory category() const override { return ResourceCategory::Material; }
};

} // namespace lc1::game
