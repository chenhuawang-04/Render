#include "vr/geometry/geometry_resource_host.hpp"

#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace vr::geometry {

void GeometryResourceHost::Initialize(VulkanContext& context_,
                                      resource::GpuMemoryHost& gpu_memory_host_,
                                      const GeometryResourceHostCreateInfo& create_info_) {
    if (initialized) {
        Shutdown(context_);
    }

    gpu_memory_host = &gpu_memory_host_;
    create_info_cache = create_info_;

    meshes.clear();
    retired_buffers.clear();
    reusable_vertex_buffers.clear();
    reusable_index_buffers.clear();
    stats = {};

    if (create_info_cache.reserve_mesh_count > 0U) {
        meshes.reserve(create_info_cache.reserve_mesh_count);
        retired_buffers.reserve(create_info_cache.reserve_mesh_count);
    }
    if (create_info_cache.reserve_reusable_buffer_count > 0U) {
        reusable_vertex_buffers.reserve(create_info_cache.reserve_reusable_buffer_count);
        reusable_index_buffers.reserve(create_info_cache.reserve_reusable_buffer_count);
    }

    initialized = true;
}

void GeometryResourceHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.Device());
    }

    for (auto& mesh : meshes) {
        DestroyMeshBuffers(context_, mesh);
        mesh.submeshes.clear();
    }
    meshes.clear();

    DestroyRetiredBuffers(context_);
    DestroyReusableBuffers(context_);

    gpu_memory_host = nullptr;
    create_info_cache = {};
    stats = {};
    initialized = false;
}

void GeometryResourceHost::BeginFrame(VulkanContext& context_,
                                      std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("GeometryResourceHost::BeginFrame called before Initialize");
    }

    CollectRetiredBuffers(context_, completed_submit_value_);
    stats.mesh_count = static_cast<std::uint32_t>(meshes.size());
    stats.retired_buffer_pair_count = static_cast<std::uint32_t>(retired_buffers.size());
    stats.reusable_vertex_buffer_count = static_cast<std::uint32_t>(reusable_vertex_buffers.size());
    stats.reusable_index_buffer_count = static_cast<std::uint32_t>(reusable_index_buffers.size());
}

void GeometryResourceHost::UploadMesh(VulkanContext& context_,
                                      render::UploadHost& upload_host_,
                                      std::uint32_t frame_index_,
                                      std::uint64_t last_submitted_value_,
                                      std::uint64_t completed_submit_value_,
                                      const GeometryMeshUploadInfo& upload_info_) {
    if (!initialized || gpu_memory_host == nullptr) {
        throw std::runtime_error("GeometryResourceHost::UploadMesh called before Initialize");
    }
    if (upload_info_.geometry_id == 0U) {
        throw std::invalid_argument("GeometryResourceHost::UploadMesh geometry_id must be non-zero");
    }
    if (upload_info_.vertices == nullptr || upload_info_.vertex_count == 0U) {
        throw std::invalid_argument("GeometryResourceHost::UploadMesh requires non-empty vertex data");
    }
    if (upload_info_.indices == nullptr || upload_info_.index_count == 0U) {
        throw std::invalid_argument("GeometryResourceHost::UploadMesh requires non-empty index data");
    }

    CollectRetiredBuffers(context_, completed_submit_value_);
    MeshRecord new_record = BuildMeshRecord(context_, upload_host_, frame_index_, upload_info_);

    const std::size_t lower_bound_index = LowerBoundMeshIndex(upload_info_.geometry_id);
    const bool replace_existing =
        lower_bound_index < meshes.size() &&
        meshes[lower_bound_index].geometry_id == upload_info_.geometry_id;
    if (replace_existing) {
        MeshRecord& old_record = meshes[lower_bound_index];
        new_record.revision = old_record.revision + 1U;
        RetireMeshBuffers(old_record, last_submitted_value_);
        old_record.submeshes.clear();
        meshes[lower_bound_index] = std::move(new_record);
        ++stats.updated_mesh_count;
    } else {
        const std::size_t old_size = meshes.size();
        meshes.resize(old_size + 1U);
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            meshes[index] = std::move(meshes[index - 1U]);
        }
        meshes[lower_bound_index] = std::move(new_record);
        ++stats.uploaded_mesh_count;
    }

    const VkDeviceSize vertex_buffer_bytes =
        static_cast<VkDeviceSize>(upload_info_.vertex_count) * sizeof(GeometryMeshVertex);
    const VkDeviceSize index_buffer_bytes =
        static_cast<VkDeviceSize>(upload_info_.index_count) * sizeof(std::uint32_t);
    stats.uploaded_bytes += static_cast<std::uint64_t>(vertex_buffer_bytes + index_buffer_bytes);
    stats.mesh_count = static_cast<std::uint32_t>(meshes.size());
    stats.retired_buffer_pair_count = static_cast<std::uint32_t>(retired_buffers.size());
    stats.reusable_vertex_buffer_count = static_cast<std::uint32_t>(reusable_vertex_buffers.size());
    stats.reusable_index_buffer_count = static_cast<std::uint32_t>(reusable_index_buffers.size());
}

