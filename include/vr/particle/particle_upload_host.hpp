#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/particle_runtime_system.hpp"
#include "vr/particle/particle_types.hpp"
#include "vr/render/retire_bus.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::particle {

template<typename T>
using ParticleUploadMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ParticleUploadHostCreateInfo final {
    std::uint32_t frames_in_flight = 2U;
    VkDeviceSize initial_2d_instance_buffer_bytes = 2U * 1024U * 1024U;
    VkDeviceSize initial_3d_instance_buffer_bytes = 4U * 1024U * 1024U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bool allow_growth = true;
};

struct ParticleUploadHostStats final {
    std::uint64_t uploaded_bytes = 0U;
    std::uint32_t upload_count = 0U;
    std::uint32_t reuse_hit_count = 0U;
    std::uint32_t resized_buffer_count = 0U;
    std::uint32_t barrier_count = 0U;
};

struct Particle2DRuntimeUploadOptions final {
    ecs::ParticleRuntimeBuildConfig runtime_build{};
};

struct Particle3DRuntimeUploadOptions final {
    ecs::ParticleRuntimeBuildConfig runtime_build{};
};

struct Particle2DRuntimeUploadResult final {
    ecs::ParticleRuntimeBuildStats runtime{};
    ParticleUploadRange upload{};
    bool skipped_upload = true;
};

struct Particle3DRuntimeUploadResult final {
    ecs::ParticleRuntimeBuildStats runtime{};
    ParticleUploadRange upload{};
    bool skipped_upload = true;
};

class ParticleUploadHost final {
public:
    ParticleUploadHost() = default;
    ~ParticleUploadHost() = default;

    ParticleUploadHost(const ParticleUploadHost&) = delete;
    ParticleUploadHost& operator=(const ParticleUploadHost&) = delete;
    ParticleUploadHost(ParticleUploadHost&&) = delete;
    ParticleUploadHost& operator=(ParticleUploadHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const ParticleUploadHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_,
                    std::uint32_t frame_index_,
                    std::uint64_t last_submitted_value_ = 0U,
                    std::uint64_t completed_submit_value_ = 0U);

    [[nodiscard]] ParticleUploadRange Upload2DInstances(VulkanContext& context_,
                                                        render::UploadHost& upload_host_,
                                                        std::uint32_t frame_index_,
                                                        const ecs::Particle2DGpuInstance* instances_,
                                                        std::uint32_t instance_count_,
                                                        std::uint64_t revision_);

    [[nodiscard]] ParticleUploadRange Upload3DInstances(VulkanContext& context_,
                                                        render::UploadHost& upload_host_,
                                                        std::uint32_t frame_index_,
                                                        const ecs::Particle3DGpuInstance* instances_,
                                                        std::uint32_t instance_count_,
                                                        std::uint64_t revision_);

    [[nodiscard]] Particle2DRuntimeUploadResult PrepareRuntimeAndUpload2D(
        VulkanContext& context_,
        render::UploadHost& upload_host_,
        std::uint32_t frame_index_,
        std::uint64_t last_submitted_value_,
        std::uint64_t completed_submit_value_,
        const ecs::Particle<ecs::Dim2>* particles_,
        const ecs::ParticleEmitter<ecs::Dim2>* emitters_,
        const ecs::Transform<ecs::Dim2>* transforms_,
        std::uint32_t component_count_,
        ecs::Particle2DRuntimeScratch& runtime_scratch_,
        const ecs::Particle2DRuntimeBuildHint& runtime_build_hint_ = {},
        const Particle2DRuntimeUploadOptions& options_ = {});

    [[nodiscard]] Particle3DRuntimeUploadResult PrepareRuntimeAndUpload3D(
        VulkanContext& context_,
        render::UploadHost& upload_host_,
        std::uint32_t frame_index_,
        std::uint64_t last_submitted_value_,
        std::uint64_t completed_submit_value_,
        const ecs::Particle<ecs::Dim3>* particles_,
        const ecs::ParticleEmitter<ecs::Dim3>* emitters_,
        const ecs::Transform<ecs::Dim3>* transforms_,
        std::uint32_t component_count_,
        ecs::Particle3DRuntimeScratch& runtime_scratch_,
        const ecs::Particle3DRuntimeBuildHint& runtime_build_hint_ = {},
        const Particle3DRuntimeUploadOptions& options_ = {});

    [[nodiscard]] static std::uint64_t ComposeUploadRevision(
        std::uint64_t component_signature_,
        std::uint64_t transform_signature_,
        std::uint64_t visible_signature_,
        std::uint64_t runtime_state_signature_) noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] const ParticleUploadHostStats& Stats() const noexcept;

private:
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
    static void HashCombineRevision(std::uint64_t& hash_,
                                    std::uint64_t value_) noexcept;
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

    [[nodiscard]] ParticleUploadRange UploadFullBuffer(VulkanContext& context_,
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
    ParticleUploadHostCreateInfo create_info_cache{};
    ParticleUploadMcVector<FrameState> frames{};
    render::RetireBus<resource::BufferResource> retired_buffers{};
    ParticleUploadHostStats stats{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool initialized = false;
};

} // namespace vr::particle
