#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/device.hpp"

#include <limits>

namespace lc1 {

// Records `record` into a fresh command buffer, submits it, and blocks until the
// GPU has finished it. For setup work such as uploads -- never inside the frame
// loop, where the wait would stall rendering.
//
// A transient pool per call: cheap enough for one-off work, and it leaves
// FrameLoop's pool untouched.
void one_time_submit(Device const &device, auto &&record)
{
    vk::raii::Device const &raii_device = device.raii();

    vk::raii::CommandPool const pool = raii_device.createCommandPool({
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = device.queue_family(),
    });
    vk::raii::CommandBuffers const command_buffers{
        raii_device,
        {
            .commandPool = *pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        },
    };
    vk::raii::CommandBuffer const &command_buffer = command_buffers.front();

    command_buffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    record(command_buffer);
    command_buffer.end();

    vk::CommandBufferSubmitInfo command_info;
    command_info.setCommandBuffer(*command_buffer);
    vk::SubmitInfo2 submit;
    submit.setCommandBufferInfos(command_info);

    vk::raii::Fence const fence = raii_device.createFence({});
    device.raii_queue().submit2(submit, *fence);
    if (raii_device.waitForFences(*fence, vk::True, std::numeric_limits<std::uint64_t>::max()) !=
        vk::Result::eSuccess) {
        fail("one_time_submit: waiting for the fence failed");
    }
}

} // namespace lc1
