#include "lc1/vk/frame_loop.hpp"

#include "lc1/vk/device.hpp"
#include "lc1/vk/renderer.hpp"
#include "lc1/vk/swapchain.hpp"

namespace lc1 {
namespace {

// Bounded timeouts, not UINT64_MAX: an unbounded wait makes a deadlock
// indistinguishable from a slow frame or a hung GPU, and leaves no way out.
constexpr uint64_t fence_timeout_ns = 1'000'000'000ULL;   // 1s
constexpr uint64_t acquire_timeout_ns = 1'000'000'000ULL; // 1s

} // namespace

FrameLoop::FrameLoop(Device const &device) : device_{device}
{
    try {
        vk::raii::Device const &raii_device = device_.raii();

        vk::CommandPoolCreateInfo pool_info{
            // RESET_COMMAND_BUFFER_BIT lets us re-begin buffers each frame without
            // reallocating; re-recording every frame is this milestone's model.
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = device_.queue_family(),
        };
        command_pool_ = raii_device.createCommandPool(pool_info);

        vk::CommandBufferAllocateInfo alloc{
            .commandPool = command_pool_,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = frames_in_flight,
        };
        auto command_buffers = raii_device.allocateCommandBuffers(alloc);
        // The count is ours, so this cannot fail unless the driver lies -- but
        // the loop below indexes with it, and a short vector would be an
        // out-of-bounds read rather than an error message.
        if (command_buffers.size() != frames_in_flight) {
            fail("vkAllocateCommandBuffers returned {} command buffers, expected {}",
                 command_buffers.size(), frames_in_flight);
        }

        vk::SemaphoreCreateInfo semaphore_info{};
        // Start signaled so the first wait on each slot returns immediately
        // instead of blocking on a fence that was never submitted.
        vk::FenceCreateInfo fence_info;
        fence_info.setFlags(vk::FenceCreateFlagBits::eSignaled);

        frames_.reserve(frames_in_flight);
        for (auto &command_buffer : command_buffers) {
            frames_.push_back({
                .command_buffer = std::move(command_buffer),
                .image_available = raii_device.createSemaphore(semaphore_info),
                .in_flight = raii_device.createFence(fence_info),
            });
        }
    }
    catch (vk::SystemError const &error) {
        fail("per-frame resource creation failed: {}", error.what());
    }
}

FrameResult FrameLoop::draw_frame(Swapchain &swapchain, Renderer &renderer,
                                  UniformBufferObject const &ubo, std::span<DrawItem const> draws)
{
    Frame const &frame = frames_[frame_index_];
    vk::raii::Device const &raii_device = device_.raii();

    try {
        // Returns a Result instead of throwing: VK_TIMEOUT is a legal outcome
        // here, not a failure.
        vk::Result const wait_result =
            raii_device.waitForFences(*frame.in_flight, vk::True, fence_timeout_ns);
        if (wait_result == vk::Result::eTimeout) {
            fail("fence wait timed out on slot {} -- deadlock or GPU hang. The usual cause is "
                 "vkResetFences on a path that never reaches vkQueueSubmit.",
                 frame_index_);
        }
        if (wait_result != vk::Result::eSuccess) {
            fail("vkWaitForFences failed: {}", result_string(wait_result));
        }

        // On a non-success result the driver left the index unwritten, so
        // .value must not be read before .result has been checked.
        auto const [acquire_result, image_index] =
            swapchain.raii().acquireNextImage(acquire_timeout_ns, *frame.image_available, nullptr);

        if (acquire_result == vk::Result::eErrorOutOfDateKHR) {
            // No image was acquired and both the semaphore and the fence are
            // left unaffected, so skipping the frame is safe. The fence stays
            // signaled; it gets reset on the next use of this slot, immediately
            // before the submit.
            return FrameResult::RecreateRequested;
        }
        // SURFACE_LOST is not in this list because it never gets here: it is
        // not among hpp's success codes for this call, so it arrives as a
        // vk::SurfaceLostKHRError and is caught below.
        if (acquire_result != vk::Result::eSuccess &&
            acquire_result != vk::Result::eSuboptimalKHR) {
            fail("vkAcquireNextImageKHR failed: {}", result_string(acquire_result));
        }

        // VK_SUBOPTIMAL_KHR means an image WAS acquired and
        // frame.image_available IS signaled. Render and present it normally
        // -- returning early would abandon a signaled semaphore, and the next
        // acquire handed that same handle trips
        // VUID-vkAcquireNextImageKHR-semaphore-01286. Recreate after
        // presenting.
        bool const recreate_after_present = (acquire_result == vk::Result::eSuboptimalKHR);

        // vkResetFences belongs HERE and nowhere else: on the path that
        // definitely reaches vkQueueSubmit. Resetting it any earlier (say,
        // right after the wait above) leaves the fence unsignaled with nothing
        // ever submitted, and the next wait on this slot blocks forever with no
        // validation message and no output.
        raii_device.resetFences(*frame.in_flight);

        // Reset THIS slot's command buffer, never the whole pool.
        // vkResetCommandPool resets every buffer allocated from the pool,
        // including the other in-flight frame's -- which is still pending, and
        // trips
        //   VUID-vkResetCommandPool-commandPool-00040: ... is in use
        // Waiting on frame.in_flight only proves this slot is free. This is
        // exactly why the pool is created with
        // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT.
        frame.command_buffer.reset({});
        frame.command_buffer.begin({
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
        });

        SwapchainImage const &image = swapchain.images()[image_index];

        // UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL.
        //
        // srcStageMask must be COLOR_ATTACHMENT_OUTPUT, matching the stage at
        // which submit waits on the acquire semaphore. NONE leaves this layout
        // transition unordered against acquire, and synchronization validation
        // reports SYNC-HAZARD-WRITE-AFTER-READ.
        // srcAccessMask stays NONE: the presentation engine's read is not
        // tracked by access masks. This is an execution-only dependency.
        transition_image_layout(
            frame.command_buffer, image.image, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlags2{},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::ImageAspectFlagBits::eColor);

        RenderTarget const target{
            .view = image.view,
            .extent = swapchain.extent(),
        };

        // frame_index_, not image_index, selects the renderer's per-frame
        // uniform buffer: the fence waited on above is this slot's.
        renderer.record(frame.command_buffer, frame_index_, target, ubo, draws);

        // COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR.
        // Destination stage/access are NONE, not BOTTOM_OF_PIPE: presentation
        // is not a pipeline stage. The submit -> present semaphore pair orders
        // this write against the presentation engine's read.
        transition_image_layout(
            frame.command_buffer, image.image, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2{}, // VK_PIPELINE_STAGE_2_NONE
            vk::AccessFlags2{},           // VK_ACCESS_2_NONE
            vk::ImageAspectFlagBits::eColor);

        frame.command_buffer.end();

        // One dereference to the C handle: vk::raii::Semaphore -> VkSemaphore
        // would be two user-defined conversions, and those do not chain.
        vk::Semaphore const render_finished = *image.render_finished;

        vk::SemaphoreSubmitInfo wait_semaphore;
        wait_semaphore
            .setSemaphore(*frame.image_available)
            // The first thing that touches the image is the layout transition
            // and the attachment write, both in the color-attachment-output
            // scope.
            .setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput);

        vk::SemaphoreSubmitInfo signal_semaphore;
        signal_semaphore.setSemaphore(render_finished)
            .setStageMask(vk::PipelineStageFlagBits2::eAllCommands);

        vk::CommandBufferSubmitInfo command_info;
        command_info.setCommandBuffer(*frame.command_buffer);

        vk::SubmitInfo2 submit;
        submit.setWaitSemaphoreInfos(wait_semaphore)
            .setCommandBufferInfos(command_info)
            .setSignalSemaphoreInfos(signal_semaphore);

        device_.raii_queue().submit2(submit, *frame.in_flight);

        // Never present without the submit above: the wait semaphore must
        // reference a signal operation that was actually submitted.
        vk::SwapchainKHR const swapchain_handle = *swapchain.raii();
        vk::PresentInfoKHR present;
        present.setWaitSemaphores(render_finished)
            .setSwapchains(swapchain_handle)
            .setImageIndices(image_index);

        vk::Result const presented = device_.raii_queue().presentKHR(present);
        if (presented == vk::Result::eErrorOutOfDateKHR ||
            presented == vk::Result::eSuboptimalKHR) {
            return FrameResult::RecreateRequested;
        }
        if (presented != vk::Result::eSuccess) {
            fail("vkQueuePresentKHR failed: {}", result_string(presented));
        }

        frame_index_ = (frame_index_ + 1) % frames_in_flight;
        return recreate_after_present ? FrameResult::RecreateRequested : FrameResult::Ok;
    }
    catch (vk::SurfaceLostKHRError const &) {
        // Must come before the SystemError catch, which is its base class.
        return FrameResult::SurfaceLost;
    }
    catch (vk::SystemError const &error) {
        fail("frame failed: {}", error.what());
    }
}

} // namespace lc1
