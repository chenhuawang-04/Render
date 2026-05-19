#pragma once

#include "Center/Memory/Adaptor/VulkanBuddyAdaptor.hpp"
#include "Center/Memory/Provider/VulkanNativeBackend.hpp"
#include "Center/Memory/Vulkan/Types.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <optional>
#include <vulkan/vulkan.h>

namespace vr::resource {

struct GpuMemoryHostCreateInfo {
    bool enable_dedicated_allocation = true;
    Center::Memory::Vulkan::AllocationPolicy allocation_policy =
        Center::Memory::Vulkan::AllocationPolicy::throughput_first();
};

class GpuMemoryHost final {
public:
    GpuMemoryHost() = default;
    ~GpuMemoryHost() = default;

    GpuMemoryHost(const GpuMemoryHost&) = delete;
    GpuMemoryHost& operator=(const GpuMemoryHost&) = delete;

    GpuMemoryHost(GpuMemoryHost&&) = delete;
    GpuMemoryHost& operator=(GpuMemoryHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const GpuMemoryHostCreateInfo& create_info_ = {});

    void Shutdown();

    [[nodiscard]] bool IsInitialized() const noexcept;

    [[nodiscard]] Center::Memory::Vulkan::Slice AllocateAndBindBuffer(
        VkBuffer buffer_,
        const VkMemoryRequirements& requirements_,
        VkMemoryPropertyFlags required_properties_,
        VkMemoryPropertyFlags preferred_properties_,
        bool persistent_map_,
        Center::Memory::Vulkan::LifetimeHint lifetime_hint_ = Center::Memory::Vulkan::LifetimeHint::long_lived,
        Center::Memory::Vulkan::HostAccess host_access_ = Center::Memory::Vulkan::HostAccess::none,
        bool dedicated_required_ = false,
        bool dedicated_preferred_ = false);

    [[nodiscard]] Center::Memory::Vulkan::Slice AllocateBufferMemory(
        const VkMemoryRequirements& requirements_,
        VkMemoryPropertyFlags required_properties_,
        VkMemoryPropertyFlags preferred_properties_,
        bool persistent_map_,
        Center::Memory::Vulkan::LifetimeHint lifetime_hint_ = Center::Memory::Vulkan::LifetimeHint::long_lived,
        Center::Memory::Vulkan::HostAccess host_access_ = Center::Memory::Vulkan::HostAccess::none,
        bool dedicated_required_ = false,
        bool dedicated_preferred_ = false,
        VkBuffer dedicated_buffer_ = VK_NULL_HANDLE);

    [[nodiscard]] bool BindBufferMemory(
        VkBuffer buffer_,
        const Center::Memory::Vulkan::Slice& slice_,
        VkDeviceSize resource_offset_ = 0U) noexcept;

    [[nodiscard]] Center::Memory::Vulkan::Slice AllocateAndBindImage(
        VkImage image_,
        const VkMemoryRequirements& requirements_,
        VkImageTiling tiling_,
        VkMemoryPropertyFlags required_properties_,
        VkMemoryPropertyFlags preferred_properties_,
        bool persistent_map_,
        Center::Memory::Vulkan::LifetimeHint lifetime_hint_ = Center::Memory::Vulkan::LifetimeHint::long_lived,
        Center::Memory::Vulkan::HostAccess host_access_ = Center::Memory::Vulkan::HostAccess::none,
        bool dedicated_required_ = false,
        bool dedicated_preferred_ = true);

    [[nodiscard]] Center::Memory::Vulkan::Slice AllocateImageMemory(
        const VkMemoryRequirements& requirements_,
        VkImageTiling tiling_,
        VkMemoryPropertyFlags required_properties_,
        VkMemoryPropertyFlags preferred_properties_,
        bool persistent_map_,
        Center::Memory::Vulkan::LifetimeHint lifetime_hint_ = Center::Memory::Vulkan::LifetimeHint::long_lived,
        Center::Memory::Vulkan::HostAccess host_access_ = Center::Memory::Vulkan::HostAccess::none,
        bool dedicated_required_ = false,
        bool dedicated_preferred_ = true,
        VkImage dedicated_image_ = VK_NULL_HANDLE);

    [[nodiscard]] bool BindImageMemory(
        VkImage image_,
        const Center::Memory::Vulkan::Slice& slice_,
        VkImageTiling tiling_ = VK_IMAGE_TILING_OPTIMAL,
        VkDeviceSize resource_offset_ = 0U) noexcept;

    void Deallocate(const Center::Memory::Vulkan::Slice& slice_) noexcept;

    [[nodiscard]] bool FlushSlice(const Center::Memory::Vulkan::Slice& slice_,
                                  VkDeviceSize offset_ = 0U,
                                  VkDeviceSize size_ = VK_WHOLE_SIZE) noexcept;

    [[nodiscard]] bool InvalidateSlice(const Center::Memory::Vulkan::Slice& slice_,
                                       VkDeviceSize offset_ = 0U,
                                       VkDeviceSize size_ = VK_WHOLE_SIZE) noexcept;

    void Trim() noexcept;

    [[nodiscard]] VkMemoryPropertyFlags QueryMemoryProperties(uint32_t memory_type_index_) const noexcept;

private:
    [[nodiscard]] static Center::Memory::Vulkan::AllocationKind ToAllocationKind(
        VkImageTiling tiling_) noexcept;

    [[nodiscard]] static std::size_t ToSizeTChecked(VkDeviceSize value_,
                                                    const char* stage_);

    [[nodiscard]] static bool NormalizeMappedRange(
        const Center::Memory::Vulkan::Slice& slice_,
        VkDeviceSize offset_,
        VkDeviceSize size_,
        Center::Memory::Vulkan::MappedRange& out_range_) noexcept;

private:
    using NativeProvider = Center::Memory::VulkanNativeProvider;
    using AllocationAdaptor = Center::Memory::VulkanBuddyAdaptor<NativeProvider, (64u * 1024u * 1024u), 256u>;

    std::optional<AllocationAdaptor> allocator{};
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkDeviceSize non_coherent_atom_size = 1U;
    bool initialized = false;
};

} // namespace vr::resource

