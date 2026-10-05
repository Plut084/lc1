#include "lc1/vk/resources/descriptor-heap.hpp"
#include "lc1/vk/core/command-buffer.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace lc1 {

namespace {
constexpr auto mapped_heap = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
                             vma::AllocationCreateFlagBits::eMapped;

vk::DeviceSize align_descriptor(vk::DeviceSize offset, vk::DeviceSize stride)
{
    if (stride == 0)
        fail("DescriptorHeap: descriptor size is zero (check device support)");
    auto const padding = (stride - offset % stride) % stride;
    if (offset > std::numeric_limits<vk::DeviceSize>::max() - padding)
        fail("DescriptorHeap: offset overflow");
    return offset + padding;
}

vk::DeviceSize region_end(vk::DeviceSize offset, vk::DeviceSize capacity, vk::DeviceSize stride,
                          vk::DeviceSize limit)
{
    if (stride == 0 || offset > limit || capacity > (limit - offset) / stride)
        fail("DescriptorHeap: capacity exceeds the device heap limit");
    return offset + capacity * stride;
}

constexpr auto descriptor_heap_usage =
    vk::BufferUsageFlagBits2::eShaderDeviceAddress | vk::BufferUsageFlagBits2::eDescriptorHeapEXT;
} // namespace

SamplerHeap::SamplerHeap(Device const &device, vk::DeviceSize capacity,
                         vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties)
    // Not considering feature.minSamplerHeapReservedRangeWithEmbedded here.
    : device_(&device), reserved_size_(descriptor_heap_properties.minSamplerHeapReservedRange),
      offset_(align_descriptor(reserved_size_, descriptor_heap_properties.samplerDescriptorSize)),
      stride_(descriptor_heap_properties.samplerDescriptorSize), capacity_(capacity),
      buffer_(device,
              std::max(descriptor_heap_properties.samplerHeapAlignment,
                       region_end(offset_, capacity, stride_,
                                  descriptor_heap_properties.maxSamplerHeapSize)),
              descriptor_heap_usage, mapped_heap, descriptor_heap_properties.samplerHeapAlignment),
      size_(0)
{
}

SamplerHeap::ShaderIndex SamplerHeap::allocate()
{
    if (size_ == capacity_)
        fail("DescriptorHeap: sampler heap is full");

    vk::SamplerCreateInfo sampler_ci{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .anisotropyEnable = vk::True,
        .maxAnisotropy = device_->raii_physical().getProperties().limits.maxSamplerAnisotropy,
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eAlways,
        .borderColor = vk::BorderColor::eIntOpaqueBlack,
        .unnormalizedCoordinates = vk::False,
    };

    vk::HostAddressRangeEXT sampler_har{
        .address = static_cast<std::byte *>(buffer_.mapped_address()) + offset_ + stride_ * size_,
        .size = stride_,
    };

    device_->raii().writeSamplerDescriptorsEXT(sampler_ci, sampler_har);

    buffer_.flush(offset_ + stride_ * size_, stride_);
    return offset_ / stride_ + size_++;
}

ResourceHeap::ResourceHeap(Device const &device, vk::DeviceSize image_capacity,
                           vk::DeviceSize buffer_capacity,
                           vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties)
    : device_(&device), reserved_size_(descriptor_heap_properties.minResourceHeapReservedRange),
      image_offset_(
          align_descriptor(reserved_size_, descriptor_heap_properties.imageDescriptorSize)),
      image_stride_(descriptor_heap_properties.imageDescriptorSize),
      image_capacity_(image_capacity),
      buffer_offset_(align_descriptor(region_end(image_offset_, image_capacity, image_stride_,
                                                 descriptor_heap_properties.maxResourceHeapSize),
                                      descriptor_heap_properties.bufferDescriptorSize)),
      buffer_stride_(descriptor_heap_properties.bufferDescriptorSize),
      buffer_capacity_(buffer_capacity),
      buffer_(device,
              std::max(descriptor_heap_properties.resourceHeapAlignment,
                       region_end(buffer_offset_, buffer_capacity, buffer_stride_,
                                  descriptor_heap_properties.maxResourceHeapSize)),
              descriptor_heap_usage, mapped_heap, descriptor_heap_properties.resourceHeapAlignment),
      image_size_(0), buffer_size_(0)
{
}

