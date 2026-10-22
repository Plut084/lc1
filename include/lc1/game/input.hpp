#pragma once

#include "lc1/input.hpp"

namespace lc1 {

enum class Action : std::uint8_t {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Sprint,
    Jump,
    Interact,
    ToggleCameraView,
    CycleLighting,
    ToggleShowcaseMotion,
    IncreaseExposure,
    DecreaseExposure,
    ToggleCameraLock,
    RecenterCamera,
    DialogResponse0,
    DialogResponse1,
    DialogResponse2,
    DialogResponse3,
    SaveGame,
    Count,
};

inline constexpr std::size_t action_count = static_cast<std::size_t>(Action::Count);

// Which set of bindings is live.
//
// Exactly one context is active at a time -- the one on top of InputRouter's stack --
// because switching modes has to be able to turn a whole set of controls off, and "off" is
// far easier to reason about than priority rules between overlapping layers.
enum class InputContext : std::uint8_t {
    // One ground-level control scheme for countryside, cities and combat.
    Gameplay,
    // Nothing binds here. Pushing it is the gate that hands the keyboard to a text field
    // or to ImGui -- every action simply resolves to false. Included from the start
    // because retrofitting it means auditing every held()/pressed() call site.
    Ui,
};

using Binding = BasicBinding<Action, InputContext>;
using InputRouter = BasicInputRouter<Action, InputContext>;

std::vector<Binding> default_bindings();

} // namespace lc1
