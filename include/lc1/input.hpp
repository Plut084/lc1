#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Input, the second non-Vulkan module after window.*: it includes no Vulkan header and,
// more importantly, no GLFW header either. window.* is the only place that knows a
// GLFW_KEY_* exists, which is the same split that keeps vk::Extent2D out of window.hpp
// and produced lc1::FrameExtent.
//
// Three layers, and keeping them apart is the whole design:
//   InputState  -- which switches are down, this frame. No meaning, no game rules.
//   Binding     -- one row of data saying "in this mode, this key means that action".
//   InputRouter -- resolves the active mode's bindings into action state.
// Game code asks "is MoveForward held", never "is W down", so rebinding and a second
// input mode cost nothing at the call sites.
namespace lc1 {

// Every key, mouse button and modifier an action can be bound to.
//
// Mouse buttons share the enum deliberately: an action does not care which device
// triggered it, and one vocabulary keeps Binding a flat row instead of a tagged union.
// Unreal's FKey makes the same call (EKeys::LeftMouseButton is an FKey).
//
// The order is not load-bearing -- src/window.cpp's translation table names each Key
// explicitly and is checked by static_assert -- with one exception: the mouse buttons
// stay last, because that table stops at the first of them.
enum class Key : std::uint16_t {
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    Left,
    Right,
    Up,
    Down,

    Space,
    Enter,
    Escape,
    Tab,
    Backspace,
    Delete,
    Insert,
    Home,
    End,
    PageUp,
    PageDown,

    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,
    LeftSuper,
    RightSuper,

    Minus,
    Equal,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Comma,
    Period,
    Slash,
    Grave,

    Keypad0,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
    KeypadDecimal,
    KeypadDivide,
    KeypadMultiply,
    KeypadSubtract,
    KeypadAdd,
    KeypadEnter,
    NumLock,

    MouseLeft,
    MouseRight,
    MouseMiddle,
    MouseX1,
    MouseX2,

    Count,
};

inline constexpr std::size_t key_count = static_cast<std::size_t>(Key::Count);

// Everything before MouseLeft is a keyboard key. src/window.cpp's table has exactly this
// many rows, and the static_asserts beside it turn "forgot one" and "listed one twice"
// into compile errors rather than a key that silently never fires.
inline constexpr std::size_t keyboard_key_count = static_cast<std::size_t>(Key::MouseLeft);

// The state of the physical devices during the frame the application is in.
//
// Two vocabularies, and the difference matters:
//   down()     -- the switch is held right now
//   pressed()  -- it went down since the last frame boundary
//   released() -- it came up since the last frame boundary
//
// Window::poll_events() calls begin_frame() and then pumps GLFW, so an edge always means
// "happened during this frame's polling" and is never consumed twice. That is also why
// begin_frame() is not the application's job: there is no way to forget it and no way to
// put it in the wrong place.
//
// Cursor coordinates are GLFW screen coordinates -- logical points, not framebuffer
// pixels. On a display with a scale factor the two differ, and nothing here converts,
// because the conversion needs the framebuffer size and only the consumer has it. When a
// cursor position has to become a pixel or a world ray, multiply by
// glfwGetWindowContentScale (exposed on the window, not here).
class InputState {
  public:
    bool down(Key key) const { return flag(down_, key); }
    bool pressed(Key key) const { return flag(pressed_, key); }
    bool released(Key key) const { return flag(released_, key); }

    glm::vec2 cursor() const { return cursor_; }
    // Movement accumulated since the last frame boundary. Zero when the pointer did not
    // move, and also zero on the frame that capture or focus changed -- see
    // invalidate_cursor().
    glm::vec2 cursor_delta() const { return cursor_delta_; }
    glm::vec2 scroll() const { return scroll_; }

    // The frame boundary. Called by Window::poll_events() before pumping GLFW, and by
    // nothing else.
    void begin_frame();

