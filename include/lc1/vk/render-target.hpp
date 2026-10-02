#pragma once

#include "lc1/vk/common.hpp"

namespace lc1 {

// Linear writes are encoded by sRGB attachments automatically. Srgb is for
// manually encoding display output into a UNORM attachment only.
enum class OutputEncoding { Linear, Srgb };

struct OutputSettings {
    float exposure = 1.0F;
    OutputEncoding encoding = OutputEncoding::Linear;
};

// A single-sampled output image covering extent. The caller owns its layout
// transitions; recording starts and ends in COLOR_ATTACHMENT_OPTIMAL.
struct RenderTarget {
    // Non-owning: the wrapper must survive recording, and its view and image
    // must remain alive until the submitted commands finish using them.
    vk::raii::ImageView const &view;
    vk::Extent2D extent;
};

} // namespace lc1