ResourceHeap::ShaderIndex ResourceHeap::allocate_image(GpuImage const &image)
{
    if (image_size_ == image_capacity_)
        fail("DescriptorHeap: image heap is full");

    auto view_ci = image.view_create_info();
    vk::ImageDescriptorInfoEXT image_info{
        .pView = &view_ci,
        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    vk::ResourceDescriptorInfoEXT resource_info{
        .type = vk::DescriptorType::eSampledImage,
        .data = vk::ResourceDescriptorDataEXT{&image_info},
    };

    vk::HostAddressRangeEXT sampler_har{
        .address = static_cast<std::byte *>(buffer_.mapped_address()) + image_offset_ +
                   image_stride_ * image_size_,
        .size = image_stride_,
    };

    device_->raii().writeResourceDescriptorsEXT(resource_info, sampler_har);

    buffer_.flush(image_offset_ + image_stride_ * image_size_, image_stride_);
    return image_offset_ / image_stride_ + image_size_++;
}

ResourceHeap::ShaderIndex ResourceHeap::allocate_buffer(GpuBuffer const &buffer)
{
    if (buffer_size_ == buffer_capacity_)
        fail("DescriptorHeap: buffer heap is full");

    vk::DeviceAddressRangeEXT buffer_range{
        .address = buffer.device_address(),
        .size = buffer.size(),
    };
    vk::ResourceDescriptorInfoEXT resource_info{
        .type = vk::DescriptorType::eUniformBuffer,
        .data = vk::ResourceDescriptorDataEXT{&buffer_range},
    };

    vk::HostAddressRangeEXT sampler_har{
        .address = static_cast<std::byte *>(buffer_.mapped_address()) + buffer_offset_ +
                   buffer_stride_ * buffer_size_,
        .size = buffer_stride_,
    };

    device_->raii().writeResourceDescriptorsEXT(resource_info, sampler_har);

    buffer_.flush(buffer_offset_ + buffer_stride_ * buffer_size_, buffer_stride_);
    return buffer_offset_ / buffer_stride_ + buffer_size_++;
}

DescriptorHeap::DescriptorHeap(Device const &device, vk::DeviceSize sampler_capacity,
                               vk::DeviceSize image_capacity, vk::DeviceSize buffer_capacity)
    : device_(&device), descriptor_heap_properties_(
                            device.raii_physical()
                                .getProperties2<vk::PhysicalDeviceProperties2,
                                                vk::PhysicalDeviceDescriptorHeapPropertiesEXT>()
                                .get<vk::PhysicalDeviceDescriptorHeapPropertiesEXT>()),
      sampler_heap_(device, sampler_capacity, descriptor_heap_properties_),
      resource_heap_(device, image_capacity, buffer_capacity, descriptor_heap_properties_)
{
}

void DescriptorHeap::probe() const
{
    auto const &physical = device_->raii_physical();
    auto const extensions = physical.enumerateDeviceExtensionProperties();
    auto const supports_extension = [&](std::string_view name) {
        return std::ranges::any_of(extensions, [&](auto const &extension) {
            return std::string_view{extension.extensionName.data()} == name;
        });
    };
    auto const log = [](std::string_view name, auto value, std::string_view unit = {}) {
        spdlog::info("[DescriptorHeap] {:<48} = {}{}", name, value, unit);
    };

    auto const identity =
        physical
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
    auto const &device_properties = identity.get<vk::PhysicalDeviceProperties2>().properties;
    auto const &driver = identity.get<vk::PhysicalDeviceDriverProperties>();
    spdlog::info("[DescriptorHeap] device = {} ({}, Vulkan {}, driver {} {})",
                 device_properties.deviceName.data(), vk::to_string(device_properties.deviceType),
                 version_string(device_properties.apiVersion), driver.driverName.data(),
                 driver.driverInfo.data());
    spdlog::info(
        "[DescriptorHeap] features below report physical-device support, not logical-device "
        "enablement; region used bytes exclude reserved ranges");

    auto const log_heap = [](std::string_view name, GpuBuffer const &buffer,
                             vk::DeviceSize reserved) {
        spdlog::info("[DescriptorHeap] {}: address=0x{:x}, capacity={} bytes, "
                     "reserved=[0, {}) bytes",
                     name, buffer.device_address(), buffer.size(), reserved);
        if (reserved > buffer.size()) {
            spdlog::warn("[DescriptorHeap] {}: reserved range exceeds heap capacity", name);
        }
    };
    auto const log_region = [](std::string_view name, GpuBuffer const &buffer,
                               vk::DeviceSize offset, vk::DeviceSize end, vk::DeviceSize used,
                               vk::DeviceSize stride) {
        spdlog::info("[DescriptorHeap] {}: range=[{}, {}) bytes, stride={} bytes, used={} bytes",
                     name, offset, end, stride, used);
        if (offset > end || end > buffer.size() || used > end - offset) {
            spdlog::warn("[DescriptorHeap] {}: invalid accounting (expected offset <= end <= "
                         "heap capacity and used <= end - offset); capacity/free bytes unavailable",
                         name);
            return;
        }
        auto const capacity = end - offset;
        spdlog::info("[DescriptorHeap] {}: capacity={} bytes, free={} bytes", name, capacity,
                     capacity - used);
    };
    log_heap("sampler heap", sampler_heap_.buffer_, sampler_heap_.reserved_size_);
    log_region("sampler region", sampler_heap_.buffer_, sampler_heap_.offset_,
               sampler_heap_.buffer_.size(), sampler_heap_.size_ * sampler_heap_.stride_,
               sampler_heap_.stride_);
    log_heap("resource heap", resource_heap_.buffer_, resource_heap_.reserved_size_);
    log_region("image region", resource_heap_.buffer_, resource_heap_.image_offset_,
               resource_heap_.buffer_offset_,
               resource_heap_.image_size_ * resource_heap_.image_stride_,
               resource_heap_.image_stride_);
    log_region("buffer region", resource_heap_.buffer_, resource_heap_.buffer_offset_,
               resource_heap_.buffer_.size(),
               resource_heap_.buffer_size_ * resource_heap_.buffer_stride_,
               resource_heap_.buffer_stride_);

    bool const supports_heap = supports_extension(vk::EXTDescriptorHeapExtensionName);
    bool const supports_untyped = supports_extension(vk::KHRShaderUntypedPointersExtensionName);
    log(vk::EXTDescriptorHeapExtensionName, supports_heap);
    log(vk::KHRShaderUntypedPointersExtensionName, supports_untyped);
    if (supports_untyped) {
        auto const features =
            physical.getFeatures2<vk::PhysicalDeviceFeatures2,
                                  vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>();
        log("shaderUntypedPointers",
            features.get<vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>()
                    .shaderUntypedPointers == vk::True);
    }
    else {
        spdlog::info(
            "[DescriptorHeap] shaderUntypedPointers = unavailable (extension unsupported)");
    }
    if (!supports_heap) {
        spdlog::warn("[DescriptorHeap] heap features, properties and exact descriptor sizes are "
                     "unavailable (VK_EXT_descriptor_heap unsupported)");
        return;
    }

    auto const features = physical.getFeatures2<vk::PhysicalDeviceFeatures2,
                                                vk::PhysicalDeviceDescriptorHeapFeaturesEXT>();
    auto const &heap_features = features.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>();
    log("descriptorHeap", heap_features.descriptorHeap == vk::True);
    log("descriptorHeapCaptureReplay", heap_features.descriptorHeapCaptureReplay == vk::True);
    log("shaderInt64",
        features.get<vk::PhysicalDeviceFeatures2>().features.shaderInt64 == vk::True);
    log("samplerAnisotropy",
        features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy == vk::True);

    auto const properties =
        physical.getProperties2<vk::PhysicalDeviceProperties2,
                                vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
    auto const &heap = properties.get<vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
    log("samplerHeapAlignment", heap.samplerHeapAlignment, " bytes");
    log("resourceHeapAlignment", heap.resourceHeapAlignment, " bytes");
    log("maxSamplerHeapSize", heap.maxSamplerHeapSize, " bytes");
    log("maxResourceHeapSize", heap.maxResourceHeapSize, " bytes");
    log("minSamplerHeapReservedRange", heap.minSamplerHeapReservedRange, " bytes");
    log("minSamplerHeapReservedRangeWithEmbedded", heap.minSamplerHeapReservedRangeWithEmbedded,
        " bytes");
    log("minResourceHeapReservedRange", heap.minResourceHeapReservedRange, " bytes");
    log("samplerDescriptorSize", heap.samplerDescriptorSize, " bytes");
    log("imageDescriptorSize", heap.imageDescriptorSize, " bytes");
    log("bufferDescriptorSize", heap.bufferDescriptorSize, " bytes");
    log("samplerDescriptorAlignment", heap.samplerDescriptorAlignment, " bytes");
    log("imageDescriptorAlignment", heap.imageDescriptorAlignment, " bytes");
    log("bufferDescriptorAlignment", heap.bufferDescriptorAlignment, " bytes");
    log("maxPushDataSize", heap.maxPushDataSize, " bytes");
    log("imageCaptureReplayOpaqueDataSize", heap.imageCaptureReplayOpaqueDataSize, " bytes");
    log("maxDescriptorHeapEmbeddedSamplers", heap.maxDescriptorHeapEmbeddedSamplers);
    log("samplerYcbcrConversionCount", heap.samplerYcbcrConversionCount);
    log("sparseDescriptorHeaps", heap.sparseDescriptorHeaps == vk::True);
    log("protectedDescriptorHeaps", heap.protectedDescriptorHeaps == vk::True);

    // Only types accepted by vkGetPhysicalDeviceDescriptorSizeEXT; optional types are gated by
    // their device extensions. Combined image samplers and dynamic buffers are not heap types.
    struct DescriptorType {
        vk::DescriptorType type;
        std::string_view extension;
    };

    constexpr std::array descriptor_types{
        DescriptorType{vk::DescriptorType::eSampler, {}},
        DescriptorType{vk::DescriptorType::eUniformBuffer, {}},
        DescriptorType{vk::DescriptorType::eStorageBuffer, {}},
        DescriptorType{vk::DescriptorType::eSampledImage, {}},
        DescriptorType{vk::DescriptorType::eStorageImage, {}},
        DescriptorType{vk::DescriptorType::eUniformTexelBuffer, {}},
        DescriptorType{vk::DescriptorType::eStorageTexelBuffer, {}},
        DescriptorType{vk::DescriptorType::eInputAttachment, {}},
        DescriptorType{vk::DescriptorType::eAccelerationStructureKHR,
                       vk::KHRAccelerationStructureExtensionName},
        // Include the legacy extension in the inventory without enabling it.
        DescriptorType{vk::DescriptorType::eAccelerationStructureNV,
                       VK_NV_RAY_TRACING_EXTENSION_NAME},
        DescriptorType{vk::DescriptorType::ePartitionedAccelerationStructureNV,
                       vk::NVPartitionedAccelerationStructureExtensionName},
        DescriptorType{vk::DescriptorType::eSampleWeightImageQCOM,
                       vk::QCOMImageProcessingExtensionName},
        DescriptorType{vk::DescriptorType::eBlockMatchImageQCOM,
                       vk::QCOMImageProcessingExtensionName},
        DescriptorType{vk::DescriptorType::eTensorARM, vk::ARMTensorsExtensionName},
    };
    for (auto const &[type, extension] : descriptor_types) {
        if (extension.empty() || supports_extension(extension)) {
            spdlog::info("[DescriptorHeap] exact descriptor size {} = {} bytes",
                         vk::to_string(type), physical.getDescriptorSizeEXT(type));
        }
        else {
            spdlog::info("[DescriptorHeap] exact descriptor size {} = unavailable ({} unsupported)",
                         vk::to_string(type), extension);
        }
    }

    if (supports_extension(vk::ARMTensorsExtensionName)) {
        auto const tensor_properties =
            physical.getProperties2<vk::PhysicalDeviceProperties2,
                                    vk::PhysicalDeviceDescriptorHeapTensorPropertiesARM>();
        auto const &tensor =
            tensor_properties.get<vk::PhysicalDeviceDescriptorHeapTensorPropertiesARM>();
        log("tensorDescriptorSize", tensor.tensorDescriptorSize, " bytes");
        log("tensorDescriptorAlignment", tensor.tensorDescriptorAlignment, " bytes");
        log("tensorCaptureReplayOpaqueDataSize", tensor.tensorCaptureReplayOpaqueDataSize,
            " bytes");
    }
    else {
        spdlog::info(
            "[DescriptorHeap] tensorDescriptorSize, tensorDescriptorAlignment, "
            "tensorCaptureReplayOpaqueDataSize = unavailable (VK_ARM_tensors unsupported)");
    }
}

void DescriptorHeap::bind_to_command_buffer(CommandBuffer &command_buffer)
{
    command_buffer.raii().bindSamplerHeapEXT(vk::BindHeapInfoEXT{
        .heapRange =
            vk::DeviceAddressRangeEXT{
                .address = sampler_heap_.buffer_.device_address(),
                .size = sampler_heap_.buffer_.size(),
            },
        .reservedRangeOffset = 0,
        .reservedRangeSize = sampler_heap_.reserved_size_,
    });

    command_buffer.raii().bindResourceHeapEXT(vk::BindHeapInfoEXT{
        .heapRange =
            vk::DeviceAddressRangeEXT{
                .address = resource_heap_.buffer_.device_address(),
                .size = resource_heap_.buffer_.size(),
            },
        .reservedRangeOffset = 0,
        .reservedRangeSize = resource_heap_.reserved_size_,
    });
}

std::size_t DescriptorHeap::allocate_sampler()
{
    return sampler_heap_.allocate();
}

std::size_t DescriptorHeap::allocate_image(GpuImage const &image)
{
    return resource_heap_.allocate_image(image);
}

std::size_t DescriptorHeap::allocate_buffer(GpuBuffer const &buffer)
{
    return resource_heap_.allocate_buffer(buffer);
}

} // namespace lc1
