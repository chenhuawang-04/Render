#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/particle_component.hpp"
#include "vr/ecs/system/particle_runtime_system.hpp"
#include "vr/particle/particle_types.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/retire_bus.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::render {
class UploadHost;
}

namespace vr::particle {

template<typename T>
using ParticleSimulationMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ParticleGpuStateRecord final {
    float position_x;
    float position_y;
    float position_z;
    float age_s;
    float velocity_x;
    float velocity_y;
    float velocity_z;
    float lifetime_s;
    float start_size;
    float end_size;
    float rotation_radians;
    float angular_velocity_radians;
    std::uint32_t start_color_rgba8;
    std::uint32_t end_color_rgba8;
    std::uint32_t texture_id;
    std::uint32_t material_id;
    std::uint32_t component_index;
    std::uint32_t user_data;
    std::uint32_t packed_flags;
    std::uint32_t reserved0;
    float drag_coefficient;
    float gravity_scale;
    float soft_particle_distance;
    float stretch_velocity_scale;
};

struct ParticleGpuAliveEntry final {
    std::uint32_t compacted_instance_index;
    std::uint32_t state_index;
};

struct ParticleGpuComponentRange final {
    std::uint32_t state_range_begin;
    std::uint32_t state_range_count;
    std::uint32_t batch_index;
    std::uint32_t reserved0;
};

struct ParticleGpuSpawnPacket final {
    std::uint32_t component_index;
    std::uint32_t spawn_count;
    std::uint32_t random_seed;
    std::uint32_t spawn_cursor;
    std::uint32_t state_range_begin;
    std::uint32_t state_range_count;
    std::uint32_t simulation_space;
    std::uint32_t emitter_shape;
    std::uint32_t packed_flags;
    float delta_time_s;
    float emitter_time_s;
    float lifetime_min_s;
    float lifetime_max_s;
    float speed_min;
    float speed_max;
    float start_size_min;
    float start_size_max;
    float end_size_min;
    float end_size_max;
    float rotation_min_radians;
    float rotation_max_radians;
    float angular_velocity_min;
    float angular_velocity_max;
    float drag_coefficient;
    float gravity_scale;
    float emission_extent_x;
    float emission_extent_y;
    float emission_extent_z;
    float emission_radius;
    float emission_axis_x;
    float emission_axis_y;
    float emission_axis_z;
    float cone_half_angle_radians;
    float spread_angle_radians;
    float transform_x0;
    float transform_x1;
    float transform_x2;
    float transform_x3;
    float transform_y0;
    float transform_y1;
    float transform_y2;
    float transform_y3;
    float transform_z0;
    float transform_z1;
    float transform_z2;
    float transform_z3;
};

struct ParticleGpuSortKeyRecord final {
    std::uint32_t sort_key_hi;
    std::uint32_t sort_key_lo;
    std::uint32_t state_index;
    std::uint32_t batch_index;
};

struct ParticleGpuIndirectCommand final {
    VkDrawIndirectCommand draw{};
    std::uint32_t live_particle_count = 0U;
    std::uint32_t reserved0 = 0U;
};

enum class ParticleSimulationResolvedPath : std::uint8_t {
    cpu = 0U,
    gpu = 1U,
    hybrid_gpu = 2U,
};

struct ParticleSimulationHostCapabilities final {
    bool compute_queue_available = false;
    bool synchronization2 = false;
    bool draw_indirect_count = false;
    bool descriptor_indexing = false;
    bool buffer_device_address = false;
    bool shader_int64 = false;

    [[nodiscard]] bool SupportsGpuSimulation() const noexcept {
        return compute_queue_available && synchronization2;
    }