    // Fed by Window's GLFW callbacks. Public only because the alternative -- befriending
    // Window -- would couple this header to it for no gain. Treat them as private.
    void on_key(Key key, bool is_down);
    void on_cursor_position(glm::vec2 position);
    void on_scroll(glm::vec2 offset);

    // The next cursor position becomes a baseline instead of a delta, and the delta
    // accumulated so far is dropped. Without this, enabling capture or regaining focus
    // moves the view by however far the pointer travelled while it was not being tracked --
    // one huge jump on the frame it returns.
    void invalidate_cursor();

    // Alt-Tab must not leave a key stuck down. GLFW stops reporting keys to an unfocused
    // window, so "still held" is unknowable and the only safe answer is "none". Every
    // released key reports a release, so a drag cannot survive losing focus half-done.
    void release_all();

  private:
    static bool flag(std::array<bool, key_count> const &flags, Key key);

    std::array<bool, key_count> down_{};
    std::array<bool, key_count> pressed_{};
    std::array<bool, key_count> released_{};

    glm::vec2 cursor_{};
    glm::vec2 cursor_delta_{};
    glm::vec2 scroll_{};
    // False until the first position arrives, and false again after invalidate_cursor().
    bool cursor_valid_ = false;
};

// What the application asks about. These are input-device-independent on purpose: no game
// code names a key, so rebinding is a data change and a new mode is a new table.
//
// The enum lives in the engine because the engine and the app are one program today. The
// day an action starts carrying game rules -- a cooldown, a target, a unit -- it belongs
// to the game layer, and InputRouter does not have to change to let it move.
enum class Action : std::uint16_t {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Sprint,
    ToggleCameraView,
    ToggleShadowPreview,
    ToggleCameraLock,
    RecenterCamera,
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

// One row of the control scheme. Context first because the table reads as groups of mode.
struct Binding {
    InputContext context;
    Action action;
    Key key;
};

// Resolves device state into action state through the active context's bindings.
//
// Bindings are data. This class never knows that W means "forward"; it knows only what the
// application's table says, so adding a control is a row in that table and touches
// nothing else -- not this class, and not the code that reads the action.
class InputRouter {
  public:
    // The table is copied, so it needs no particular lifetime: a local
    // `constexpr std::array` is enough and there is no dangling-view contract to honour.
    InputRouter(std::span<Binding const> bindings, InputContext initial_context);

    void push(InputContext context);
    // Popping the base context is ignored rather than fatal: the stack always has one, so
    // active_context() is never empty and never undefined.
    void pop();
    InputContext active_context() const { return stack_.back(); }

    // Recompute every action from this frame's device state. Call once per frame, after
    // Window::poll_events() and before anything reads an action.
    //
    // Recomputed from scratch rather than updated in place, which has three consequences
    // worth knowing: a context switch cannot strand a key that is still down (a key with no
    // binding in the new context simply resolves to false), calling this twice in one frame
    // gives the same answer both times, and no edge survives into a frame that did not
    // produce it.
    void update(InputState const &input);

    // Action state, combined across every binding the active context gives the action:
    //   held     -- at least one bound key is down
    //   pressed  -- at least one bound key went down this frame. Reported even when the
    //               action was already held through another binding, so a tap that begins
    //               and ends inside one frame is never lost.
    //   released -- no bound key is down and one came up this frame. This one is masked by
    //               held: "released" means the action ended, not merely that some key bound
    //               to it did. Otherwise letting go of one of two keys would tell a consumer
    //               that the drag it started had finished while it was still running.
    bool held(Action action) const { return flag(held_, action); }
    bool pressed(Action action) const { return flag(pressed_, action); }
    bool released(Action action) const { return flag(released_, action); }

  private:
    static bool flag(std::array<bool, action_count> const &flags, Action action);

    std::vector<Binding> bindings_;
    std::vector<InputContext> stack_;
    std::array<bool, action_count> held_{};
    std::array<bool, action_count> pressed_{};
    std::array<bool, action_count> released_{};
};

} // namespace lc1
