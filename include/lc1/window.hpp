#pragma once

#include "lc1/error.hpp"

#include <GLFW/glfw3.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace lc1 {

// A framebuffer size in pixels. {0, 0} means "nothing to render to" -- the
// window is minimized or the surface has no usable extent.
//
// Not vk::Extent2D: this header must not depend on Vulkan.
struct FrameExtent {
    uint32_t width = 0;
    uint32_t height = 0;
};

// The GLFW window, and with it the GLFW library lifetime.
//
// One Window per process: glfwInit/glfwTerminate are process-wide state, so
// copies and moves are deleted. With no moved-from state, handle_ is never
// null in a constructed Window.
class Window {
  public:
    Window(int width, int height, std::string_view title);
    ~Window();

    Window(Window const &) = delete;
    Window &operator=(Window const &) = delete;
    Window(Window &&) = delete;
    Window &operator=(Window &&) = delete;

    // Throws instead of returning an empty vector: an Instance built without
    // VK_KHR_surface fails much later and with a much worse message.
    std::vector<char const *> required_instance_extensions() const;

    // Borrowed, for glfwCreateWindowSurface in Device.
    GLFWwindow *handle() const { return handle_; }

    bool should_close() const;

    // Reads and clears the flag the resize callback sets. The caller must
    // consume it unconditionally: `recreate_requested ||
    // window.take_resize_event()` short-circuits, leaves the flag set, and
    // forces a second rebuild on the next frame.
    bool take_resize_event();

    // Not const: dispatching events runs the resize callback, which writes
    // framebuffer_resized_.
    void poll_events();
    // Bounded so the application can also check signal-based shutdown.
    // timeout_seconds must be positive and finite.
    void wait_events(double timeout_seconds);

    // {0, 0} when the window is minimized or the framebuffer is unusable.
    FrameExtent framebuffer_extent() const;

  private:
    static void framebuffer_size_callback(GLFWwindow *window, int width,
                                          int height);

    struct GlfwLifetime {
        ~GlfwLifetime();
    };

    // Declared first so it is destroyed last, after ~Window's
    // glfwDestroyWindow. A member rather than a call in ~Window so that it
    // also runs when the constructor throws; glfwTerminate is a no-op when
    // GLFW is not initialized.
    GlfwLifetime glfw_lifetime_;

    // Owning: GLFW has no raii form, so ~Window is the single destroy site.
    GLFWwindow *handle_ = nullptr;
    bool framebuffer_resized_ = false;
};

} // namespace lc1
