#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "Center/Memory/Vulkan/Types.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>

namespace vr::resource {

class GpuMemoryHost;

template<typename T>
using ResourceMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct BufferCreateInfo {
    VkDeviceSize size = 0U;
    VkBufferUsageFlags usage = 0U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferCreateFlags flags = 0U;
    VkSharingMode sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    ResourceMcVector<uint32_t> queue_family_indices{};
    bool persistently_mapped = false;
};

struct BufferResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    Center::Memory::Vulkan::Slice allocation_slice{};
    GpuMemoryHost* memory_host = nullptr;
    VkDeviceSize size = 0U;
    VkBufferUsageFlags usage = 0U;
    VkMemoryPropertyFlags memory_properties = 0U;
    uint32_t memory_type_index = 0U;
    VkDeviceSize non_coherent_atom_size = 1U;
    void* mapped_ptr = nullptr;
    bool owns_allocation = false;
};

class BufferHost final {
public:
    BufferHost() = delete;

    [[nodiscard]] static BufferResource CreateBufferObject(VulkanContext& context_,
                                                           const BufferCreateInfo& create_info_);

    [[nodiscard]] static BufferResource CreateBuffer(VulkanContext& context_,
                                                     const BufferCreateInfo& create_info_,
                                                     GpuMemoryHost& gpu_memory_host_);

    static void BindAllocation(BufferResource& resource_,
                               GpuMemoryHost& gpu_memory_host_,
                               const Center::Memory::Vulkan::Slice& allocation_slice_,
                               bool owns_allocation_ = true,
                               VkDeviceSize resource_offset_ = 0U);

    static void DestroyBuffer(VulkanContext& context_,
                              BufferResource& resource_);

    [[nodiscard]] static void* Map(VulkanContext& context_,
                                   BufferResource& resource_,
                                   VkDeviceSize offset_ = 0U,
                                   VkDeviceSize size_ = VK_WHOLE_SIZE);

    static void Unmap(VulkanContext& context_,
                      BufferResource& resource_);

    static void Flush(VulkanContext& context_,
                      const BufferResource& resource_,
                      VkDeviceSize offset_ = 0U,
                      VkDeviceSize size_ = VK_WHOLE_SIZE);

    static void Invalidate(VulkanContext& context_,
                           const BufferResource& resource_,
                           VkDeviceSize offset_ = 0U,
                           VkDeviceSize size_ = VK_WHOLE_SIZE);

    [[nodiscard]] static bool IsHostVisible(const BufferResource& resource_) noexcept;
    [[nodiscard]] static bool IsHostCoherent(const BufferResource& resource_) noexcept;
};

} // namespace vr::resource