bool GeometryResourceHost::RemoveMesh(VulkanContext& context_,
                                      std::uint32_t geometry_id_,
                                      std::uint64_t last_submitted_value_,
                                      std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("GeometryResourceHost::RemoveMesh called before Initialize");
    }
    if (geometry_id_ == 0U) {
        return false;
    }

    CollectRetiredBuffers(context_, completed_submit_value_);
    const std::size_t lower_bound_index = LowerBoundMeshIndex(geometry_id_);
    if (lower_bound_index >= meshes.size() ||
        meshes[lower_bound_index].geometry_id != geometry_id_) {
        return false;
    }

    MeshRecord& mesh = meshes[lower_bound_index];
    RetireMeshBuffers(mesh, last_submitted_value_);
    mesh.submeshes.clear();
    meshes.erase(meshes.begin() + static_cast<std::ptrdiff_t>(lower_bound_index));

    ++stats.removed_mesh_count;
    stats.mesh_count = static_cast<std::uint32_t>(meshes.size());
    stats.retired_buffer_pair_count = static_cast<std::uint32_t>(retired_buffers.size());
    return true;
}

const GeometryResourceHost::MeshRecord* GeometryResourceHost::FindMesh(std::uint32_t geometry_id_) const noexcept {
    if (!initialized || geometry_id_ == 0U) {
        return nullptr;
    }

    const std::size_t lower_bound_index = LowerBoundMeshIndex(geometry_id_);
    if (lower_bound_index >= meshes.size()) {
        return nullptr;
    }
    const MeshRecord& mesh = meshes[lower_bound_index];
    if (mesh.geometry_id != geometry_id_) {
        return nullptr;
    }
    return &mesh;
}

std::uint32_t GeometryResourceHost::MeshCount() const noexcept {
    return static_cast<std::uint32_t>(meshes.size());
}

bool GeometryResourceHost::IsInitialized() const noexcept {
    return initialized;
}

const GeometryResourceHostStats& GeometryResourceHost::Stats() const noexcept {
    return stats;
}

