#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace lc1 {

// Authored route through the house and its entrance, in continent coordinates.
// The return leg retraces the stairs; no segment jumps between stacked floors.
std::vector<glm::vec3> patrol_house_route();

inline constexpr glm::vec3 patrol_house_spawn{30.0F, 0.6F, 34.0F};

} // namespace lc1
