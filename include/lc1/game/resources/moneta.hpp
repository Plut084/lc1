#pragma once

#include "lc1/game/resources/resource.hpp"

#include <cstdint>

namespace lc1::game {

class Moneta : public Resource {
  public:
    ResourceType type() override { return ResourceType::Moneta; }

    ResourceCategory category() override { return ResourceCategory::Currency; }
};

} // namespace lc1::game