    [[nodiscard]] bool SupportsHybridSimulation() const noexcept {
        return compute_queue_available && synchronization2;
    }
};

struct ParticleSimulationHostCreateInfo final {
    std::uint32_t frames_in_flight = 2U;
    std::uint32_t initial_particle_capacity = 32U * 1024U;
    std::uint32_t initial_visible_particle_capacity = 32U * 1024U;
    std::uint32_t initial_spawn_packet_capacity = 4096U;
    std::uint32_t initial_indirect_command_capacity = 256U;
    std::uint32_t initial_sort_key_capacity = 32U * 1024U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bool allow_growth = true;
    bool prefer_buffer_device_address = true;
};

struct ParticleSimulationBufferView final {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize size_bytes = 0U;
    VkDeviceAddress device_address = 0U;
    std::uint32_t element_capacity = 0U;
};

struct ParticleSimulationPrepareDesc final {
    ecs::ParticleSimulationMode requested_mode = ecs::ParticleSimulationMode::hybrid_gpu;
    std::uint32_t particle_capacity = 0U;
    std::uint32_t visible_particle_capacity = 0U;
    std::uint32_t spawn_packet_capacity = 0U;
    std::uint32_t indirect_command_capacity = 0U;
    std::uint32_t sort_key_capacity = 0U;
    std::uint32_t draw_instance_stride_bytes = 0U;
    bool require_draw_instance_buffer = false;
    bool require_sort_buffers = false;
    bool require_buffer_device_address = false;
    bool allow_cpu_fallback = true;
};

struct ParticleSimulationFrameResources final {
    std::uint32_t frame_index = 0U;
    std::uint32_t state_read_index = 0U;
    std::uint32_t state_write_index = 1U;
    ParticleSimulationResolvedPath resolved_path = ParticleSimulationResolvedPath::cpu;
    ParticleSimulationBufferView state_read{};
    ParticleSimulationBufferView state_write{};
    ParticleSimulationBufferView alive_list{};
    ParticleSimulationBufferView dead_list{};
    ParticleSimulationBufferView spawn_packets{};
    ParticleSimulationBufferView draw_instances{};
    ParticleSimulationBufferView indirect_commands{};
    ParticleSimulationBufferView sort_keys{};
    ParticleSimulationBufferView sort_indices{};
    bool fell_back_to_cpu = false;
    bool using_gpu_buffers = false;
};

struct ParticleSimulationHostStats final {
    std::uint32_t begin_frame_count = 0U;
    std::uint32_t prepared_frame_count = 0U;
    std::uint32_t cpu_prepare_count = 0U;
    std::uint32_t gpu_prepare_count = 0U;
    std::uint32_t hybrid_prepare_count = 0U;
    std::uint32_t fallback_to_cpu_count = 0U;
    std::uint32_t update_dispatch_count = 0U;
    std::uint32_t state_buffer_resize_count = 0U;
    std::uint32_t scratch_buffer_resize_count = 0U;
    std::uint32_t retired_buffer_count = 0U;
    std::uint32_t state_particle_capacity = 0U;
    std::uint32_t visible_particle_capacity = 0U;
    std::uint32_t spawn_packet_capacity = 0U;
    std::uint32_t indirect_command_capacity = 0U;
    std::uint32_t sort_key_capacity = 0U;
    std::uint32_t revision = 0U;
    std::uint32_t gpu_build_prepare_count = 0U;
    std::uint32_t gpu_build_dispatch_count = 0U;
    std::uint32_t state_upload_count = 0U;
    std::uint32_t spawn_packet_upload_count = 0U;
    std::uint32_t indirect_upload_count = 0U;
    std::uint32_t sort_dispatch_count = 0U;
    std::uint64_t allocated_bytes = 0U;
    std::uint64_t gpu_build_uploaded_bytes = 0U;
};

struct ParticleSimulationGpuBuildResult final {
    ParticleSimulationFrameResources resources{};
    ParticleUploadRange state_upload{};
    ParticleUploadRange spawn_upload{};
    ParticleUploadRange indirect_upload{};
    std::uint32_t state_record_count = 0U;
    std::uint32_t indirect_command_count = 0U;
    bool used_gpu_build = false;
    bool cache_reused = false;
};

class ParticleSimulationHost final {
public:
    ParticleSimulationHost() = default;
    ~ParticleSimulationHost() = default;

    ParticleSimulationHost(const ParticleSimulationHost&) = delete;
    ParticleSimulationHost& operator=(const ParticleSimulationHost&) = delete;
    ParticleSimulationHost(ParticleSimulationHost&&) = delete;
    ParticleSimulationHost& operator=(ParticleSimulationHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const ParticleSimulationHostCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_,
                    std::uint32_t frame_index_,
                    std::uint64_t last_submitted_value_ = 0U,
                    std::uint64_t completed_submit_value_ = 0U);

    [[nodiscard]] ParticleSimulationFrameResources PrepareFrameResources(
        VulkanContext& context_,
        std::uint32_t frame_index_,
        const ParticleSimulationPrepareDesc& prepare_desc_);

    [[nodiscard]] ParticleSimulationGpuBuildResult PrepareGpuBuild2D(
        VulkanContext& context_,
        render::UploadHost& upload_host_,
        render::DescriptorHost& descriptor_host_,
        render::PipelineHost& pipeline_host_,
        std::uint32_t frame_index_,
        const ParticleSimulationFrameResources& frame_resources_,
        const ecs::Particle<ecs::Dim2>* particles_,
        const ecs::ParticleEmitter<ecs::Dim2>* emitters_,
        const ecs::Transform<ecs::Dim2>* transforms_,
        std::uint32_t component_count_,
        const ecs::ParticleRuntimeBuildConfig& build_config_,
        bool cpu_seeded_this_frame_,
        ecs::Particle2DRuntimeScratch& runtime_scratch_,
        const ecs::ParticleRuntimeBuildStats& runtime_stats_);

