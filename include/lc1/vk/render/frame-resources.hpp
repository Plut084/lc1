#pragma once

#include "lc1/scene/lights/light.hpp"
#include "lc1/vk/render/render-data.hpp"
#include "lc1/vk/resources/acceleration-structure.hpp"
#include "lc1/vk/resources/descriptor-heap-cache.hpp"
#include "lc1/vk/resources/descriptor-heap.hpp"
#include "lc1/vk/resources/gpu-image.hpp"
#include <memory>

#include <optional>
#include <utility>
#include <vector>

namespace lc1 {

// Per-slot ray-query resources, reused only after the slot fence.
struct RayQueryShadowFrameResources {
    std::optional<GpuBuffer> instances;
    std::optional<AccelerationStructure> scene;
    // Holds the TLAS address for a heap uniform-buffer lookup; see docs/ray-query-shadows.md.
    std::optional<GpuBuffer> scene_address;
    std::vector<vk::AccelerationStructureInstanceKHR> built_instances;
    std::optional<GpuImage> visibility;
    std::optional<GpuImage> surface;
    std::optional<GpuImage> depth;
};

// One independently reusable set of drawing resources. The caller must wait
// for its previous GPU use before recording, replacing or destroying it.
// Device and Renderer must outlive these resources.
struct FrameResources {
    FrameResources(std::unique_ptr<DescriptorHeap> heap, GpuBuffer camera, GpuBuffer lights,
                   GpuBuffer temporal, GpuBuffer tone_map)
        : descriptor_heap(std::move(heap)),
          descriptor_cache(std::make_unique<DescriptorHeapCache>(*descriptor_heap)),
          camera_buffer(std::move(camera)), lights_buffer(std::move(lights)),
          temporal_buffer(std::move(temporal)), tone_map_buffer(std::move(tone_map))
    {
    }

    ~FrameResources() = default;
    FrameResources(FrameResources const &) = delete;
    FrameResources &operator=(FrameResources const &) = delete;
    FrameResources(FrameResources &&) = default;
    // Preserve heap/cache identity and the lifetime of resources referenced by descriptors.
    FrameResources &operator=(FrameResources &&) = delete;

    RayQueryShadowFrameResources ray_query;
    // Main material pass attachments; created lazily from the output extent.
    std::optional<GpuImage> depth_image;
    std::optional<GpuImage> color_image;
    // Single-sampled linear HDR resolve, kept for tone mapping and debug readback.
    std::optional<GpuImage> hdr_image;

    struct PushData {
        HeapIndex frame_buffer_index;
    };

    struct FrameBufferObject {
        glm::vec4 clear_color; // TODO: For test only.

        HeapIndex camera_index; // view projection
        HeapIndex lights_index;
        HeapIndex object_data_index; // model
        HeapIndex material_index;
        HeapIndex shadow_visibility_index;
    };

    using CameraBufferObject = CameraData;
    using LightsBufferObject = LightsData;
    using ObjectBufferObject = ObjectData;

    struct MaterialBufferObject {
        glm::vec4 base_color_factor;
        float metallic_factor;
        float roughtness_factor;
        HeapIndex base_color_texture_index;
        HeapIndex base_color_texture_sampler_index;
        HeapIndex metallic_roughness_texture_index;
        HeapIndex metallic_roughness_texture_sampler_index;

        float normal_scale;
        HeapIndex normal_texture_index;
        HeapIndex normal_texture_sampler_index;

        float occlusion_strength;
        HeapIndex occlusion_texture_index;
        HeapIndex occlusion_texture_sampler_index;

        glm::vec3 emissive_factor;
        HeapIndex emissive_texture_index;
        HeapIndex emissive_texture_sampler_index;

        std::uint32_t flags;
    };

    struct TextureSlots {
        std::optional<HeapIndex> image;
        std::optional<HeapIndex> sampler;
    };

    // Per-slot heaps keep descriptor writes isolated from other in-flight frames.
    // Heap is immovable; its owning pointer preserves identity when this frame moves.
    std::unique_ptr<DescriptorHeap> descriptor_heap;
    std::unique_ptr<DescriptorHeapCache> descriptor_cache;
    PassData pass_data;
    std::optional<HeapIndex> surface_index;
    std::optional<HeapIndex> history_visibility_index;
    std::optional<HeapIndex> history_surface_index;
    std::optional<HeapIndex> resolved_visibility_index;
    std::optional<HeapIndex> hdr_index;

    HeapIndex image_index(std::optional<HeapIndex> &slot, GpuImage const &image)
    {
        if (slot)
            descriptor_heap->write_image(*slot, image);
        else
            slot = descriptor_heap->allocate_image(image);
        return *slot;
    }

    GpuBuffer camera_buffer;
    GpuBuffer lights_buffer;
    GpuBuffer temporal_buffer;
    GpuBuffer tone_map_buffer;
    HeapIndex camera_index{};
    HeapIndex lights_index{};
    std::optional<HeapIndex> shadow_visibility_index;
    std::vector<GpuBuffer> frame_buffers;
    std::vector<GpuBuffer> object_buffers;
    std::vector<GpuBuffer> material_buffers;
    std::vector<HeapIndex> frame_indices;
    std::vector<HeapIndex> object_indices;
    std::vector<HeapIndex> material_indices;
    std::vector<std::array<TextureSlots, 5>> material_textures;
};

static_assert(sizeof(FrameResources::FrameBufferObject) == 36);
static_assert(offsetof(FrameResources::MaterialBufferObject, emissive_factor) == 64);
static_assert(sizeof(FrameResources::MaterialBufferObject) == 88);

} // namespace lc1
