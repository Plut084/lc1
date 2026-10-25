#pragma once

#include "lc1/game/resources/resource.hpp"

namespace lc1::game {

class IronHelmet : public Resource {
  public:
    ResourceType type() override { return ResourceType::IronHelmet; }

    ResourceCategory category() override { return ResourceCategory::Equipment; }
};

} // namespace lc1::game
