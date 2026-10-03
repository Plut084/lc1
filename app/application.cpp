#include "application.hpp"

#include "lc1/error.hpp"
#include "lc1/game-clock.hpp"
#include "lc1/vk/swapchain-policy.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>

namespace lc1::app {
namespace {

constexpr double event_wait_timeout_seconds = 0.1;
constexpr std::array device_extensions{vk::KHRSwapchainExtensionName};

Instance make_instance(VulkanLoader const &loader, Window const &window, bool enable_validation)
{
    auto extensions = window.required_instance_extensions();
    std::vector<char const *> layers;
    if (enable_validation) {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    Instance instance{loader, extensions, layers};
    if (enable_validation)
        instance.setup_debug_messenger();
    return instance;
}

OutputSettings output_settings(vk::Format format)
{
    return {.encoding = format == vk::Format::eR8G8B8A8Unorm || format == vk::Format::eB8G8R8A8Unorm
                            ? OutputEncoding::Srgb
                            : OutputEncoding::Linear};
}

// The imgui Vulkan backend is compiled with IMGUI_IMPL_VULKAN_NO_PROTOTYPES (see
// third-party/CMakeLists.txt): every vk* it calls goes through a function-pointer
// table it asks us to fill in. Fill it from the same dlopened loader the raii
// dispatchers come from -- the loader's trampolines answer for device-level
// commands too, so one instance-level lookup covers the backend's whole map.
struct ImGuiVulkanFunctions {
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    VkInstance instance;
};

PFN_vkVoidFunction load_imgui_vulkan_function(char const *name, void *user_data)
{
    auto const *functions = static_cast<ImGuiVulkanFunctions *>(user_data);
    return functions->get_instance_proc_addr(functions->instance, name);
}

} // namespace

Application::Application(ApplicationConfig const &config)
    : window_{1280, 720, "lc1"},
      instance_{make_instance(loader_, window_, config.enable_validation)},
      surface_{instance_, window_}, device_{instance_, surface_, device_extensions},
      surface_format_{
          pick_surface_format(device_.raii_physical().getSurfaceFormatsKHR(surface_.raii()))},
      renderer_{device_,
                {surface_format_.format},
                std::min(device_.max_sample_count(), vk::SampleCountFlagBits::e4)},
      game_{window_, device_, renderer_, config.asset_root,
            output_settings(surface_format_.format)},
      frame_loop_{device_,
                  [this] { return renderer_.make_frame_resources(game_.object_capacity()); }}
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    if (!ImGui_ImplGlfw_InitForVulkan(window_.handle(), true)) {
        ImGui::DestroyContext();
        fail("the imgui GLFW backend could not initialize");
    }
}

void Application::init_imgui_vulkan(std::uint32_t min_image_count)
{
    auto const ui_format = static_cast<VkFormat>(swapchain_->format());
    ImGuiVulkanFunctions imgui_functions{
        .get_instance_proc_addr = loader_.raii().getDispatcher()->vkGetInstanceProcAddr,
        .instance = instance_.handle(),
    };
    if (!ImGui_ImplVulkan_LoadFunctions(vk::ApiVersion14, &load_imgui_vulkan_function,
                                        &imgui_functions))
        fail("the imgui Vulkan backend could not load a required Vulkan function");
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = vk::ApiVersion14;
    init_info.Instance = instance_.handle();
    init_info.PhysicalDevice = *device_.raii_physical();
    init_info.Device = *device_.raii();
    init_info.QueueFamily = device_.queue_family();
    init_info.Queue = *device_.raii_queue();
    // Leave DescriptorPool null: the backend owns the automatically created pool.
    init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
    init_info.MinImageCount = min_image_count;
    init_info.ImageCount = static_cast<std::uint32_t>(swapchain_->images().size());
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    auto &rendering = init_info.PipelineInfoMain.PipelineRenderingCreateInfo;
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &ui_format;
    init_info.UseDynamicRendering = true;
    init_info.CheckVkResultFn = [](VkResult result) {
        if (result < 0)
            fail("imgui Vulkan operation failed: {}", static_cast<int>(result));
    };
    if (!ImGui_ImplVulkan_Init(&init_info))
        fail("the imgui Vulkan backend could not initialize");
    imgui_image_count_ = static_cast<std::uint32_t>(swapchain_->images().size());
}

Application::~Application()
{
    // Initialization may never run (for example, while the window is minimized).
    // Check backend ownership too, so a partially failed Init is cleaned up.
    if (ImGui::GetIO().BackendRendererUserData)
        ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

bool Application::prepare_swapchain()
{
    // Consume every resize event, even when recreation is already pending.
    bool const framebuffer_resized = window_.take_resize_event();
    recreate_requested_ = recreate_requested_ || framebuffer_resized;
    if (swapchain_ && !recreate_requested_)
        return true;

    auto const caps = device_.raii_physical().getSurfaceCapabilitiesKHR(surface_.raii());
    auto const framebuffer = window_.framebuffer_extent();
    vk::Extent2D extent{};
    if (!compute_extent({.width = framebuffer.width, .height = framebuffer.height}, caps,
                        &extent)) {
        // Keep creation/recreation pending. Bounded waits also observe shutdown
        // signals when no further window event arrives.
        window_.wait_events(event_wait_timeout_seconds);
        return false;
    }

    auto const modes = device_.raii_physical().getSurfacePresentModesKHR(surface_.raii());
    SwapchainConfig const config{
        .surface_format = surface_format_,
        .present_mode = pick_present_mode(modes),
        .min_image_count = pick_image_count(caps),
        .composite_alpha = pick_composite_alpha(caps.supportedCompositeAlpha),
    };
    if (config.min_image_count < 2)
        fail("the imgui Vulkan backend requires at least two swapchain images");
    if (swapchain_)
        swapchain_->recreate(config, extent);
    else
        swapchain_.emplace(device_, surface_, config, extent);
    // SetMinImageCount does not update the backend's actual ImageCount.
    // Reinitialize when that count changes; recreation has already drained the GPU.
    if (imgui_image_count_ != 0 && imgui_image_count_ != swapchain_->images().size()) {
        ImGui_ImplVulkan_Shutdown();
        imgui_image_count_ = 0;
    }
    if (imgui_image_count_ == 0)
        init_imgui_vulkan(config.min_image_count);
    else
        ImGui_ImplVulkan_SetMinImageCount(config.min_image_count);
    recreate_requested_ = false;
    game_.set_aspect_ratio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
    return true;
}

void Application::run_loop(std::function<bool()> const &stop_requested)
{
    Stopwatch stopwatch;
    // Construct once; FrameLoop invokes the callback synchronously and never retains it.
    std::function const record = [this](vk::raii::CommandBuffer const &command_buffer,
                                        FrameResources &resources, RenderTarget const &target) {
        renderer_.record(command_buffer, resources, target, game_.camera(), game_.lights(),
                         game_.draws(), game_.output());

        // Make tone mapping's writes visible to the UI attachment load and blending.
        vk::MemoryBarrier2 const barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentRead |
                             vk::AccessFlagBits2::eColorAttachmentWrite,
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
        vk::RenderingAttachmentInfo const color{
            .imageView = *target.view,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eStore,
        };
        vk::RenderingInfo rendering{
            .renderArea = {.offset = {.x = 0, .y = 0}, .extent = target.extent},
            .layerCount = 1,
        };
        rendering.setColorAttachments(color);
        command_buffer.beginRendering(rendering);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *command_buffer);
        command_buffer.endRendering();
    };

    while (!window_.should_close() && !stop_requested()) {
        window_.poll_events();
        // Resolve this frame's events before any action is read, including skipped frames.
        router_.update(window_.input());
        if (window_.should_close() || stop_requested())
            break;
        // Exit is application policy; Window must not consume Escape.
        if (window_.input().pressed(Key::Escape))
            break;

        // Recreate only at the top of a frame, before acquiring an image.
        if (!prepare_swapchain())
            continue;
        game_.update(window_, router_, stopwatch.tick());

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        // Build the UI here.
        ImGui::Text("position: %.2f, %.2f, %.2f", game_.camera().position().x,
                    game_.camera().position().y, game_.camera().position().z);
        ImGui::Render();

        switch (frame_loop_.draw_frame(*swapchain_, record)) {
        case FrameResult::Ok:
            break;
        case FrameResult::RecreateRequested:
            recreate_requested_ = true;
            break;
        case FrameResult::SurfaceLost:
            fail("VK_ERROR_SURFACE_LOST_KHR: the presentation surface is gone "
                 "and is not recoverable in this milestone");
        }
    }
}

void Application::run(std::function<bool()> const &stop_requested)
{
    try {
        run_loop(stop_requested);
    }
    catch (...) {
        // Another frame slot may still reference the game's meshes/materials.
        device_.wait_idle();
        throw;
    }
    device_.wait_idle();
}

} // namespace lc1::app
