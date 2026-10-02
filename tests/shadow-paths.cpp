#include "lc1/image.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/gpu-texture.hpp"
#include "lc1/vk/instance.hpp"
#include "lc1/vk/loader.hpp"
#include "lc1/vk/one-time-submit.hpp"
#include "lc1/vk/renderer.hpp"
#include "lc1/vk/surface.hpp"
#include "lc1/window.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {

void check(bool condition, char const *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

enum class View { Lit, ShadowMapDepth };

// Exercise actual draw calls and shader reads, not just pipeline construction.
std::vector<std::uint8_t> render(lc1::Device const &device, lc1::Renderer &renderer,
                                 lc1::FrameResources &frame, lc1::scene::FpsCamera &camera,
                                 std::span<lc1::scene::Light const> lights,
                                 std::span<lc1::DrawItem const> draws, View view,
                                 vk::Extent2D extent = {64, 64})
{
    lc1::GpuImage output{device,
                         vk::Format::eR8G8B8A8Unorm,
                         extent,
                         1,
                         vk::SampleCountFlagBits::e1,
                         vk::ImageUsageFlagBits::eColorAttachment |
                             vk::ImageUsageFlagBits::eTransferSrc,
                         vk::ImageAspectFlagBits::eColor};
    auto readback = device.allocator().createBuffer(
        {.size = extent.width * extent.height * 4, .usage = vk::BufferUsageFlagBits::eTransferDst},
        {.flags = vma::AllocationCreateFlagBits::eHostAccessRandom,
         .usage = vma::MemoryUsage::eAuto});
    camera.set_aspect_ratio(static_cast<float>(extent.width) / extent.height);
    lc1::one_time_submit(device, [&](vk::raii::CommandBuffer const &commands) {
        output.transition_layout(commands, vk::ImageLayout::eUndefined,
                                 vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::PipelineStageFlagBits2::eNone, {},
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite);
        lc1::RenderTarget const target{output.view(), extent};
        if (view == View::Lit)
            renderer.record(commands, frame, target, camera, lights, draws);
        else
            renderer.record_shadow_map_preview(commands, frame, target, lights, draws,
                                               {.center = {0, 0, 0}, .half_extent = 4.0F});
        output.transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::eTransferSrcOptimal,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eCopy,
                                 vk::AccessFlagBits2::eTransferRead);
        vk::BufferImageCopy const copy{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent = {extent.width, extent.height, 1},
        };
        commands.copyImageToBuffer(*output.raii(), vk::ImageLayout::eTransferSrcOptimal, *readback,
                                   copy);
        vk::MemoryBarrier2 const barrier{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                         .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                         .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                         .dstAccessMask = vk::AccessFlagBits2::eHostRead};
        commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
    });
    std::vector<std::uint8_t> pixels(extent.width * extent.height * 4);
    readback.getAllocation().copyToMemory(0, pixels.data(), pixels.size());
    return pixels;
}

