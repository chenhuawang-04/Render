#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/geometry/geometry_temporal_motion_builder.hpp"
#include "vr/ecs/system/geometry_runtime_system.hpp"
#include "vr/render/retire_bus.hpp"
#include "vr/geometry/geometry_types.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::geometry {

template<typename T>
using GeometryUploadMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct GeometryUploadHostCreateInfo {
    std::uint32_t frames_in_flight = 2U;
    VkDeviceSize initial_2d_primitive_buffer_bytes = 2U * 1024U * 1024U;
    VkDeviceSize initial_3d_instance_buffer_bytes = 4U * 1024U * 1024U;
    VkDeviceSize initial_3d_temporal_motion_buffer_bytes = 4U * 1024U * 1024U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bool allow_growth = true;
};

struct GeometryUploadHostStats {
    std::uint64_t uploaded_bytes = 0U;
    std::uint32_t upload_count = 0U;
    std::uint32_t reuse_hit_count = 0U;
    std::uint32_t resized_buffer_count = 0U;
};

class GeometryUploadHost final {
public:
    GeometryUploadHost() = default;
    ~GeometryUploadHost() = default;

    GeometryUploadHost(const GeometryUploadHost&) = delete;
    GeometryUploadHost& operator=(const GeometryUploadHost&) = delete;

    GeometryUploadHost(GeometryUploadHost&&) = delete;
    GeometryUploadHost& operator=(GeometryUploadHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const GeometryUploadHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_,
                    std::uint32_t frame_index_,
                    std::uint64_t last_submitted_value_ = 0U,
                    std::uint64_t completed_submit_value_ = 0U);

    [[nodiscard]] GeometryUploadRange Upload2DPrimitives(VulkanContext& context_,
                                                         render::UploadHost& upload_host_,
                                                         std::uint32_t frame_index_,
                                                         const ecs::Geometry2DPathPrimitive* primitives_,
                                                         std::uint32_t primitive_count_,
                                                         std::uint64_t revision_);

    [[nodiscard]] GeometryUploadRange Upload3DInstances(VulkanContext& context_,
                                                        render::UploadHost& upload_host_,
                                                        std::uint32_t frame_index_,
                                                        const ecs::Geometry3DGpuInstance* instances_,
                                                        std::uint32_t instance_count_,
                                                        std::uint64_t revision_);

    [[nodiscard]] GeometryUploadRange Upload3DTemporalMotionInstances(
        VulkanContext& context_,
        render::UploadHost& upload_host_,
        std::uint32_t frame_index_,
        const Geometry3DTemporalMotionInstance* instances_,
        std::uint32_t instance_count_,
        std::uint64_t revision_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] const GeometryUploadHostStats& Stats() const noexcept;

private:
    struct StreamBuffer final {
        resource::BufferResource buffer{};
        VkDeviceSize capacity_bytes = 0U;
        VkDeviceSize uploaded_size_bytes = 0U;
        std::uint32_t element_count = 0U;
        std::uint64_t uploaded_revision = 0U;
    };

    struct FrameState final {
        StreamBuffer primitives_2d{};
        StreamBuffer instances_3d{};
        StreamBuffer temporal_motion_instances_3d{};
    };

    [[nodiscard]] static VkDeviceSize NextPow2(VkDeviceSize value_) noexcept;
    static void DestroyStreamBuffer(VulkanContext& context_,
                                    StreamBuffer& stream_);
    void RetireStreamBuffer(StreamBuffer& stream_,
                            std::uint64_t retire_value_);
    void CollectRetiredBuffers(VulkanContext& context_,
                               std::uint64_t completed_submit_value_);
    void DestroyRetiredBuffers(VulkanContext& context_) noexcept;
    [[nodiscard]] std::uint64_t ComputeRetireValue() const noexcept;
    void EnsureStreamCapacity(VulkanContext& context_,
                              StreamBuffer& stream_,
                              VkDeviceSize required_bytes_,
                              VkBufferUsageFlags usage_,
                              VkDeviceSize minimum_capacity_bytes_);

    [[nodiscard]] GeometryUploadRange UploadToStream(VulkanContext& context_,
                                                     render::UploadHost& upload_host_,
                                                     std::uint32_t frame_index_,
                                                     StreamBuffer& stream_,
                                                     const void* src_data_,
                                                     VkDeviceSize size_bytes_,
                                                     std::uint32_t element_count_,
                                                     std::uint64_t revision_,
                                                     VkDeviceSize minimum_capacity_bytes_);

    [[nodiscard]] FrameState& FrameAt(std::uint32_t frame_index_);
    [[nodiscard]] const FrameState& FrameAt(std::uint32_t frame_index_) const;

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    GeometryUploadHostCreateInfo create_info_cache{};
    GeometryUploadMcVector<FrameState> frames{};
    render::RetireBus<resource::BufferResource> retired_buffers{};
    GeometryUploadHostStats stats{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool initialized = false;
};

} // namespace vr::geometry

