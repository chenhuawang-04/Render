#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/surface_upload_plan_system.hpp"
#include "vr/ecs/system/surface_runtime_system.hpp"
#include "vr/render/retire_bus.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/surface/surface_types.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::surface {

template<typename T>
using SurfaceUploadMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SurfaceUploadHostCreateInfo {
    std::uint32_t frames_in_flight = 2U;
    VkDeviceSize initial_2d_instance_buffer_bytes = 2U * 1024U * 1024U;
    VkDeviceSize initial_3d_instance_buffer_bytes = 4U * 1024U * 1024U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bool allow_growth = true;
    std::uint32_t patch_merge_gap_bytes = 0U;
    std::uint32_t patch_fallback_coverage_percent = 70U;
    std::uint32_t patch_fallback_copy_count = 96U;
};

struct SurfaceUploadHostStats {
    std::uint64_t uploaded_bytes = 0U;
    std::uint32_t upload_count = 0U;
    std::uint32_t partial_upload_count = 0U;
    std::uint32_t patch_copy_count = 0U;
    std::uint32_t patch_input_count = 0U;
    std::uint32_t patch_merged_count = 0U;
    std::uint32_t patch_dropped_count = 0U;
    std::uint32_t patch_fallback_full_upload_count = 0U;
    std::uint32_t reuse_hit_count = 0U;
    std::uint32_t resized_buffer_count = 0U;
    std::uint32_t barrier_count = 0U;
};

struct Surface2DRuntimeUploadOptions final {
    ecs::Surface2DRuntimeBuildConfig runtime_build{};
    ecs::SurfaceUploadPlanBuildOptions plan_build =
        ecs::SurfaceUploadPlanSystem<ecs::Dim2>::DefaultBuildOptions();
    bool enable_partial_upload = true;
    bool require_dirty_hint_for_partial = true;
    std::uint32_t min_partial_dirty_component_count = 1U;
};

struct Surface3DRuntimeUploadOptions final {
    ecs::Surface3DRuntimeBuildConfig runtime_build{};
    ecs::SurfaceUploadPlanBuildOptions plan_build =
        ecs::SurfaceUploadPlanSystem<ecs::Dim3>::DefaultBuildOptions();
    bool enable_partial_upload = true;
    bool require_dirty_hint_for_partial = true;
    std::uint32_t min_partial_dirty_component_count = 1U;
};

struct Surface2DRuntimeUploadResult final {
    ecs::Surface2DRuntimeBuildStats runtime{};
    ecs::SurfaceUploadPlanStats plan{};
    SurfaceUploadRange upload{};
    bool used_partial_upload = false;
    bool skipped_upload = true;
};

struct Surface3DRuntimeUploadResult final {
    ecs::Surface3DRuntimeBuildStats runtime{};
    ecs::SurfaceUploadPlanStats plan{};
    SurfaceUploadRange upload{};
    bool used_partial_upload = false;
    bool skipped_upload = true;
};

class SurfaceUploadHost final {
public:
    SurfaceUploadHost() = default;
    ~SurfaceUploadHost() = default;

    SurfaceUploadHost(const SurfaceUploadHost&) = delete;
    SurfaceUploadHost& operator=(const SurfaceUploadHost&) = delete;

    SurfaceUploadHost(SurfaceUploadHost&&) = delete;
    SurfaceUploadHost& operator=(SurfaceUploadHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const SurfaceUploadHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_,
                    std::uint32_t frame_index_,
                    std::uint64_t last_submitted_value_ = 0U,
                    std::uint64_t completed_submit_value_ = 0U);

    [[nodiscard]] SurfaceUploadRange Upload2DInstances(VulkanContext& context_,
                                                       render::UploadHost& upload_host_,
                                                       std::uint32_t frame_index_,
                                                       const ecs::Surface2DGpuInstance* instances_,
                                                       std::uint32_t instance_count_,
                                                       std::uint64_t revision_);

    [[nodiscard]] SurfaceUploadRange Upload3DInstances(VulkanContext& context_,
                                                       render::UploadHost& upload_host_,
                                                       std::uint32_t frame_index_,
                                                       const ecs::Surface3DGpuInstance* instances_,
                                                       std::uint32_t instance_count_,
                                                       std::uint64_t revision_);

    [[nodiscard]] SurfaceUploadRange Upload2DInstancePatches(VulkanContext& context_,
                                                             render::UploadHost& upload_host_,
                                                             std::uint32_t frame_index_,
                                                             const ecs::Surface2DGpuInstance* instances_,
                                                             std::uint32_t instance_count_,
                                                             const SurfaceUploadPatch* patches_,
                                                             std::uint32_t patch_count_,
                                                             std::uint64_t revision_);

    [[nodiscard]] SurfaceUploadRange Upload3DInstancePatches(VulkanContext& context_,
                                                             render::UploadHost& upload_host_,
                                                             std::uint32_t frame_index_,
                                                             const ecs::Surface3DGpuInstance* instances_,
                                                             std::uint32_t instance_count_,
                                                             const SurfaceUploadPatch* patches_,
                                                             std::uint32_t patch_count_,
                                                             std::uint64_t revision_);

