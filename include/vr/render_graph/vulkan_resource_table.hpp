#pragma once

#include "vr/render/render_target_desc.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render_graph/compiled_render_graph.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vr::render_graph {

struct ImportedBufferBinding final {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize size_bytes = 0U;
    VkBufferUsageFlags usage = 0U;
};

struct PhysicalTextureRecord final {
    ResourceHandle logical{};
    std::string debug_name{};
    ResourceLifetime lifetime = ResourceLifetime::transient;
    TextureDesc desc{};
    render::RenderTargetHandle render_target = render::invalid_render_target_handle;
    bool imported = false;
};

struct PhysicalBufferRecord final {
    ResourceHandle logical{};
    std::string debug_name{};
    ResourceLifetime lifetime = ResourceLifetime::transient;
    BufferDesc desc{};
    resource::BufferResource owned_resource{};
    ImportedBufferBinding imported_buffer{};
    std::uint32_t alias_page_index = invalid_render_graph_index;
    bool aliased = false;
    bool imported = false;
};

struct TransientBufferPageRecord final {
    std::uint32_t page_index = invalid_render_graph_index;
    Center::Memory::Vulkan::Slice allocation_slice{};
    resource::GpuMemoryHost* memory_host = nullptr;
    VkDeviceSize size_bytes = 0U;
    VkDeviceSize alignment_bytes = 1U;
    std::uint32_t memory_type_bits = 0U;
    std::vector<ResourceHandle> resources{};
};

struct VulkanResourceTableStats final {
    std::uint32_t imported_texture_count = 0U;
    std::uint32_t imported_buffer_count = 0U;
    std::uint32_t persistent_texture_count = 0U;
    std::uint32_t persistent_buffer_count = 0U;
    std::uint32_t transient_texture_count = 0U;
    std::uint32_t transient_buffer_count = 0U;
    std::uint32_t transient_buffer_page_count = 0U;
    std::uint32_t transient_aliased_buffer_count = 0U;
};

class VulkanResourceTable final {
public:
    void BeginFrame(VulkanContext& device_,
                    render::RenderTargetHost& render_target_host_,
                    std::uint64_t last_submitted_value_,
                    std::uint64_t completed_submit_value_);

    void RegisterImportedTexture(ResourceHandle logical_,
                                 render::RenderTargetHandle render_target_) noexcept;

    void RegisterImportedBuffer(ResourceHandle logical_,
                                const ImportedBufferBinding& imported_buffer_) noexcept;

    void Resolve(VulkanContext& device_,
                 resource::GpuMemoryHost& gpu_memory_host_,
                 render::RenderTargetHost& render_target_host_,
                 const CompiledRenderGraph& compiled_graph_,
                 std::uint64_t last_submitted_value_,
                 std::uint64_t completed_submit_value_);

    void Shutdown(VulkanContext& device_,
                  render::RenderTargetHost& render_target_host_,
                  std::uint64_t last_submitted_value_,
                  std::uint64_t completed_submit_value_) noexcept;

    [[nodiscard]] const PhysicalTextureRecord* FindTexture(ResourceHandle logical_) const noexcept;
    [[nodiscard]] const PhysicalBufferRecord* FindBuffer(ResourceHandle logical_) const noexcept;
    [[nodiscard]] const std::vector<PhysicalTextureRecord>& Textures() const noexcept;
    [[nodiscard]] const std::vector<PhysicalBufferRecord>& Buffers() const noexcept;
    [[nodiscard]] const VulkanResourceTableStats& Stats() const noexcept;

    [[nodiscard]] static render::RenderTargetDesc BuildRenderTargetDesc(
        const TextureDesc& desc_) noexcept;
    [[nodiscard]] static resource::BufferCreateInfo BuildBufferCreateInfo(
        const BufferDesc& desc_) noexcept;
    [[nodiscard]] static resource::ImageCreateInfo BuildImageCreateInfo(
        const TextureDesc& desc_) noexcept;

private:
    void DestroyTransientRecords(VulkanContext& device_,
                                 render::RenderTargetHost& render_target_host_,
                                 std::uint64_t last_submitted_value_,
                                 std::uint64_t completed_submit_value_) noexcept;
    void DestroyAllRecords(VulkanContext& device_,
                           render::RenderTargetHost& render_target_host_,
                           std::uint64_t last_submitted_value_,
                           std::uint64_t completed_submit_value_) noexcept;
    void RefreshStats() noexcept;

private:
    std::vector<PhysicalTextureRecord> textures{};
    std::vector<PhysicalBufferRecord> buffers{};
    std::vector<TransientBufferPageRecord> transient_buffer_pages{};
    std::vector<render::RenderTargetHandle> imported_textures{};
    std::vector<ImportedBufferBinding> imported_buffers{};
    VulkanResourceTableStats stats{};
};

} // namespace vr::render_graph
