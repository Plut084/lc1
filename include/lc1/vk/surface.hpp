#pragma once

#include "lc1/vk/common.hpp"

namespace lc1 {

class Instance;
class Window;

// Owns the window's Vulkan presentation surface. Instance and Window must
// outlive it, and it must outlive every swapchain created for it.
class Surface {
  public:
    Surface(Instance const &instance, Window const &window);

    vk::raii::SurfaceKHR const &raii() const { return surface_; }

  private:
    vk::raii::SurfaceKHR surface_;
};

} // namespace lc1