    [[nodiscard]] Surface2DRuntimeUploadResult PrepareRuntimeAndUpload2D(
        VulkanContext& context_,
        render::UploadHost& upload_host_,
        std::uint32_t frame_index_,
        const ecs::Surface<ecs::Dim2>* components_,
        const ecs::Transform<ecs::Dim2>* transforms_,
        std::uint32_t component_count_,
        ecs::Surface2DRuntimeScratch& runtime_scratch_,
        ecs::SurfaceUploadPlanScratch<ecs::Dim2>& plan_scratch_,
        const ecs::Surface2DRuntimeBuildHint& runtime_build_hint_ = {},
        const Surface2DRuntimeUploadOptions& options_ = {});

    [[nodiscard]] Surface3DRuntimeUploadResult PrepareRuntimeAndUpload3D(
        VulkanContext& context_,
        render::UploadHost& upload_host_,
        std::uint32_t frame_index_,
        const ecs::Surface<ecs::Dim3>* components_,
        const ecs::Transform<ecs::Dim3>* transforms_,
        std::uint32_t component_count_,
        ecs::Surface3DRuntimeScratch& runtime_scratch_,
        ecs::SurfaceUploadPlanScratch<ecs::Dim3>& plan_scratch_,
        const ecs::Surface3DRuntimeBuildHint& runtime_build_hint_ = {},
        const Surface3DRuntimeUploadOptions& options_ = {});

    [[nodiscard]] static bool ShouldAttemptPartialUpload(
        const ecs::Surface2DRuntimeBuildStats& runtime_stats_,
        const ecs::Surface2DRuntimeBuildHint& runtime_build_hint_,
        const Surface2DRuntimeUploadOptions& options_) noexcept;

    [[nodiscard]] static bool ShouldAttemptPartialUpload(
        const ecs::Surface3DRuntimeBuildStats& runtime_stats_,
        const ecs::Surface3DRuntimeBuildHint& runtime_build_hint_,
        const Surface3DRuntimeUploadOptions& options_) noexcept;

    [[nodiscard]] static std::uint64_t ComposeUploadRevision(
        std::uint64_t surface_signature_,
        std::uint64_t transform_signature_) noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] const SurfaceUploadHostStats& Stats() const noexcept;

private:
    struct BytePatchRange final {
        VkDeviceSize dst_offset = 0U;
        VkDeviceSize size_bytes = 0U;
    };

    struct StreamBuffer final {
        resource::BufferResource buffer{};
        VkDeviceSize capacity_bytes = 0U;
        VkDeviceSize uploaded_size_bytes = 0U;
        std::uint32_t element_count = 0U;
        std::uint64_t uploaded_revision = 0U;
    };

    struct FrameState final {
        StreamBuffer instances_2d{};
        StreamBuffer instances_3d{};
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

    [[nodiscard]] SurfaceUploadRange UploadFullBuffer(VulkanContext& context_,
                                                      render::UploadHost& upload_host_,
                                                      std::uint32_t frame_index_,
                                                      StreamBuffer& stream_,
                                                      const void* src_data_,
                                                      VkDeviceSize size_bytes_,
                                                      std::uint32_t element_count_,
                                                      std::uint64_t revision_,
                                                      VkDeviceSize minimum_capacity_bytes_);

    [[nodiscard]] SurfaceUploadRange UploadBufferPatches(VulkanContext& context_,
                                                         render::UploadHost& upload_host_,
                                                         std::uint32_t frame_index_,
                                                         StreamBuffer& stream_,
                                                         const void* src_data_,
                                                         std::uint32_t element_count_,
                                                         std::size_t element_size_bytes_,
                                                         const SurfaceUploadPatch* patches_,
                                                         std::uint32_t patch_count_,
                                                         std::uint64_t revision_,
                                                         VkDeviceSize minimum_capacity_bytes_);

    [[nodiscard]] bool ShouldFallbackToFullUpload(
        VkDeviceSize full_size_bytes_,
        VkDeviceSize merged_patch_bytes_,
        std::uint32_t merged_patch_count_) const noexcept;

    static void HashCombineRevision(std::uint64_t& hash_,
                                    std::uint64_t value_) noexcept;

    void BuildMergedByteRanges(std::uint32_t element_count_,
                               std::size_t element_size_bytes_,
                               const SurfaceUploadPatch* patches_,
                               std::uint32_t patch_count_,
                               std::uint32_t& out_dropped_patch_count_,
                               std::uint64_t& out_merged_patch_bytes_);

    [[nodiscard]] FrameState& FrameAt(std::uint32_t frame_index_);
    [[nodiscard]] const FrameState& FrameAt(std::uint32_t frame_index_) const;

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    SurfaceUploadHostCreateInfo create_info_cache{};
    SurfaceUploadMcVector<FrameState> frames{};
    render::RetireBus<resource::BufferResource> retired_buffers{};
    SurfaceUploadMcVector<SurfaceUploadPatch> patch_scratch{};
    SurfaceUploadMcVector<BytePatchRange> merged_patch_scratch{};
    SurfaceUploadHostStats stats{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool initialized = false;
};

} // namespace vr::surface
