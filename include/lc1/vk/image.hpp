#pragma once

#include "lc1/vk/common.hpp"
#include "lc1/vk/memory.hpp"

namespace lc1 {

class Device;

vk::Format find_supported_format(std::vector<vk::Format> const &candidates, vk::ImageTiling tiling,
                                 vk::FormatFeatureFlags features);

vk::Format find_depth_format(Device const &device);

// A GPU image, its VMA allocation, and a view of the whole image. Knows nothing
// about what it holds: a texture and a depth buffer differ only in format and
// usage (and, for depth, the view's aspect). Filling it is the owner's job --
// see Texture.
class Image {
  public:
    Image(Device const &device, vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usage,
          vk::ImageAspectFlags aspect);

    vk::raii::Image const &raii() const { return image_; }
    vk::raii::ImageView const &view() const { return view_; }
    vk::Extent2D extent() const { return extent_; }

  private:
    // Owns both the VkImage and the VmaAllocation, and frees them together.
    // Must stay a vma::raii::Image: moving it into a vk::raii::Image keeps the
    // image but frees the memory at the end of that statement.
    vma::raii::Image image_{nullptr};
    // Declared after image_ so it is destroyed first: the view refers to it.
    vk::raii::ImageView view_{nullptr};
    vk::Extent2D extent_;
};

} // namespace lc1
