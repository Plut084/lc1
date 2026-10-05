#include "lc1/vk/resources/descriptor-heap.hpp"
#include "lc1/vk/core/command-buffer.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/loader.hpp"
#include "lc1/vk/resources/gpu-buffer.hpp"
#include "lc1/vk/resources/gpu-image.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"
#include "lc1/vk/resources/shader-stages.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#include <span>
#include <vector>

namespace {
constexpr vk::Extent2D extent{256, 256};
constexpr auto format = vk::Format::eR8G8B8A8Unorm;
using Pixel = std::array<std::uint8_t, 4>;
using Triangle = std::array<std::array<float, 4>, 4>;

lc1::Device make_device(lc1::Instance const &instance)
{
    lc1::DeviceRequirements const requirements{
        .vulkan_version = vk::ApiVersion14,
        .extensions = {vk::EXTDescriptorHeapExtensionName,
                       vk::KHRShaderUntypedPointersExtensionName},
        .features = {&vk::PhysicalDeviceFeatures::samplerAnisotropy,
                     &vk::PhysicalDeviceVulkan11Features::shaderDrawParameters,
                     &vk::PhysicalDeviceVulkan12Features::bufferDeviceAddress,
                     &vk::PhysicalDeviceVulkan13Features::dynamicRendering,
                     &vk::PhysicalDeviceVulkan13Features::synchronization2,
                     &vk::PhysicalDeviceVulkan14Features::maintenance5,
                     &vk::PhysicalDeviceDescriptorHeapFeaturesEXT::descriptorHeap,
                     &vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR::shaderUntypedPointers},
    };
    for (auto &physical : lc1::filter_physical_devices(instance, requirements)) {
        if (physical.getProperties().deviceType == vk::PhysicalDeviceType::eCpu)
            continue;
        auto const families = physical.getQueueFamilyProperties();
        for (std::uint32_t i = 0; i < families.size(); ++i) {
            if (families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                std::cout << "Descriptor heap test device: "
                          << physical.getProperties().deviceName.data() << '\n';
                return lc1::Device{instance, std::move(physical), i, requirements};
            }
        }
    }
    lc1::fail("descriptor heap test requires a Vulkan 1.4 GPU with descriptorHeap and "
              "shaderUntypedPointers");
}

vk::raii::Pipeline make_pipeline(lc1::Device const &device)
{
    auto shader = lc1::ShaderModule::load_from_file(device, "shaders/descriptor-heap-test.spv");
    lc1::ShaderStages stages;
    stages.append(vk::ShaderStageFlagBits::eVertex, shader, "vertMain");
    stages.append(vk::ShaderStageFlagBits::eFragment, shader, "fragMain");
    vk::PipelineVertexInputStateCreateInfo const vertex_input{};
    vk::PipelineInputAssemblyStateCreateInfo const assembly{
        .topology = vk::PrimitiveTopology::eTriangleList};
    vk::Viewport const viewport{.width = 256, .height = 256, .maxDepth = 1};
    vk::Rect2D const scissor{.extent = extent};
    vk::PipelineViewportStateCreateInfo viewport_state;
    viewport_state.setViewports(viewport).setScissors(scissor);
    vk::PipelineRasterizationStateCreateInfo const raster{.polygonMode = vk::PolygonMode::eFill,
                                                          .cullMode = vk::CullModeFlagBits::eNone,
                                                          .frontFace =
                                                              vk::FrontFace::eCounterClockwise,
                                                          .lineWidth = 1};
    vk::PipelineMultisampleStateCreateInfo const multisample{.rasterizationSamples =
                                                                 vk::SampleCountFlagBits::e1};
    vk::PipelineColorBlendAttachmentState const attachment{
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
    vk::PipelineColorBlendStateCreateInfo blend;
    blend.setAttachments(attachment);
    vk::PipelineRenderingCreateInfo rendering;
    rendering.setColorAttachmentFormats(format);
    vk::PipelineCreateFlags2CreateInfo const flags{
        .pNext = &rendering, .flags = vk::PipelineCreateFlagBits2::eDescriptorHeapEXT};
    vk::GraphicsPipelineCreateInfo info{.pNext = &flags,
                                        .pVertexInputState = &vertex_input,
                                        .pInputAssemblyState = &assembly,
                                        .pViewportState = &viewport_state,
                                        .pRasterizationState = &raster,
                                        .pMultisampleState = &multisample,
                                        .pColorBlendState = &blend,
                                        // Native heap pipelines require a null pipeline layout.
                                        .layout = nullptr};
    info.setStages(stages.value());
    return device.raii().createGraphicsPipeline(nullptr, info);
}

void run(lc1::Device const &device)
{
    std::array<Triangle, 2> const triangles{{
        {{{-0.9F, 0.7F, 0, 1}, {-0.1F, 0.7F, 0, 1}, {-0.5F, -0.7F, 0, 1}, {1, 1, 1, 1}}},
        {{{0.1F, 0.7F, 0, 1}, {0.9F, 0.7F, 0, 1}, {0.5F, -0.7F, 0, 1}, {1, 1, 1, 1}}},
    }};
    std::vector<lc1::GpuBuffer> data;
    for (auto const &triangle : triangles) {
        data.emplace_back(device, sizeof(Triangle),
                          vk::BufferUsageFlagBits2::eUniformBuffer |
                              vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                          vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
        data.back().upload(std::as_bytes(std::span{triangle}));
    }
    std::array<Pixel, 2> const texels{{{255, 64, 0, 255}, {0, 191, 255, 255}}};
    std::array textures{lc1::GpuTexture{device, {1, 1}, texels[0], lc1::TextureColorSpace::Linear},
                        lc1::GpuTexture{device, {1, 1}, texels[1], lc1::TextureColorSpace::Linear}};
    // Allocate every descriptor through the production allocator. The caller does
    // not know heap offsets, reserved ranges, strides, or descriptor encoding.
    lc1::DescriptorHeap heap{device, 2, 2, 2};

    struct PushData {
        std::uint32_t buffer_index;
        std::uint32_t image_index;
        std::uint32_t sampler_index;
    };

    static_assert(sizeof(PushData) == 12);
    auto shader_index = [](std::size_t index) {
        if (index > std::numeric_limits<std::uint32_t>::max())
            lc1::fail("heap index exceeds the shader's uint32 index");
        return static_cast<std::uint32_t>(index);
    };
    std::array<PushData, 2> draws;
    for (std::size_t i = 0; i < draws.size(); ++i)
        draws[i] = {shader_index(heap.allocate_buffer(data[i])),
                    shader_index(heap.allocate_image(textures[i].image())),
                    shader_index(heap.allocate_sampler())};
    auto expect_full = [](auto &&allocate) {
        try {
            allocate();
        }
        catch (lc1::Error const &) {
            return;
        }
        lc1::fail("heap allocation unexpectedly succeeded beyond capacity");
    };
    expect_full([&] { heap.allocate_buffer(data[0]); });
    expect_full([&] { heap.allocate_image(textures[0].image()); });
    expect_full([&] { heap.allocate_sampler(); });
    lc1::DescriptorHeap empty{device, 0, 0, 0};
    expect_full([&] { empty.allocate_buffer(data[0]); });
    expect_full([&] { empty.allocate_image(textures[0].image()); });
    expect_full([&] { empty.allocate_sampler(); });

    lc1::GpuImage output{device,
                         format,
                         extent,
                         1,
                         vk::SampleCountFlagBits::e1,
                         vk::ImageUsageFlagBits::eColorAttachment |
                             vk::ImageUsageFlagBits::eTransferSrc,
                         vk::ImageAspectFlagBits::eColor};
    auto readback =
        device.allocator().createBuffer({.size = extent.width * extent.height * sizeof(Pixel),
                                         .usage = vk::BufferUsageFlagBits::eTransferDst},
                                        {.flags = vma::AllocationCreateFlagBits::eHostAccessRandom,
                                         .usage = vma::MemoryUsage::eAuto});
    auto pipeline = make_pipeline(device);
    auto pool = device.raii().createCommandPool(
        {.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
         .queueFamilyIndex = device.queue_family()});
    auto buffers = device.raii().allocateCommandBuffers(
        {.commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
    lc1::CommandBuffer command_buffer{std::move(buffers.front())};
    auto fence = device.raii().createFence({});
    std::vector<Pixel> pixels(extent.width * extent.height);
    // Re-record the same wrapper to cover invalidation of its heap binding cache.
    for (std::uint32_t frame = 0; frame < 2; ++frame) {
        command_buffer.reset();
        auto &commands = command_buffer.raii();
        commands.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        command_buffer.bind_descriptor_heap(heap);
        output.transition_layout(
            commands,
            frame == 0 ? vk::ImageLayout::eUndefined : vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eCopy,
            vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite);
        vk::RenderingAttachmentInfo const color{
            .imageView = *output.view(),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = vk::ClearValue{vk::ClearColorValue{std::array{0.F, 0.F, 0.F, 1.F}}}};
        commands.beginRendering(vk::RenderingInfo{.renderArea = {.extent = extent}, .layerCount = 1}
                                    .setColorAttachments(color));
        commands.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
        for (auto const &draw : draws) {
            heap.push_data(commands, std::span{&draw, 1});
            commands.draw(3, 1, 0, 0);
        }
        commands.endRendering();
        output.transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::eTransferSrcOptimal,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eCopy,
                                 vk::AccessFlagBits2::eTransferRead);
        vk::BufferImageCopy const region{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent = {extent.width, extent.height, 1}};
        commands.copyImageToBuffer(*output.raii(), vk::ImageLayout::eTransferSrcOptimal, *readback,
                                   region);
        vk::MemoryBarrier2 const barrier{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                         .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                         .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                         .dstAccessMask = vk::AccessFlagBits2::eHostRead};
        commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
        commands.end();
        vk::CommandBufferSubmitInfo const command_info{.commandBuffer = *commands};
        device.raii_queue().submit2(vk::SubmitInfo2{}.setCommandBufferInfos(command_info), *fence);
        if (device.raii().waitForFences(*fence, vk::True,
                                        std::numeric_limits<std::uint64_t>::max()) !=
            vk::Result::eSuccess)
            lc1::fail("descriptor heap test fence wait failed");
        device.raii().resetFences(*fence);
        readback.getAllocation().copyToMemory(0, pixels.data(), pixels.size() * sizeof(Pixel));
        auto check = [&](std::uint32_t x, std::uint32_t y, Pixel expected) {
            auto const actual = pixels[y * extent.width + x];
            for (std::size_t channel = 0; channel < actual.size(); ++channel)
                if (std::abs(static_cast<int>(actual[channel]) -
                             static_cast<int>(expected[channel])) > 1)
                    lc1::fail("pixel ({}, {}) channel {}: expected {}, got {}", x, y, channel,
                              expected[channel], actual[channel]);
        };
        check(64, 128, {255, 64, 0, 255});
        check(192, 128, {0, 191, 255, 255});
        check(128, 128, {0, 0, 0, 255});
        check(0, 0, {0, 0, 0, 255});
    }
    std::filesystem::create_directories("artifacts");
    std::ofstream image{"artifacts/descriptor-heap.ppm", std::ios::binary};
    image.exceptions(std::ios::failbit | std::ios::badbit);
    image << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
    for (auto const &pixel : pixels)
        for (std::size_t channel = 0; channel < 3; ++channel)
            image.put(static_cast<char>(pixel[channel]));
    image.close();
    std::cout << "PASS: DescriptorHeap buffer/image/sampler allocation, capacity checks, two "
                 "recordings and pixel readback\n"
                 "Image: artifacts/descriptor-heap.ppm\n";
}
} // namespace

int main()
{
    try {
        lc1::VulkanLoader loader;
        lc1::Instance instance{
            loader, {vk::EXTDebugUtilsExtensionName}, {"VK_LAYER_KHRONOS_validation"}};
        instance.setup_debug_messenger();
        auto device = make_device(instance);
        run(device);
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
