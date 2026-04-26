#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>
#include <stdexcept>

namespace vr::resource {

namespace {

[[nodiscard]] const char* AllocStatusName(Center::Memory::Vulkan::VulkanAllocStatus status_) noexcept {
    using Status = Center::Memory::Vulkan::VulkanAllocStatus;
    switch (status_) {
        case Status::ok: return "ok";
        case Status::invalid_size: return "invalid_size";
        case Status::invalid_alignment: return "invalid_alignment";
        case Status::invalid_memory_type_bits: return "invalid_memory_type_bits";
        case Status::invalid_memory_type_index: return "invalid_memory_type_index";
        case Status::backend_memory_type_mismatch: return "backend_memory_type_mismatch";
        case Status::backend_allocate_failed: return "backend_allocate_failed";
        case Status::backend_deallocate_failed: return "backend_deallocate_failed";
        case Status::bind_failed: return "bind_failed";
        case Status::map_failed: return "map_failed";
        case Status::unsupported_backend_feature: return "unsupported_backend_feature";
        case Status::range_overflow: return "range_overflow";
        case Status::out_of_capacity: return "out_of_capacity";
        case Status::not_found: return "not_found";
        case Status::debug_validation_failed: return "debug_validation_failed";
        default: return "unknown";
    }
}

[[nodiscard]] Center::Memory::Vulkan::DeviceType ToDeviceType(VkPhysicalDeviceType type_) noexcept {
    using DeviceType = Center::Memory::Vulkan::DeviceType;
    switch (type_) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return DeviceType::integrated_gpu;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return DeviceType::discrete_gpu;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return DeviceType::virtual_gpu;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return DeviceType::cpu;
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
        default:
            return DeviceType::unknown;
    }
}

[[nodiscard]] bool HasHostVisibleDeviceLocalOverlap(
    const VkPhysicalDeviceMemoryProperties& memory_properties_) noexcept {
    for (uint32_t i = 0U; i < memory_properties_.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags flags = memory_properties_.memoryTypes[i].propertyFlags;
        const bool host_visible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U;
        const bool device_local = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0U;
        if (host_visible && device_local) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Center::Memory::Vulkan::DeviceProfile BuildDeviceProfile(
    const VkPhysicalDeviceProperties& physical_properties_,
    const VkPhysicalDeviceMemoryProperties& memory_properties_,
    bool enable_dedicated_allocation_) noexcept {
    Center::Memory::Vulkan::DeviceProfile profile{};
    profile.device_type = ToDeviceType(physical_properties_.deviceType);
    profile.host_visible_device_local_overlap = HasHostVisibleDeviceLocalOverlap(memory_properties_);
    profile.is_uma = profile.host_visible_device_local_overlap;
    profile.supports_dedicated_allocation = enable_dedicated_allocation_;
    profile.buffer_image_granularity = static_cast<std::size_t>(
        std::max<VkDeviceSize>(1U, physical_properties_.limits.bufferImageGranularity));
    profile.non_coherent_atom_size = static_cast<std::size_t>(
        std::max<VkDeviceSize>(1U, physical_properties_.limits.nonCoherentAtomSize));
    profile.memory_type_count = memory_properties_.memoryTypeCount;
    profile.heap_count = memory_properties_.memoryHeapCount;
    return profile;
}

[[nodiscard]] std::string MakeAllocationFailureMessage(
    std::string_view stage_,
    const Center::Memory::Vulkan::AllocationFailure& failure_) {
    std::ostringstream oss;
    oss << stage_ << " failed: status=" << AllocStatusName(failure_.status)
        << ", size=" << failure_.size
        << ", alignment=" << failure_.alignment
        << ", memory_type_bits=0x" << std::hex << failure_.memory_type_bits << std::dec
        << ", memory_type_index=" << failure_.memory_type_index;
    return oss.str();
}

} // namespace

void GpuMemoryHost::Initialize(VulkanContext& context_,
                               const GpuMemoryHostCreateInfo& create_info_) {
    if (!context_.IsDeviceInitialized()) {
        throw std::runtime_error("GpuMemoryHost::Initialize requires initialized Vulkan device");
    }

    Shutdown();

    device = context_.Device();
    physical_device = context_.PhysicalDevice();

    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    VkPhysicalDeviceProperties physical_properties{};
    vkGetPhysicalDeviceProperties(physical_device, &physical_properties);
    non_coherent_atom_size = std::max<VkDeviceSize>(1U, physical_properties.limits.nonCoherentAtomSize);

    Center::Memory::VulkanNativeContext native_context{};
    native_context.device = device;
    native_context.memory_properties = memory_properties;
    native_context.non_coherent_atom_size = non_coherent_atom_size;
    native_context.enable_dedicated_allocation = create_info_.enable_dedicated_allocation;
    native_context.device_profile = BuildDeviceProfile(
        physical_properties,
        memory_properties,
        create_info_.enable_dedicated_allocation);

    Center::Memory::VulkanNativeBackend backend{native_context};
    NativeProvider provider{std::move(backend)};
    allocator.emplace(std::move(provider), create_info_.allocation_policy);
    initialized = true;
}

void GpuMemoryHost::Shutdown() {
    if (allocator.has_value()) {
        allocator.reset();
    }
    memory_properties = {};
    non_coherent_atom_size = 1U;
    physical_device = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
    initialized = false;
}

bool GpuMemoryHost::IsInitialized() const noexcept {
    return initialized;
}

Center::Memory::Vulkan::Slice GpuMemoryHost::AllocateAndBindBuffer(
    VkBuffer buffer_,
    const VkMemoryRequirements& requirements_,
    VkMemoryPropertyFlags required_properties_,
    VkMemoryPropertyFlags preferred_properties_,
    bool persistent_map_,
    Center::Memory::Vulkan::LifetimeHint lifetime_hint_,
    Center::Memory::Vulkan::HostAccess host_access_,
    bool dedicated_required_,
    bool dedicated_preferred_) {
    if (!initialized || !allocator.has_value()) {
        throw std::runtime_error("GpuMemoryHost::AllocateAndBindBuffer called before Initialize");
    }
    if (buffer_ == VK_NULL_HANDLE) {
        throw std::runtime_error("GpuMemoryHost::AllocateAndBindBuffer requires valid VkBuffer");
    }
    if (requirements_.size == 0U || requirements_.memoryTypeBits == 0U) {
        throw std::runtime_error("GpuMemoryHost::AllocateAndBindBuffer invalid memory requirements");
    }

    Center::Memory::Vulkan::AllocationRequest request{};
    request.resource = Center::Memory::VulkanNativeBackend::to_resource_handle(buffer_);
    request.size = ToSizeTChecked(requirements_.size, "GpuMemoryHost::AllocateAndBindBuffer(size)");
    request.alignment = ToSizeTChecked(
        std::max<VkDeviceSize>(1U, requirements_.alignment),
        "GpuMemoryHost::AllocateAndBindBuffer(alignment)");
    request.selector.memory_type_bits = requirements_.memoryTypeBits;
    request.selector.required_flags = static_cast<std::uint32_t>(required_properties_);
    request.selector.preferred_flags = static_cast<std::uint32_t>(preferred_properties_);
    request.persistent_map = persistent_map_;
    request.dedicated_required = dedicated_required_;
    request.dedicated_preferred = dedicated_preferred_;
    request.allocation_kind = Center::Memory::Vulkan::AllocationKind::buffer;
    request.lifetime_hint = lifetime_hint_;
    request.host_access = host_access_;
    request.buffer_image_granularity = static_cast<std::size_t>(
        std::max<VkDeviceSize>(1U, requirements_.alignment));

    Center::Memory::Vulkan::Slice slice = allocator->allocate(request);
    if (!slice.valid()) {
        throw std::runtime_error(MakeAllocationFailureMessage(
            "GpuMemoryHost::AllocateAndBindBuffer(allocate)",
            allocator->last_failure()));
    }

    if (!allocator->provider().bind_resource(request, slice, 0U)) {
        allocator->deallocate(slice);
        throw std::runtime_error(
            "GpuMemoryHost::AllocateAndBindBuffer(bind_resource) failed");
    }

    return slice;
}

Center::Memory::Vulkan::Slice GpuMemoryHost::AllocateAndBindImage(
    VkImage image_,
    const VkMemoryRequirements& requirements_,
    VkImageTiling tiling_,
    VkMemoryPropertyFlags required_properties_,
    VkMemoryPropertyFlags preferred_properties_,
    bool persistent_map_,
    Center::Memory::Vulkan::LifetimeHint lifetime_hint_,
    Center::Memory::Vulkan::HostAccess host_access_,
    bool dedicated_required_,
    bool dedicated_preferred_) {
    if (!initialized || !allocator.has_value()) {
        throw std::runtime_error("GpuMemoryHost::AllocateAndBindImage called before Initialize");
    }
    if (image_ == VK_NULL_HANDLE) {
        throw std::runtime_error("GpuMemoryHost::AllocateAndBindImage requires valid VkImage");
    }
    if (requirements_.size == 0U || requirements_.memoryTypeBits == 0U) {
        throw std::runtime_error("GpuMemoryHost::AllocateAndBindImage invalid memory requirements");
    }

    Center::Memory::Vulkan::AllocationRequest request{};
    request.resource = Center::Memory::VulkanNativeBackend::to_resource_handle(image_);
    request.size = ToSizeTChecked(requirements_.size, "GpuMemoryHost::AllocateAndBindImage(size)");
    request.alignment = ToSizeTChecked(
        std::max<VkDeviceSize>(1U, requirements_.alignment),
        "GpuMemoryHost::AllocateAndBindImage(alignment)");
    request.selector.memory_type_bits = requirements_.memoryTypeBits;
    request.selector.required_flags = static_cast<std::uint32_t>(required_properties_);
    request.selector.preferred_flags = static_cast<std::uint32_t>(preferred_properties_);
    request.persistent_map = persistent_map_;
    request.dedicated_required = dedicated_required_;
    request.dedicated_preferred = dedicated_preferred_;
    request.allocation_kind = ToAllocationKind(tiling_);
    request.lifetime_hint = lifetime_hint_;
    request.host_access = host_access_;
    request.buffer_image_granularity = static_cast<std::size_t>(
        std::max<VkDeviceSize>(1U, requirements_.alignment));

    Center::Memory::Vulkan::Slice slice = allocator->allocate(request);
    if (!slice.valid()) {
        throw std::runtime_error(MakeAllocationFailureMessage(
            "GpuMemoryHost::AllocateAndBindImage(allocate)",
            allocator->last_failure()));
    }

    if (!allocator->provider().bind_resource(request, slice, 0U)) {
        allocator->deallocate(slice);
        throw std::runtime_error(
            "GpuMemoryHost::AllocateAndBindImage(bind_resource) failed");
    }

    return slice;
}

void GpuMemoryHost::Deallocate(const Center::Memory::Vulkan::Slice& slice_) noexcept {
    if (!initialized || !allocator.has_value() || !slice_.valid()) {
        return;
    }
    allocator->deallocate(slice_);
}

bool GpuMemoryHost::FlushSlice(const Center::Memory::Vulkan::Slice& slice_,
                               VkDeviceSize offset_,
                               VkDeviceSize size_) noexcept {
    if (!initialized || !allocator.has_value()) {
        return false;
    }
    Center::Memory::Vulkan::MappedRange range{};
    if (!NormalizeMappedRange(slice_, offset_, size_, range)) {
        return false;
    }
    return allocator->provider().flush_range(range);
}

bool GpuMemoryHost::InvalidateSlice(const Center::Memory::Vulkan::Slice& slice_,
                                    VkDeviceSize offset_,
                                    VkDeviceSize size_) noexcept {
    if (!initialized || !allocator.has_value()) {
        return false;
    }
    Center::Memory::Vulkan::MappedRange range{};
    if (!NormalizeMappedRange(slice_, offset_, size_, range)) {
        return false;
    }
    return allocator->provider().invalidate_range(range);
}

void GpuMemoryHost::Trim() noexcept {
    if (!initialized || !allocator.has_value()) {
        return;
    }
    allocator->trim();
}

VkMemoryPropertyFlags GpuMemoryHost::QueryMemoryProperties(uint32_t memory_type_index_) const noexcept {
    if (!initialized || memory_type_index_ >= memory_properties.memoryTypeCount) {
        return 0U;
    }
    return memory_properties.memoryTypes[memory_type_index_].propertyFlags;
}

Center::Memory::Vulkan::AllocationKind GpuMemoryHost::ToAllocationKind(
    VkImageTiling tiling_) noexcept {
    switch (tiling_) {
        case VK_IMAGE_TILING_LINEAR:
            return Center::Memory::Vulkan::AllocationKind::image_linear;
        case VK_IMAGE_TILING_OPTIMAL:
#ifdef VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT
        case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT:
#endif
        default:
            return Center::Memory::Vulkan::AllocationKind::image_optimal;
    }
}

std::size_t GpuMemoryHost::ToSizeTChecked(VkDeviceSize value_,
                                          const char* stage_) {
    if (value_ > static_cast<VkDeviceSize>(std::numeric_limits<std::size_t>::max())) {
        std::ostringstream oss;
        oss << stage_ << " overflow: VkDeviceSize does not fit size_t";
        throw std::runtime_error(oss.str());
    }
    return static_cast<std::size_t>(value_);
}

bool GpuMemoryHost::NormalizeMappedRange(
    const Center::Memory::Vulkan::Slice& slice_,
    VkDeviceSize offset_,
    VkDeviceSize size_,
    Center::Memory::Vulkan::MappedRange& out_range_) noexcept {
    if (!slice_.valid()) {
        return false;
    }
    const VkDeviceSize slice_size = static_cast<VkDeviceSize>(slice_.size);
    if (offset_ > slice_size) {
        return false;
    }
    VkDeviceSize resolved_size = size_;
    if (resolved_size == VK_WHOLE_SIZE) {
        resolved_size = slice_size - offset_;
    }
    if (resolved_size > slice_size - offset_) {
        return false;
    }
    if (resolved_size == 0U) {
        return true;
    }
    const VkDeviceSize base_offset = static_cast<VkDeviceSize>(slice_.offset);
    if (base_offset > std::numeric_limits<VkDeviceSize>::max() - offset_) {
        return false;
    }
    const VkDeviceSize range_offset = base_offset + offset_;
    if (range_offset > static_cast<VkDeviceSize>(std::numeric_limits<std::size_t>::max()) ||
        resolved_size > static_cast<VkDeviceSize>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }

    out_range_.handle = slice_.handle;
    out_range_.memory_type_index = slice_.memory_type_index;
    out_range_.offset = static_cast<std::size_t>(range_offset);
    out_range_.size = static_cast<std::size_t>(resolved_size);
    return true;
}

} // namespace vr::resource
