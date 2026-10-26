#pragma once

#include "lc1/vk/core/common.hpp"

namespace lc1 {

// A single-sampled output image covering extent. The caller owns its layout
// transitions; recording starts and ends in COLOR_ATTACHMENT_OPTIMAL.
struct RenderTarget {
    // Non-owning, non-null: the wrapper must survive recording, and its view and image
    // must remain alive until the submitted commands finish using them.
    vk::raii::ImageView const *view;
    vk::Extent2D extent;
};

} // namespace lc1