    [[nodiscard]] ParticleSimulationGpuBuildResult PrepareGpuBuild3D(
        VulkanContext& context_,
        render::UploadHost& upload_host_,
        render::DescriptorHost& descriptor_host_,
        render::PipelineHost& pipeline_host_,
        std::uint32_t frame_index_,
        const ParticleSimulationFrameResources& frame_resources_,
        const ecs::Particle<ecs::Dim3>* particles_,
        const ecs::ParticleEmitter<ecs::Dim3>* emitters_,
        const ecs::Transform<ecs::Dim3>* transforms_,
        std::uint32_t component_count_,
        const ecs::ParticleRuntimeBuildConfig& build_config_,
        bool cpu_seeded_this_frame_,
        ecs::Particle3DRuntimeScratch& runtime_scratch_,
        const ecs::ParticleRuntimeBuildStats& runtime_stats_);

    void RecordBuild2D(VulkanContext& context_,
                       render::PipelineHost& pipeline_host_,
                       std::uint32_t frame_index_,
                       VkCommandBuffer command_buffer_);
    void RecordBuild3D(VulkanContext& context_,
                       render::PipelineHost& pipeline_host_,
                       std::uint32_t frame_index_,
                       const ecs::Float3& sort_origin_,
                       const ecs::Float3& sort_direction_,
                       VkCommandBuffer command_buffer_);

    [[nodiscard]] static ParticleSimulationResolvedPath ResolveSimulationPath(
        ecs::ParticleSimulationMode requested_mode_,
        const ParticleSimulationHostCapabilities& capabilities_) noexcept;
    [[nodiscard]] static ParticleSimulationHostCapabilities QueryCapabilities(
        const VulkanContext& context_) noexcept;
    [[nodiscard]] bool NeedsCpuSeed2D(std::uint32_t frame_index_) const noexcept;
    [[nodiscard]] bool NeedsCpuSeed3D(std::uint32_t frame_index_) const noexcept;
    [[nodiscard]] bool HasPersistentState2D() const noexcept;
    [[nodiscard]] bool HasPersistentState3D() const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] const ParticleSimulationHostCreateInfo& CreateInfo() const noexcept;
    [[nodiscard]] const ParticleSimulationHostStats& Stats() const noexcept;
    [[nodiscard]] const ParticleSimulationHostCapabilities& Capabilities() const noexcept;

private:
    struct BufferSlot final {
        resource::BufferResource resource{};
        VkDeviceSize capacity_bytes = 0U;
        std::uint32_t element_capacity = 0U;
    };

    struct FrameState final {
        struct BuildPathState final {
            BufferSlot state_buffers[2]{};
            BufferSlot alive_list{};
            BufferSlot dead_list{};
            BufferSlot spawn_packets{};
            BufferSlot draw_instances{};
            BufferSlot indirect_commands{};
            BufferSlot sort_keys{};
            BufferSlot sort_indices{};
            VkDescriptorSet update_descriptor_set = VK_NULL_HANDLE;
            VkDescriptorSet build_descriptor_set = VK_NULL_HANDLE;
            std::uint32_t state_record_count = 0U;
            std::uint32_t spawn_packet_count = 0U;
            std::uint32_t indirect_command_count = 0U;
            std::uint32_t dispatch_group_count = 0U;
            std::uint32_t draw_instance_stride_bytes = 0U;
            std::uint32_t state_read_index = 0U;
            std::uint32_t state_write_index = 1U;
            bool gpu_sort_enabled = false;
            float update_delta_time_s = 0.0F;
            std::uint64_t prepared_revision = 0U;
            std::uint64_t state_uploaded_revision = 0U;
            std::uint64_t spawn_uploaded_revision = 0U;
            std::uint64_t indirect_uploaded_revision = 0U;
            VkDeviceSize state_uploaded_size_bytes = 0U;
            VkDeviceSize spawn_uploaded_size_bytes = 0U;
            VkDeviceSize indirect_uploaded_size_bytes = 0U;
        };

        BuildPathState build_2d{};
        BuildPathState build_3d{};
    };

