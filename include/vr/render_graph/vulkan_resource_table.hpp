#pragma once

#include "vr/render/render_target_desc.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/retire_bus.hpp"
#include "vr/render_graph/compiled_render_graph.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <stdexcept>
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
    resource::ImageResource owned_resource{};
    std::uint32_t alias_page_index = invalid_render_graph_index;
    bool aliased = false;
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

struct TransientImagePageRecord final {
    std::uint32_t page_index = invalid_render_graph_index;
    Center::Memory::Vulkan::Slice allocation_slice{};
    resource::GpuMemoryHost* memory_host = nullptr;
    VkDeviceSize size_bytes = 0U;
    VkDeviceSize alignment_bytes = 1U;
    std::uint32_t memory_type_bits = 0U;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    std::vector<ResourceHandle> resources{};
};

struct RetiredTransientImagePayload final {
    resource::ImageResource owned_resource{};
};

struct VulkanResourceTableStats final {
    std::uint32_t imported_texture_count = 0U;
    std::uint32_t imported_buffer_count = 0U;
    std::uint32_t persistent_texture_count = 0U;
    std::uint32_t persistent_buffer_count = 0U;
    std::uint32_t transient_texture_count = 0U;
    std::uint32_t transient_buffer_count = 0U;
    std::uint32_t transient_buffer_page_count = 0U;
    std::uint32_t transient_image_page_count = 0U;
    std::uint32_t transient_aliased_buffer_count = 0U;
    std::uint32_t transient_aliased_texture_count = 0U;
};

struct RenderGraphExecutorCapabilities final {
    bool supports_queue_transfer_batches = false;
};

enum class VulkanResourceResolveErrorCode : std::uint8_t {
    unsupported_cross_queue_alias = 1U,
};

class VulkanResourceResolveError final : public std::runtime_error {
public:
    VulkanResourceResolveError(VulkanResourceResolveErrorCode code_,
                               ResourceHandle previous_resource_,
                               ResourceHandle next_resource_,
                               QueueClass source_queue_,
                               QueueClass target_queue_,
                               std::string message_);

    [[nodiscard]] VulkanResourceResolveErrorCode Code() const noexcept {
        return code;
    }

    [[nodiscard]] ResourceHandle PreviousResource() const noexcept {
        return previous_resource;
    }

    [[nodiscard]] ResourceHandle NextResource() const noexcept {
        return next_resource;
    }

    [[nodiscard]] QueueClass SourceQueue() const noexcept {
        return source_queue;
    }

    [[nodiscard]] QueueClass TargetQueue() const noexcept {
        return target_queue;
    }

private:
    VulkanResourceResolveErrorCode code = VulkanResourceResolveErrorCode::unsupported_cross_queue_alias;
    ResourceHandle previous_resource{};
    ResourceHandle next_resource{};
    QueueClass source_queue = QueueClass::graphics;
    QueueClass target_queue = QueueClass::graphics;
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
                 CompiledRenderGraph& compiled_graph_,
                 std::uint64_t last_submitted_value_,
                 std::uint64_t completed_submit_value_,
                 const RenderGraphExecutorCapabilities& executor_capabilities_ = {});

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
    std::vector<TransientImagePageRecord> transient_image_pages{};
    render::RetireBus<RetiredTransientImagePayload> retired_transient_images{};
    render::RetireBus<TransientImagePageRecord> retired_transient_image_pages{};
    std::vector<render::RenderTargetHandle> imported_textures{};
    std::vector<ImportedBufferBinding> imported_buffers{};
    VulkanResourceTableStats stats{};
};

} // namespace vr::render_graph
