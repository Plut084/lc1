#include "lc1/input.hpp"

namespace lc1 {
namespace {

constexpr std::size_t index_of(Key key) { return static_cast<std::size_t>(key); }
constexpr std::size_t index_of(Action action) { return static_cast<std::size_t>(action); }

} // namespace

bool InputState::flag(std::array<bool, key_count> const &flags, Key key)
{
    auto const index = index_of(key);
    // A Key built by a cast is not something the enum rules out, and an out-of-range index
    // would be a silent out-of-bounds write in on_key. Treat it as never pressed.
    return index < key_count && flags[index];
}

void InputState::begin_frame()
{
    pressed_.fill(false);
    released_.fill(false);
    cursor_delta_ = glm::vec2{0.0F};
    scroll_ = glm::vec2{0.0F};
}

void InputState::on_key(Key key, bool is_down)
{
    auto const index = index_of(key);
    if (index >= key_count)
        return;

    if (is_down) {
        // GLFW cannot deliver two presses without a release between them, but the guard is
        // what keeps "pressed means this frame's transition" true if that ever changes.
        if (!down_[index])
            pressed_[index] = true;
        down_[index] = true;
    }
    else {
        if (down_[index])
            released_[index] = true;
        down_[index] = false;
    }
}

void InputState::on_cursor_position(glm::vec2 position)
{
    // The first position after construction, capture or focus is a baseline: the pointer
    // travelled while nothing was tracking it, and that distance is not camera motion.
    if (cursor_valid_)
        cursor_delta_ += position - cursor_;

    cursor_ = position;
    cursor_valid_ = true;
}

void InputState::on_scroll(glm::vec2 offset)
{
    scroll_ += offset;
}

void InputState::invalidate_cursor()
{
    cursor_valid_ = false;
    // Also drop what the pointer accumulated before the discontinuity: the caller has just
    // declared that this motion was not tracked, and keeping it would apply exactly the
    // jump the baseline exists to prevent -- on this frame rather than the next.
    cursor_delta_ = glm::vec2{0.0F};
}

void InputState::release_all()
{
    for (std::size_t index = 0; index < key_count; ++index) {
        if (!down_[index])
            continue;

        down_[index] = false;
        released_[index] = true;
    }
}

InputRouter::InputRouter(std::span<Binding const> bindings, InputContext initial_context)
    : bindings_(bindings.begin(), bindings.end()), stack_{initial_context}
{
}

void InputRouter::push(InputContext context)
{
    stack_.push_back(context);
}

void InputRouter::pop()
{
    if (stack_.size() > 1)
        stack_.pop_back();
}

bool InputRouter::flag(std::array<bool, action_count> const &flags, Action action)
{
    auto const index = index_of(action);
    return index < action_count && flags[index];
}

void InputRouter::update(InputState const &input)
{
    held_.fill(false);
    pressed_.fill(false);
    released_.fill(false);

    auto const context = active_context();
    for (auto const &binding : bindings_) {
        if (binding.context != context)
            continue;

        // Several keys may share one action -- W and Up both mean MoveForward -- so the
        // action is whatever any of them says.
        auto const index = index_of(binding.action);
        if (index >= action_count)
            continue;

        held_[index] = held_[index] || input.down(binding.key);
        pressed_[index] = pressed_[index] || input.pressed(binding.key);
        released_[index] = released_[index] || input.released(binding.key);
    }

    // A release only counts when the action actually ended. Without this, letting go of one
    // of two keys bound to the same action reports "released" while the other still holds
    // it, and a consumer that starts something on pressed() and finishes it on released()
    // stops half-way.
    //
    // pressed() needs no mask in the other direction. Reporting a second key's press while
    // the action is already held is harmless, and suppressing it would mean asking whether
    // the action was held *before* this frame -- state that would make update() depend on
    // how many times it has been called, which is exactly what it must not do.
    for (std::size_t index = 0; index < action_count; ++index)
        released_[index] = released_[index] && !held_[index];
}

} // namespace lc1