    struct GpuBuildPrepareScratch final {
        ParticleSimulationMcVector<ParticleGpuStateRecord> state_records{};
        ParticleSimulationMcVector<ParticleGpuComponentRange> component_ranges{};
        ParticleSimulationMcVector<ParticleGpuSpawnPacket> spawn_packets{};
        ParticleSimulationMcVector<ParticleGpuIndirectCommand> indirect_commands{};
    };

    [[nodiscard]] static VkDeviceSize NextPow2(VkDeviceSize value_) noexcept;
    [[nodiscard]] static VkDeviceSize BufferSizeForElements(std::uint32_t element_capacity_,
                                                            VkDeviceSize element_size_) noexcept;
    [[nodiscard]] VkBufferUsageFlags BuildBufferUsageFlags(VkBufferUsageFlags base_usage_) const noexcept;
    [[nodiscard]] VkDeviceAddress QueryBufferDeviceAddress(VulkanContext& context_,
                                                           const resource::BufferResource& resource_) const noexcept;
    [[nodiscard]] ParticleSimulationBufferView MakeBufferView(VulkanContext& context_,
                                                              const BufferSlot& slot_) const noexcept;

    void EnsureBufferCapacity(VulkanContext& context_,
                              BufferSlot& slot_,
                              std::uint32_t required_element_capacity_,
                              VkDeviceSize element_size_,
                              VkBufferUsageFlags usage_,
                              std::uint32_t& resize_counter_);
    void RetireBuffer(BufferSlot& slot_,
                      std::uint64_t retire_value_);
    void CollectRetiredBuffers(VulkanContext& context_,
                               std::uint64_t completed_submit_value_);
    void DestroyRetiredBuffers(VulkanContext& context_) noexcept;
    void DestroyFrameState(VulkanContext& context_,
                           FrameState& frame_) noexcept;
    void EnsureGpuBuildObjects(VulkanContext& context_,
                               render::DescriptorHost& descriptor_host_,
                               render::PipelineHost& pipeline_host_);
    [[nodiscard]] std::uint64_t ComputeRetireValue() const noexcept;
    [[nodiscard]] FrameState& FrameAt(std::uint32_t frame_index_);
    [[nodiscard]] const FrameState& FrameAt(std::uint32_t frame_index_) const;

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    ParticleSimulationHostCreateInfo create_info_cache{};
    ParticleSimulationHostCapabilities capabilities{};
    ParticleSimulationMcVector<FrameState> frames{};
    render::RetireBus<resource::BufferResource> retired_buffers{};
    GpuBuildPrepareScratch gpu_build_scratch_2d{};
    GpuBuildPrepareScratch gpu_build_scratch_3d{};
    render::DescriptorSetLayoutId gpu_build_descriptor_layout_id{};
    render::PipelineLayoutId gpu_build_pipeline_layout_id{};
    render::ShaderModuleId gpu_update_shader_2d_id{};
    render::ShaderModuleId gpu_update_shader_3d_id{};
    render::ShaderModuleId gpu_build_shader_2d_id{};
    render::ShaderModuleId gpu_build_shader_3d_id{};
    render::ShaderModuleId gpu_sort_shader_3d_id{};
    render::ComputePipelineId gpu_update_pipeline_2d_id{};
    render::ComputePipelineId gpu_update_pipeline_3d_id{};
    render::ComputePipelineId gpu_build_pipeline_2d_id{};
    render::ComputePipelineId gpu_build_pipeline_3d_id{};
    render::ComputePipelineId gpu_sort_pipeline_3d_id{};
    ParticleSimulationHostStats stats{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    std::uint64_t simulation_epoch = 0U;
    std::uint64_t last_begin_frame_cookie = ~std::uint64_t{0};
    bool initialized = false;
};

static_assert(std::is_standard_layout_v<ParticleGpuStateRecord>);
static_assert(std::is_trivial_v<ParticleGpuStateRecord>);
static_assert(std::is_standard_layout_v<ParticleGpuComponentRange>);
static_assert(std::is_trivial_v<ParticleGpuComponentRange>);
static_assert(std::is_standard_layout_v<ParticleGpuSpawnPacket>);
static_assert(std::is_trivial_v<ParticleGpuSpawnPacket>);
static_assert(std::is_standard_layout_v<ParticleGpuSortKeyRecord>);
static_assert(std::is_trivial_v<ParticleGpuSortKeyRecord>);
static_assert(std::is_standard_layout_v<ParticleSimulationBufferView>);
static_assert(std::is_trivially_copyable_v<ParticleSimulationBufferView>);
static_assert(std::is_standard_layout_v<ParticleSimulationFrameResources>);
static_assert(std::is_trivially_copyable_v<ParticleSimulationFrameResources>);

} // namespace vr::particle
