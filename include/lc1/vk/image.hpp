#pragma once

#include "lc1/vk/common.hpp"
#include "lc1/vk/device.hpp"

namespace lc1 {

class Image {
  public:
    Image(Device const &device, vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usage,
          vma::AllocationCreateFlags alloc_flags);

    vk::raii::Image const &raii() const { return image_; }
    vma::Allocation const &allocation() const { return allocation_; }
    vk::ImageView const &view() const { return view_; }

  private:
    Device const *device_;
    vk::raii::Image image_{nullptr};
    vma::Allocation allocation_{nullptr};
    vk::ImageView view_{nullptr};
};

} // namespace lc1
