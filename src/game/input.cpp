#include "lc1/game/input.hpp"

namespace lc1 {

std::vector<Binding> default_bindings()
{
    std::vector<lc1::Binding> bindings{
        // NOLINTBEGIN
        {lc1::InputContext::Gameplay, lc1::Action::MoveForward, lc1::Key::W},
        {lc1::InputContext::Gameplay, lc1::Action::MoveForward, lc1::Key::Up},
        {lc1::InputContext::Gameplay, lc1::Action::MoveBackward, lc1::Key::S},
        {lc1::InputContext::Gameplay, lc1::Action::MoveBackward, lc1::Key::Down},
        {lc1::InputContext::Gameplay, lc1::Action::MoveLeft, lc1::Key::A},
        {lc1::InputContext::Gameplay, lc1::Action::MoveLeft, lc1::Key::Left},
        {lc1::InputContext::Gameplay, lc1::Action::MoveRight, lc1::Key::D},
        {lc1::InputContext::Gameplay, lc1::Action::MoveRight, lc1::Key::Right},
        {lc1::InputContext::Gameplay, lc1::Action::Sprint, lc1::Key::LeftShift},
        {lc1::InputContext::Gameplay, lc1::Action::Jump, lc1::Key::Space},
        {lc1::InputContext::Gameplay, lc1::Action::Interact, lc1::Key::F},
        {lc1::InputContext::Gameplay, lc1::Action::DialogResponse0, lc1::Key::Digit1},
        {lc1::InputContext::Gameplay, lc1::Action::DialogResponse1, lc1::Key::Digit2},
        {lc1::InputContext::Gameplay, lc1::Action::DialogResponse2, lc1::Key::Digit3},
        {lc1::InputContext::Gameplay, lc1::Action::DialogResponse3, lc1::Key::Digit4},
        {lc1::InputContext::Gameplay, lc1::Action::SaveGame, lc1::Key::F6},
        {lc1::InputContext::Gameplay, lc1::Action::ToggleCameraView, lc1::Key::M},
        {lc1::InputContext::Gameplay, lc1::Action::ToggleCameraLock, lc1::Key::Y},
        {lc1::InputContext::Gameplay, lc1::Action::RecenterCamera, lc1::Key::Space},
        {lc1::InputContext::Gameplay, lc1::Action::CycleLighting, lc1::Key::F4},
        {lc1::InputContext::Gameplay, lc1::Action::ToggleShowcaseMotion, lc1::Key::F5},
        {lc1::InputContext::Gameplay, lc1::Action::IncreaseExposure, lc1::Key::PageUp},
        {lc1::InputContext::Gameplay, lc1::Action::DecreaseExposure, lc1::Key::PageDown},
        // NOLINTEND
    };
    return bindings;
}

} // namespace lc1
