#pragma once

#include "lc1/game/resources/resource.hpp"

namespace lc1::game {

class IronHelmet : public Resource {
  public:
    ResourceType type() const override { return ResourceType::IronHelmet; }

    ResourceCategory category() const override { return ResourceCategory::Equipment; }
};

} // namespace lc1::game
