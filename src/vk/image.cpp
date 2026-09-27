#include "lc1/vk/image.hpp"

#include <stb_image.h>

namespace lc1 {

Image::Image(Device const &device, vk::Format format, vk::Extent2D extent,
             vk::ImageUsageFlags usage, vma::AllocationCreateFlags alloc_flags)
{
}

} // namespace lc1