void exercise(lc1::Device const &device)
{
    auto const samples = std::min(device.max_sample_count(), vk::SampleCountFlagBits::e4);
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Unorm}, samples};
    lc1::GpuTexture texture{device,
                            lc1::Image::load_from_file("assets/textures/prototype-white.png")};
    auto material = renderer.make_material(texture);
    std::array const vertices{
        lc1::Vertex{.position = {-2, 0, -2}, .uv = {0, 0}},
        lc1::Vertex{.position = {-2, 0, 2}, .uv = {0, 1}},
        lc1::Vertex{.position = {2, 0, 2}, .uv = {1, 1}},
        lc1::Vertex{.position = {2, 0, -2}, .uv = {1, 0}},
    };
    std::array<std::uint32_t, 6> const indices{0, 1, 2, 0, 2, 3};
    lc1::GpuMesh mesh{device, std::span<lc1::Vertex const>{vertices},
                      std::span<std::uint32_t const>{indices}};
    std::array draws{lc1::DrawItem{.mesh = &mesh, .material = &material}};
    std::array const lights{
        lc1::scene::to_light(lc1::scene::DirectionalLight{.base = {}, .direction = {0, -1, 0}})};
    lc1::scene::FpsCamera camera;
    camera.set_zfar(50.0F);
    camera.set_position({0, 4, 5});
    camera.look_at({0, 0, 0});
    auto normal_frame = renderer.make_frame_resources(1);
    auto preview_frame = renderer.make_frame_resources(1);

    // A fresh preview slot must bind only its own descriptors. Starting here
    // catches accidental dependence on a previous ray-query frame's setup.
    auto const preview =
        render(device, renderer, preview_frame, camera, lights, draws, View::ShadowMapDepth);
    check(preview_frame.shadow_map.has_value(), "preview did not allocate its depth map");
    check(!preview_frame.ray_query.scene && !preview_frame.ray_query.visibility &&
              !preview_frame.depth_image && !preview_frame.color_image,
          "preview allocated ray-query or main-pass attachments");
    auto const [dark, bright] = std::minmax_element(preview.begin(), preview.end());
    check(*dark < 200 && *bright == 255, "depth preview contains no rendered geometry");
    for (std::size_t i = 0; i < preview.size(); i += 4)
        check(preview[i] == preview[i + 1] && preview[i] == preview[i + 2],
              "depth preview must be grayscale");

    auto const first_lit = render(device, renderer, normal_frame, camera, lights, draws, View::Lit);
    check(!normal_frame.shadow_map, "normal rendering allocated the legacy depth map");
    check(normal_frame.ray_query.scene && normal_frame.ray_query.visibility,
          "normal rendering did not prepare ray-query resources");
    check(first_lit != preview, "normal rendering still displays the legacy preview");

    // Switch both ways on both slots, resize while previewing, then return to
    // ray-query rendering. Validation checks descriptors, layouts and lifetimes.
    render(device, renderer, preview_frame, camera, lights, draws, View::Lit);
    render(device, renderer, normal_frame, camera, lights, draws, View::ShadowMapDepth, {96, 48});
    auto const resumed = render(device, renderer, normal_frame, camera, lights, draws, View::Lit);
    check(resumed == first_lit, "returning from preview changed a static hard-lit scene");
    // Printed labels can render without entering either shadow-caster path.
    // Also exercise an empty TLAS and invalidate history when caster membership changes.
    draws[0].casts_shadow = false;
    auto const no_caster = render(device, renderer, normal_frame, camera, lights, draws, View::Lit);
    check(no_caster == first_lit && normal_frame.ray_query.built_instances.empty(),
          "non-caster must remain visible without entering the TLAS");
    auto const empty_preview =
        render(device, renderer, preview_frame, camera, lights, draws, View::ShadowMapDepth);
    check(std::ranges::all_of(empty_preview, [](auto value) { return value == 255; }),
          "non-caster must not appear in the depth preview");
    draws[0].casts_shadow = true;
    render(device, renderer, normal_frame, camera, lights, draws, View::Lit);
    check(normal_frame.ray_query.built_instances.size() == 1, "caster must rejoin the TLAS");
    render(device, renderer, preview_frame, camera, lights, draws, View::Lit, {96, 48});
    render(device, renderer, normal_frame, camera, lights, draws, View::Lit, {96, 48});
    render(device, renderer, preview_frame, camera, {}, draws, View::ShadowMapDepth);
    draws[0].model = glm::scale(glm::mat4{1}, glm::vec3{-1, 1, 1});
    render(device, renderer, normal_frame, camera, lights, draws, View::ShadowMapDepth);
    render(device, renderer, normal_frame, camera, lights, draws, View::Lit);
    device.wait_idle();
}

} // namespace

int main()
{
    try {
        lc1::VulkanLoader loader;
        lc1::Window window{64, 64, "shadow path GPU tests"};
        glfwHideWindow(window.handle());
        auto extensions = window.required_instance_extensions();
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
        lc1::Instance instance{loader, extensions, {"VK_LAYER_KHRONOS_validation"}};
        instance.setup_debug_messenger();
        lc1::Surface surface{instance, window};
        lc1::Device device{instance, surface, {}};
        exercise(device);
        std::cout << "shadow path isolation, switching, resize and rendering passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
