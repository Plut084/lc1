#pragma once

#include "lc1/vk/core/common.hpp"

namespace lc1 {

// Records one image barrier, which does two jobs at once: it changes `image`'s
// layout from old_layout to new_layout, and it orders memory access around
// that change.
//   src_stage / src_access: the earlier work that must finish, and whose
//     writes must be made available, before the transition happens.
//   dst_stage / dst_access: the later work that must wait for the transition,
//     and that will see those writes.
//   aspect: which part of the image the barrier covers -- eColor for a color
//     image, eDepth (plus eStencil when the format carries one) for a depth
//     image. Required, and deliberately without a default: eColor would be
//     wrong for a depth image, and a wrong aspect is a validation error rather
//     than something the compiler can catch.
// Covers mip level 0, layer 0 only.
void transition_image_layout(vk::raii::CommandBuffer const &command_buffer, vk::Image image,
                             vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                             vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
                             vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access,
                             vk::ImageAspectFlags aspect);

} // namespace lc1
