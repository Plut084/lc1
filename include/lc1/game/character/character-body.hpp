#pragma once

namespace lc1 {

// Body proportions and ground-following distances in metres. The placeholder mesh and the
// collision body are both built from these numbers, so the two cannot silently disagree.
struct CharacterBody {
    float half_width = 0.3F;
    float height = 1.8F;
    float eye_height = 1.65F;
    // Maximum ledge/ramp rise followed while grounded, and maximum distance the body
    // snaps downward to remain attached to a slope.
    float step_height = 0.35F;
    float ground_snap_distance = 0.35F;
};

} // namespace lc1
