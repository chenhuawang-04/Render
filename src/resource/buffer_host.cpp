#include "vr/resource/buffer_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"

#include <cstddef>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vr::resource {

namespace {

[[nodiscard]] const char* VkResultName(VkResult result_) noexcept {
    switch (result_) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        default: return "VK_ERROR_UNKNOWN";
    }
}

void ThrowVk(const char* stage_, VkResult result_) {
    std::ostringstream oss;
    oss << stage_ << " failed: " << VkResultName(result_) << " (" << static_cast<int>(result_) << ")";
    throw std::runtime_error(oss.str());
}

void CheckVk(const char* stage_, VkResult result_) {
    if (result_ != VK_SUCCESS) {
        ThrowVk(stage_, result_);
    }
}

[[nodiscard]] Center::Memory::Vulkan::LifetimeHint InferLifetimeHint(
    const BufferCreateInfo& create_info_) noexcept {
    if ((create_info_.usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0U &&
        (create_info_.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U) {
        return Center::Memory::Vulkan::LifetimeHint::transient;
    }
    return Center::Memory::Vulkan::LifetimeHint::long_lived;
}

[[nodiscard]] Center::Memory::Vulkan::HostAccess InferHostAccess(
    const BufferCreateInfo& create_info_) noexcept {
    if ((create_info_.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0U) {
        return Center::Memory::Vulkan::HostAccess::none;
    }
    return Center::Memory::Vulkan::HostAccess::sequential_write;
}

[[nodiscard]] void* AddByteOffset(void* ptr_,
                                  VkDeviceSize offset_) {
    if (ptr_ == nullptr) {
        return nullptr;
    }
    if (offset_ > static_cast<VkDeviceSize>(std::numeric_limits<std::ptrdiff_t>::max())) {
        throw std::runtime_error("BufferHost::Map offset exceeds ptrdiff_t range");
    }
    auto* base = static_cast<std::byte*>(ptr_);
    return static_cast<void*>(base + static_cast<std::ptrdiff_t>(offset_));
}

} // namespace

BufferResource BufferHost::CreateBufferObject(VulkanContext& context_,
                                              const BufferCreateInfo& create_info_) {
    if (!context_.IsDeviceInitialized()) {
        throw std::runtime_error("BufferHost::CreateBufferObject requires initialized Vulkan device");
    }
    if (create_info_.size == 0U) {
        throw std::runtime_error("BufferHost::CreateBufferObject requires size > 0");
    }
    if (create_info_.usage == 0U) {
        throw std::runtime_error("BufferHost::CreateBufferObject requires non-zero usage flags");
    }
    if (create_info_.persistently_mapped &&
        (create_info_.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0U) {
        throw std::runtime_error(
            "BufferHost::CreateBufferObject persistently_mapped requires HOST_VISIBLE memory");
    }

    BufferResource resource{};

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.flags = create_info_.flags;
    buffer_info.size = create_info_.size;
    buffer_info.usage = create_info_.usage;
    buffer_info.sharingMode = create_info_.sharing_mode;
    if (create_info_.sharing_mode == VK_SHARING_MODE_CONCURRENT) {
        if (create_info_.queue_family_indices.empty()) {
            throw std::runtime_error(
                "BufferHost::CreateBufferObject concurrent sharing requires queue_family_indices");
        }
        buffer_info.queueFamilyIndexCount = static_cast<uint32_t>(create_info_.queue_family_indices.size());
        buffer_info.pQueueFamilyIndices = create_info_.queue_family_indices.data();
    } else {
        buffer_info.queueFamilyIndexCount = 0U;
        buffer_info.pQueueFamilyIndices = nullptr;
    }

    CheckVk("vkCreateBuffer",
            vkCreateBuffer(context_.Device(), &buffer_info, nullptr, &resource.buffer));
    resource.size = create_info_.size;
    resource.usage = create_info_.usage;
    return resource;
}

BufferResource BufferHost::CreateBuffer(VulkanContext& context_,
                                        const BufferCreateInfo& create_info_,
                                        GpuMemoryHost& gpu_memory_host_) {
    if (!gpu_memory_host_.IsInitialized()) {
        throw std::runtime_error("BufferHost::CreateBuffer requires initialized GpuMemoryHost");
    }
    BufferResource resource = CreateBufferObject(context_, create_info_);
    try {
        VkMemoryRequirements memory_requirements{};
        vkGetBufferMemoryRequirements(context_.Device(), resource.buffer, &memory_requirements);

        const bool host_visible_requested =
            (create_info_.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U;
        const bool persistent_map = host_visible_requested || create_info_.persistently_mapped;

        const auto allocation_slice = gpu_memory_host_.AllocateBufferMemory(
            memory_requirements,
            create_info_.memory_properties,
            create_info_.memory_properties,
            persistent_map,
            InferLifetimeHint(create_info_),
            InferHostAccess(create_info_),
            false,
            false,
            resource.buffer);
        BindAllocation(resource, gpu_memory_host_, allocation_slice, true, 0U);
    } catch (...) {
        DestroyBuffer(context_, resource);
        throw;
    }

    return resource;
}

void BufferHost::BindAllocation(BufferResource& resource_,
                                GpuMemoryHost& gpu_memory_host_,
                                const Center::Memory::Vulkan::Slice& allocation_slice_,
                                const bool owns_allocation_,
                                const VkDeviceSize resource_offset_) {
    if (resource_.buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("BufferHost::BindAllocation requires a created VkBuffer");
    }
    if (!gpu_memory_host_.IsInitialized()) {
        throw std::runtime_error("BufferHost::BindAllocation requires initialized GpuMemoryHost");
    }
    if (!allocation_slice_.valid()) {
        throw std::runtime_error("BufferHost::BindAllocation requires a valid allocation slice");
    }
    if (!gpu_memory_host_.BindBufferMemory(resource_.buffer, allocation_slice_, resource_offset_)) {
        throw std::runtime_error("BufferHost::BindAllocation bind failed");
    }

    resource_.allocation_slice = allocation_slice_;
    resource_.memory_host = &gpu_memory_host_;
    resource_.memory_type_index = allocation_slice_.memory_type_index;
    resource_.memory_properties = gpu_memory_host_.QueryMemoryProperties(resource_.memory_type_index);
    resource_.non_coherent_atom_size = 1U;
    resource_.mapped_ptr = AddByteOffset(allocation_slice_.mapped_ptr, resource_offset_);
    resource_.owns_allocation = owns_allocation_;

    const bool host_visible_requested =
        (resource_.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U;
    if (host_visible_requested && resource_.mapped_ptr == nullptr) {
        throw std::runtime_error(
            "BufferHost::BindAllocation host-visible allocation is not persistently mapped");
    }
}

void BufferHost::DestroyBuffer(VulkanContext& context_,
                               BufferResource& resource_) {
    const VkDevice device = context_.Device();
    if (device != VK_NULL_HANDLE && resource_.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, resource_.buffer, nullptr);
    }
    resource_.buffer = VK_NULL_HANDLE;

    if (resource_.owns_allocation &&
        resource_.memory_host != nullptr &&
        resource_.allocation_slice.valid()) {
        resource_.memory_host->Deallocate(resource_.allocation_slice);
    }

    resource_.allocation_slice = {};
    resource_.memory_host = nullptr;
    resource_.mapped_ptr = nullptr;
    resource_.size = 0U;
    resource_.usage = 0U;
    resource_.memory_properties = 0U;
    resource_.memory_type_index = 0U;
    resource_.non_coherent_atom_size = 1U;
    resource_.owns_allocation = false;
}

void* BufferHost::Map(VulkanContext& context_,
                      BufferResource& resource_,
                      VkDeviceSize offset_,
                      VkDeviceSize size_) {
    (void)context_;
    if (!IsHostVisible(resource_)) {
        throw std::runtime_error("BufferHost::Map requires host-visible memory");
    }
    if (!resource_.allocation_slice.valid() || resource_.memory_host == nullptr) {
        throw std::runtime_error("BufferHost::Map invalid MemoryCenter allocation slice");
    }
    if (offset_ > resource_.size) {
        throw std::runtime_error("BufferHost::Map offset out of range");
    }
    if (size_ != VK_WHOLE_SIZE && size_ > resource_.size - offset_) {
        throw std::runtime_error("BufferHost::Map size out of range");
    }
    if (resource_.allocation_slice.mapped_ptr == nullptr) {
        throw std::runtime_error("BufferHost::Map missing mapped pointer from MemoryCenter");
    }

    resource_.mapped_ptr = AddByteOffset(resource_.allocation_slice.mapped_ptr, offset_);
    return resource_.mapped_ptr;
}

void BufferHost::Unmap(VulkanContext& context_,
                       BufferResource& resource_) {
    (void)context_;
    resource_.mapped_ptr = resource_.allocation_slice.mapped_ptr;
}

void BufferHost::Flush(VulkanContext& context_,
                       const BufferResource& resource_,
                       VkDeviceSize offset_,
                       VkDeviceSize size_) {
    (void)context_;
    if (!IsHostVisible(resource_) || IsHostCoherent(resource_)) {
        return;
    }
    if (resource_.memory_host == nullptr || !resource_.allocation_slice.valid()) {
        throw std::runtime_error("BufferHost::Flush invalid MemoryCenter allocation slice");
    }
    if (offset_ > resource_.size) {
        throw std::runtime_error("BufferHost::Flush offset out of range");
    }

    VkDeviceSize flush_size = size_;
    if (flush_size == VK_WHOLE_SIZE) {
        flush_size = resource_.size - offset_;
    }
    if (flush_size > resource_.size - offset_) {
        throw std::runtime_error("BufferHost::Flush size out of range");
    }
    if (flush_size == 0U) {
        return;
    }

    if (!resource_.memory_host->FlushSlice(resource_.allocation_slice, offset_, flush_size)) {
        throw std::runtime_error("BufferHost::Flush failed in MemoryCenter path");
    }
}

void BufferHost::Invalidate(VulkanContext& context_,
                            const BufferResource& resource_,
                            VkDeviceSize offset_,
                            VkDeviceSize size_) {
    (void)context_;
    if (!IsHostVisible(resource_) || IsHostCoherent(resource_)) {
        return;
    }
    if (resource_.memory_host == nullptr || !resource_.allocation_slice.valid()) {
        throw std::runtime_error("BufferHost::Invalidate invalid MemoryCenter allocation slice");
    }
    if (offset_ > resource_.size) {
        throw std::runtime_error("BufferHost::Invalidate offset out of range");
    }

    VkDeviceSize invalidate_size = size_;
    if (invalidate_size == VK_WHOLE_SIZE) {
        invalidate_size = resource_.size - offset_;
    }
    if (invalidate_size > resource_.size - offset_) {
        throw std::runtime_error("BufferHost::Invalidate size out of range");
    }
    if (invalidate_size == 0U) {
        return;
    }

    if (!resource_.memory_host->InvalidateSlice(resource_.allocation_slice, offset_, invalidate_size)) {
        throw std::runtime_error("BufferHost::Invalidate failed in MemoryCenter path");
    }
}

bool BufferHost::IsHostVisible(const BufferResource& resource_) noexcept {
    return (resource_.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U;
}

bool BufferHost::IsHostCoherent(const BufferResource& resource_) noexcept {
    return (resource_.memory_properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U;
}

} // namespace vr::resource

