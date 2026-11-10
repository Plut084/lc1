#pragma once

namespace lc1::combat {
// Simulation timing for the current vertical slash, matching the default Swing clip.
inline constexpr float windup = 0.12F;
inline constexpr float active_end = 0.28F;
inline constexpr float swing_duration = 0.55F;
inline constexpr float cooldown = 2.0F;
inline constexpr float hit_duration = 0.3F;
inline constexpr float death_duration = 0.6F;
} // namespace lc1::combat
