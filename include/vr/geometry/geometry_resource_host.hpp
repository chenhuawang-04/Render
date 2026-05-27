#pragma once

#include "vr/geometry/geometry_types.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::geometry {

struct GeometryResourceHostCreateInfo {
    std::uint32_t reserve_mesh_count = 256U;
    std::uint32_t reserve_submesh_count = 1024U;
    std::uint32_t reserve_reusable_buffer_count = 256U;
    std::uint32_t max_reusable_vertex_buffer_count = 512U;
    std::uint32_t max_reusable_index_buffer_count = 512U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
};

struct GeometryResourceHostStats {
    std::uint32_t mesh_count = 0U;
    std::uint32_t uploaded_mesh_count = 0U;
    std::uint32_t updated_mesh_count = 0U;
    std::uint32_t removed_mesh_count = 0U;
    std::uint32_t retired_buffer_pair_count = 0U;
    std::uint32_t reusable_vertex_buffer_count = 0U;
    std::uint32_t reusable_index_buffer_count = 0U;
    std::uint32_t created_vertex_buffer_count = 0U;
    std::uint32_t created_index_buffer_count = 0U;
    std::uint32_t reused_vertex_buffer_count = 0U;
    std::uint32_t reused_index_buffer_count = 0U;
    std::uint32_t destroyed_vertex_buffer_count = 0U;
    std::uint32_t destroyed_index_buffer_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
};

class GeometryResourceHost final {
public:
    struct MeshRecord final {
        GeometryResourceId geometry_id{};
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        std::uint32_t vertex_count = 0U;
        std::uint32_t index_count = 0U;
        std::uint32_t vertex_stride = 0U;
        ecs::Float3 bounds_min{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        ecs::Float3 bounds_max{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        resource::BufferResource vertex_buffer{};
        resource::BufferResource index_buffer{};
        GeometryMcVector<GeometrySubmeshRange> submeshes{};
        std::uint32_t revision = 0U;
    };

    GeometryResourceHost() = default;
    ~GeometryResourceHost() = default;

    GeometryResourceHost(const GeometryResourceHost&) = delete;
    GeometryResourceHost& operator=(const GeometryResourceHost&) = delete;

    GeometryResourceHost(GeometryResourceHost&&) = delete;
    GeometryResourceHost& operator=(GeometryResourceHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const GeometryResourceHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_,
                    std::uint64_t completed_submit_value_);

    void UploadMesh(VulkanContext& context_,
                    render::UploadHost& upload_host_,
                    std::uint32_t frame_index_,
                    std::uint64_t last_submitted_value_,
                    std::uint64_t completed_submit_value_,
                    const GeometryMeshUploadInfo& upload_info_);

    [[nodiscard]] bool RemoveMesh(VulkanContext& context_,
                                  GeometryResourceId geometry_id_,
                                  std::uint64_t last_submitted_value_,
                                  std::uint64_t completed_submit_value_);

    [[nodiscard]] const MeshRecord* FindMesh(GeometryResourceId geometry_id_) const noexcept;
    [[nodiscard]] std::uint32_t MeshCount() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GeometryResourceHostStats& Stats() const noexcept;

private:
    struct RetiredBuffers final {
        resource::BufferResource vertex_buffer{};
        resource::BufferResource index_buffer{};
        std::uint64_t retire_value = 0U;
    };

    [[nodiscard]] static std::size_t LowerBoundReusableBufferIndex(
        const GeometryMcVector<resource::BufferResource>& buffers_,
        VkDeviceSize required_size_) noexcept;
    [[nodiscard]] std::size_t LowerBoundMeshIndex(GeometryResourceId geometry_id_) const noexcept;
    void RetireMeshBuffers(MeshRecord& mesh_,
                           std::uint64_t retire_value_);
    void DestroyMeshBuffers(VulkanContext& context_,
                            MeshRecord& mesh_) noexcept;
    void DestroyRetiredBuffers(VulkanContext& context_) noexcept;
    void DestroyReusableBuffers(VulkanContext& context_) noexcept;
    void CollectRetiredBuffers(VulkanContext& context_,
                               std::uint64_t completed_submit_value_);
    void RecycleOrDestroyBuffer(VulkanContext& context_,
                                resource::BufferResource& buffer_,
                                GeometryMcVector<resource::BufferResource>& reusable_buffers_,
                                std::uint32_t max_reusable_count_,
                                bool is_vertex_buffer_);
    [[nodiscard]] resource::BufferResource AcquireBuffer(VulkanContext& context_,
                                                         VkDeviceSize required_size_bytes_,
                                                         VkBufferUsageFlags usage_,
                                                         GeometryMcVector<resource::BufferResource>& reusable_buffers_,
                                                         bool is_vertex_buffer_);

    [[nodiscard]] MeshRecord BuildMeshRecord(VulkanContext& context_,
                                             render::UploadHost& upload_host_,
                                             std::uint32_t frame_index_,
                                             const GeometryMeshUploadInfo& upload_info_);

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    GeometryResourceHostCreateInfo create_info_cache{};
    GeometryMcVector<MeshRecord> meshes{};
    GeometryMcVector<RetiredBuffers> retired_buffers{};
    GeometryMcVector<resource::BufferResource> reusable_vertex_buffers{};
    GeometryMcVector<resource::BufferResource> reusable_index_buffers{};
    GeometryResourceHostStats stats{};
    bool initialized = false;
};

} // namespace vr::geometry