std::size_t GeometryResourceHost::LowerBoundReusableBufferIndex(
    const GeometryMcVector<resource::BufferResource>& buffers_,
    VkDeviceSize required_size_) noexcept {
    std::size_t first = 0U;
    std::size_t count = buffers_.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (buffers_[it].size < required_size_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

std::size_t GeometryResourceHost::LowerBoundMeshIndex(std::uint32_t geometry_id_) const noexcept {
    std::size_t first = 0U;
    std::size_t count = meshes.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (meshes[it].geometry_id < geometry_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

void GeometryResourceHost::RetireMeshBuffers(MeshRecord& mesh_,
                                             std::uint64_t retire_value_) {
    if (mesh_.vertex_buffer.buffer == VK_NULL_HANDLE &&
        mesh_.index_buffer.buffer == VK_NULL_HANDLE) {
        return;
    }

    RetiredBuffers retired{};
    retired.vertex_buffer = mesh_.vertex_buffer;
    retired.index_buffer = mesh_.index_buffer;
    retired.retire_value = retire_value_;
    retired_buffers.push_back(retired);

    mesh_.vertex_buffer = {};
    mesh_.index_buffer = {};
}

void GeometryResourceHost::DestroyMeshBuffers(VulkanContext& context_,
                                              MeshRecord& mesh_) noexcept {
    if (mesh_.vertex_buffer.buffer != VK_NULL_HANDLE) {
        resource::BufferHost::DestroyBuffer(context_, mesh_.vertex_buffer);
    }
    if (mesh_.index_buffer.buffer != VK_NULL_HANDLE) {
        resource::BufferHost::DestroyBuffer(context_, mesh_.index_buffer);
    }
}

void GeometryResourceHost::DestroyRetiredBuffers(VulkanContext& context_) noexcept {
    for (auto& retired : retired_buffers) {
        if (retired.vertex_buffer.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(context_, retired.vertex_buffer);
        }
        if (retired.index_buffer.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(context_, retired.index_buffer);
        }
    }
    retired_buffers.clear();
}

void GeometryResourceHost::DestroyReusableBuffers(VulkanContext& context_) noexcept {
    for (auto& buffer : reusable_vertex_buffers) {
        if (buffer.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(context_, buffer);
        }
    }
    reusable_vertex_buffers.clear();

    for (auto& buffer : reusable_index_buffers) {
        if (buffer.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(context_, buffer);
        }
    }
    reusable_index_buffers.clear();
}

void GeometryResourceHost::CollectRetiredBuffers(VulkanContext& context_,
                                                 std::uint64_t completed_submit_value_) {
    if (retired_buffers.empty()) {
        return;
    }

    std::size_t write_index = 0U;
    for (std::size_t read_index = 0U; read_index < retired_buffers.size(); ++read_index) {
        RetiredBuffers& retired = retired_buffers[read_index];
        if (retired.retire_value <= completed_submit_value_) {
            RecycleOrDestroyBuffer(context_,
                                   retired.vertex_buffer,
                                   reusable_vertex_buffers,
                                   create_info_cache.max_reusable_vertex_buffer_count,
                                   true);
            RecycleOrDestroyBuffer(context_,
                                   retired.index_buffer,
                                   reusable_index_buffers,
                                   create_info_cache.max_reusable_index_buffer_count,
                                   false);
            continue;
        }

        if (write_index != read_index) {
            retired_buffers[write_index] = retired;
        }
        ++write_index;
    }
    retired_buffers.resize(write_index);

    stats.retired_buffer_pair_count = static_cast<std::uint32_t>(retired_buffers.size());
    stats.reusable_vertex_buffer_count = static_cast<std::uint32_t>(reusable_vertex_buffers.size());
    stats.reusable_index_buffer_count = static_cast<std::uint32_t>(reusable_index_buffers.size());
}

void GeometryResourceHost::RecycleOrDestroyBuffer(
    VulkanContext& context_,
    resource::BufferResource& buffer_,
    GeometryMcVector<resource::BufferResource>& reusable_buffers_,
    std::uint32_t max_reusable_count_,
    bool is_vertex_buffer_) {
    if (buffer_.buffer == VK_NULL_HANDLE) {
        return;
    }

    if (max_reusable_count_ == 0U ||
        reusable_buffers_.size() >= static_cast<std::size_t>(max_reusable_count_)) {
        resource::BufferHost::DestroyBuffer(context_, buffer_);
        if (is_vertex_buffer_) {
            ++stats.destroyed_vertex_buffer_count;
        } else {
            ++stats.destroyed_index_buffer_count;
        }
        return;
    }

    const std::size_t insert_index = LowerBoundReusableBufferIndex(reusable_buffers_, buffer_.size);
    const std::size_t old_size = reusable_buffers_.size();
    reusable_buffers_.resize(old_size + 1U);
    for (std::size_t index = old_size; index > insert_index; --index) {
        reusable_buffers_[index] = std::move(reusable_buffers_[index - 1U]);
    }
    reusable_buffers_[insert_index] = buffer_;
    buffer_ = {};
}

resource::BufferResource GeometryResourceHost::AcquireBuffer(
    VulkanContext& context_,
    VkDeviceSize required_size_bytes_,
    VkBufferUsageFlags usage_,
    GeometryMcVector<resource::BufferResource>& reusable_buffers_,
    bool is_vertex_buffer_) {
    if (required_size_bytes_ == 0U) {
        throw std::invalid_argument("GeometryResourceHost::AcquireBuffer required_size_bytes must be > 0");
    }

    const std::size_t first_candidate = LowerBoundReusableBufferIndex(reusable_buffers_, required_size_bytes_);
    for (std::size_t index = first_candidate; index < reusable_buffers_.size(); ++index) {
        const resource::BufferResource& candidate = reusable_buffers_[index];
        if (candidate.size < required_size_bytes_) {
            continue;
        }
        if ((candidate.usage & usage_) != usage_) {
            continue;
        }
        if (candidate.memory_properties != create_info_cache.memory_properties) {
            continue;
        }
        if (candidate.memory_host != gpu_memory_host) {
            continue;
        }

        resource::BufferResource acquired = candidate;
        reusable_buffers_.erase(reusable_buffers_.begin() + static_cast<std::ptrdiff_t>(index));
        if (is_vertex_buffer_) {
            ++stats.reused_vertex_buffer_count;
        } else {
            ++stats.reused_index_buffer_count;
        }
        return acquired;
    }

    resource::BufferCreateInfo create_info{};
    create_info.size = required_size_bytes_;
    create_info.usage = usage_;
    create_info.memory_properties = create_info_cache.memory_properties;

    if (is_vertex_buffer_) {
        ++stats.created_vertex_buffer_count;
    } else {
        ++stats.created_index_buffer_count;
    }
    return resource::BufferHost::CreateBuffer(context_, create_info, *gpu_memory_host);
}

GeometryResourceHost::MeshRecord GeometryResourceHost::BuildMeshRecord(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const GeometryMeshUploadInfo& upload_info_) {
    const VkDeviceSize vertex_buffer_bytes =
        static_cast<VkDeviceSize>(upload_info_.vertex_count) * sizeof(GeometryMeshVertex);
    const VkDeviceSize index_buffer_bytes =
        static_cast<VkDeviceSize>(upload_info_.index_count) * sizeof(std::uint32_t);
    if (vertex_buffer_bytes == 0U || index_buffer_bytes == 0U) {
        throw std::invalid_argument("GeometryResourceHost::BuildMeshRecord buffer size must be > 0");
    }

    MeshRecord record{};
    record.vertex_buffer = AcquireBuffer(context_,
                                         vertex_buffer_bytes,
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         reusable_vertex_buffers,
                                         true);
    record.index_buffer = AcquireBuffer(context_,
                                        index_buffer_bytes,
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        reusable_index_buffers,
                                        false);

    upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                          record.vertex_buffer.buffer,
                                          0U,
                                          upload_info_.vertices,
                                          vertex_buffer_bytes,
                                          16U);
    upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                          record.index_buffer.buffer,
                                          0U,
                                          upload_info_.indices,
                                          index_buffer_bytes,
                                          16U);

    if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
        VkBufferMemoryBarrier2 vertex_barrier{};
        vertex_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        vertex_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        vertex_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        vertex_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        vertex_barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        vertex_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vertex_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vertex_barrier.buffer = record.vertex_buffer.buffer;
        vertex_barrier.offset = 0U;
        vertex_barrier.size = vertex_buffer_bytes;
        upload_host_.RecordBufferBarrier2(frame_index_, vertex_barrier);

        VkBufferMemoryBarrier2 index_barrier{};
        index_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        index_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        index_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        index_barrier.dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        index_barrier.dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT;
        index_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        index_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        index_barrier.buffer = record.index_buffer.buffer;
        index_barrier.offset = 0U;
        index_barrier.size = index_buffer_bytes;
        upload_host_.RecordBufferBarrier2(frame_index_, index_barrier);
    }

    record.geometry_id = upload_info_.geometry_id;
    record.topology = upload_info_.topology;
    record.vertex_count = upload_info_.vertex_count;
    record.index_count = upload_info_.index_count;
    record.vertex_stride = static_cast<std::uint32_t>(sizeof(GeometryMeshVertex));
    record.bounds_min = upload_info_.bounds_min;
    record.bounds_max = upload_info_.bounds_max;
    record.revision = 1U;

    if (upload_info_.submeshes != nullptr && upload_info_.submesh_count > 0U) {
        record.submeshes.reserve(upload_info_.submesh_count);
        for (std::uint32_t i = 0U; i < upload_info_.submesh_count; ++i) {
            const GeometrySubmeshRange& submesh = upload_info_.submeshes[i];
            const std::uint32_t bounded_first = std::min(submesh.first_index, upload_info_.index_count);
            const std::uint32_t bounded_count =
                std::min(submesh.index_count, upload_info_.index_count - bounded_first);
            if (bounded_count == 0U) {
                continue;
            }
            GeometrySubmeshRange normalized = submesh;
            normalized.first_index = bounded_first;
            normalized.index_count = bounded_count;
            record.submeshes.push_back(normalized);
        }
    }

    if (record.submeshes.empty()) {
        record.submeshes.push_back(GeometrySubmeshRange{
            .first_index = 0U,
            .index_count = upload_info_.index_count,
            .vertex_offset = 0,
            .reserved0 = 0U
        });
    }

    stats.reusable_vertex_buffer_count = static_cast<std::uint32_t>(reusable_vertex_buffers.size());
    stats.reusable_index_buffer_count = static_cast<std::uint32_t>(reusable_index_buffers.size());
    return record;
}

} // namespace vr::geometry
