#include "lc1/window.hpp"

#include <cstdio>
#include <print>
#include <string>

namespace lc1 {
namespace {

// Set before glfwInit so that a failure *inside* initialization is reported
// through it as well.
void glfw_error_callback(int code, char const *description)
{
    std::println(stderr, "[glfw] error {}: {}", code, description);
}

} // namespace

Window::Window(int width, int height, std::string_view title)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() != GLFW_TRUE)
        fail("glfwInit failed");

    if (glfwVulkanSupported() != GLFW_TRUE)
        fail("GLFW reports no Vulkan support");

    // Must come after glfwInit and before glfwCreateWindow.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    handle_ = glfwCreateWindow(width, height, std::string{title}.c_str(), nullptr, nullptr);
    if (handle_ == nullptr)
        fail("glfwCreateWindow failed");

    // The user pointer goes on first: the callback reads it, so registering the
    // callback first would leave a window whose callback finds no Window.
    glfwSetWindowUserPointer(handle_, this);
    glfwSetFramebufferSizeCallback(handle_, &Window::framebuffer_size_callback);
}

Window::~Window()
{
    glfwDestroyWindow(handle_);
}

Window::GlfwLifetime::~GlfwLifetime()
{
    glfwTerminate();
}

void Window::framebuffer_size_callback(GLFWwindow *window, int /*width*/, int /*height*/)
{
    // Only sets a flag. Recreation happens at the top of the frame, before
    // acquire -- never from inside the callback, where it would destroy the
    // swapchain out from under an image that may already be acquired.
    auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
    if (self == nullptr)
        return;
    self->framebuffer_resized_ = true;
}

std::vector<char const *> Window::required_instance_extensions() const
{
    uint32_t count = 0;
    char const **const extensions = glfwGetRequiredInstanceExtensions(&count);
    if (extensions == nullptr) {
        fail("glfwGetRequiredInstanceExtensions returned null "
             "(no Vulkan surface support for this platform)");
    }
    return {extensions, extensions + count};
}

bool Window::should_close() const
{
    return glfwWindowShouldClose(handle_) != GLFW_FALSE;
}

bool Window::take_resize_event()
{
    bool const resized = framebuffer_resized_;
    framebuffer_resized_ = false;
    return resized;
}

void Window::poll_events()
{
    glfwPollEvents();
}

void Window::wait_events()
{
    glfwWaitEvents();
}

FrameExtent Window::framebuffer_extent() const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(handle_, &width, &height);

    if (width <= 0 || height <= 0)
        return FrameExtent{};

    return FrameExtent{.width = static_cast<uint32_t>(width),
                       .height = static_cast<uint32_t>(height)};
}

} // namespace lc1
