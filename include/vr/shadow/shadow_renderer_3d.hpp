#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/bounds_component.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/geometry_component.hpp"
#include "vr/ecs/component/shadow_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/geometry/geometry_skeletal_palette_builder.hpp"
#include "vr/geometry/geometry_resource_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/render/shadow_frame_coordinator.hpp"
#include "vr/shadow/shadow_atlas_host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::shadow {

template<typename T>
using ShadowRenderer3DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ShadowRenderer3DCreateInfo final {
    ecs::ShadowRuntimeBuildConfig runtime_build{};
    ecs::ShadowCasterBuildConfig caster_build{};
    ShadowAtlasHostCreateInfo atlas{};
    std::uint32_t reserve_shadow_count = 512U;
    std::uint32_t reserve_caster_count = 4096U;
    std::uint32_t reserve_atlas_request_count = 32U;
    VkFormat preferred_depth_format = VK_FORMAT_D32_SFLOAT;
    bool clear_atlas_each_frame = true;
    bool compile_required_pipelines_in_prepare = true;
    bool prewarm_common_pipelines = true;
};

struct ShadowRenderer3DStats final {
    std::uint32_t shadow_component_count = 0U;
    std::uint32_t geometry_component_count = 0U;
    std::uint32_t shadow_view_count = 0U;
    std::uint32_t shadow_runtime_updated_count = 0U;
    std::uint32_t shadow_caster_index_count = 0U;
    std::uint32_t shadow_caster_header_count = 0U;
    std::uint32_t atlas_namespace_count = 0U;
    std::uint32_t atlas_layer_draw_pass_count = 0U;
    std::uint32_t atlas_transition_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t draw_indexed_call_count = 0U;
    std::uint32_t skipped_no_mesh_count = 0U;
    std::uint32_t skipped_invalid_submesh_count = 0U;
    std::uint32_t skipped_no_shadow_flag_count = 0U;
    std::uint32_t skipped_out_of_range_count = 0U;
    std::uint32_t pipeline_bind_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t pipeline_compile_count = 0U;
    std::uint32_t reused_pipeline_count = 0U;
    std::uint32_t morph_animated_draw_call_count = 0U;
    std::uint32_t skeletal_palette_component_count = 0U;
    std::uint32_t skeletal_palette_matrix_count = 0U;
    bool runtime_cache_reused = false;
    bool runtime_transform_only_update = false;
};

struct ShadowDeformComponentGpu final {
    float deform_param0[4]{};
    float deform_param1[4]{};
};

static_assert(sizeof(ShadowDeformComponentGpu) == 32U);

class ShadowRenderer3D final {
public:
    ShadowRenderer3D() = default;
    ~ShadowRenderer3D() = default;

    ShadowRenderer3D(const ShadowRenderer3D&) = delete;
    ShadowRenderer3D& operator=(const ShadowRenderer3D&) = delete;

    ShadowRenderer3D(ShadowRenderer3D&&) = delete;
    ShadowRenderer3D& operator=(ShadowRenderer3D&&) = delete;

    void Initialize(const ShadowRenderer3DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetHosts(geometry::GeometryResourceHost* geometry_resource_host_) noexcept;
    void SetSceneData(ecs::Shadow<ecs::Dim3>* shadow_components_,
                      ecs::Transform<ecs::Dim3>* shadow_transforms_,
                      std::uint32_t shadow_component_count_,
                      ecs::Camera<ecs::Dim3>* camera_component_,
                      ecs::Bounds<ecs::Dim3>* caster_bounds_) noexcept;
    void SetGeometryData(ecs::Geometry<ecs::Dim3>* geometry_components_,
                         ecs::Transform<ecs::Dim3>* geometry_transforms_,
                         std::uint32_t geometry_component_count_) noexcept;
    void SetAnimationOutputs(const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
                             std::uint32_t skeletal_output_count_,
                             const ecs::VertexDeformOutputState* vertex_deform_outputs_,
                             std::uint32_t vertex_deform_output_count_,
                             const ecs::MorphWeightOutputState* morph_outputs_,
                             std::uint32_t morph_output_count_,
                             const ecs::FrameSequenceOutputState* frame_sequence_outputs_,
                             std::uint32_t frame_sequence_output_count_) noexcept;
    void SetShadowDirtyHint(const std::uint32_t* dirty_component_indices_,
                            std::uint32_t dirty_component_count_) noexcept;
    void SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                               std::uint32_t dirty_component_count_) noexcept;

    void PrepareFrame(const render::ShadowRenderer3DPrepareView& prepare_view_);
    void Record(const render::FrameRecordContext& record_context_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const ShadowRenderer3DStats& Stats() const noexcept;
    [[nodiscard]] const ShadowAtlasHost& AtlasHost() const noexcept;
    [[nodiscard]] ShadowAtlasHost& AtlasHostMutable() noexcept;
    [[nodiscard]] const render::ShadowFrameCoordinator<ecs::Dim3>& FrameCoordinator() const noexcept;
    [[nodiscard]] render::ShadowFrameCoordinator<ecs::Dim3>& FrameCoordinatorMutable() noexcept;

private:
    struct PushConstants final {
        ecs::Matrix4x4 view_projection{};
        float world_affine[12]{};
        float morph_weights[4]{};
    };

    static_assert(sizeof(PushConstants) == 128U);

    enum class TopologyMode : std::uint8_t {
        triangles = 0U,
        lines = 1U,
        points = 2U,
        count = 3U
    };

    enum class CullMode : std::uint8_t {
        back = 0U,
        none = 1U,
        count = 2U
    };

    enum class DepthMode : std::uint8_t {
        forward = 0U,
        reverse_z = 1U,
        count = 2U
    };

    struct PipelineSelection final {
        TopologyMode topology = TopologyMode::triangles;
        CullMode cull = CullMode::back;
        DepthMode depth = DepthMode::forward;
    };

    struct AtlasRequestAggregate final {
        std::uint32_t namespace_id = 0U;
        std::uint16_t width = 1U;
        std::uint16_t height = 1U;
        std::uint16_t layer_count = 1U;
    };

    [[nodiscard]] static bool IsDepthFormatSupported(VulkanContext& context_,
                                                     VkFormat format_) noexcept;
    [[nodiscard]] static bool DepthFormatHasStencil(VkFormat format_) noexcept;
    [[nodiscard]] static VkImageAspectFlags DepthAspectMask(VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_,
                                                     VkFormat preferred_format_);
    [[nodiscard]] static std::size_t TopologyModeIndex(TopologyMode mode_) noexcept;
    [[nodiscard]] static std::size_t CullModeIndex(CullMode mode_) noexcept;
    [[nodiscard]] static std::size_t DepthModeIndex(DepthMode mode_) noexcept;
    [[nodiscard]] static TopologyMode ResolveTopologyMode(ecs::Geometry3DTopology topology_) noexcept;
    [[nodiscard]] static CullMode ResolveCullMode(const ecs::Geometry<ecs::Dim3>& geometry_component_) noexcept;
    [[nodiscard]] static DepthMode ResolveDepthMode(const ecs::ShadowViewGpuRecord& view_record_) noexcept;
    [[nodiscard]] static ecs::Matrix4x4 ComposeEffectiveWorldMatrix(
        const ecs::Matrix4x4& base_world_matrix_,
        const ecs::Geometry<ecs::Dim3>& geometry_component_,
        std::uint32_t component_index_,
        const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
        std::uint32_t skeletal_output_count_) noexcept;
    [[nodiscard]] static std::uint32_t ResolveAnimatedSubmeshIndex(
        const ecs::Geometry<ecs::Dim3>& geometry_component_,
        std::uint32_t component_index_,
        const ecs::FrameSequenceOutputState* frame_sequence_outputs_,
        std::uint32_t frame_sequence_output_count_) noexcept;
    [[nodiscard]] static std::size_t LowerBoundAtlasRequestIndex(
        const ShadowRenderer3DMcVector<AtlasRequestAggregate>& entries_,
        std::uint32_t namespace_id_) noexcept;

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat depth_format_);
    void EnsureDescriptorObjects(VulkanContext& context_,
                                 render::DescriptorHost& descriptor_host_);
    [[nodiscard]] render::GraphicsPipelineId EnsureGraphicsPipeline(VulkanContext& context_,
                                                                    render::PipelineHost& pipeline_host_,
                                                                    VkFormat depth_format_,
                                                                    TopologyMode topology_mode_,
                                                                    CullMode cull_mode_,
                                                                    DepthMode depth_mode_);
    void PrewarmCommonPipelines(VulkanContext& context_,
                                render::PipelineHost& pipeline_host_,
                                VkFormat depth_format_);
    void CompileRequiredPipelinesForCurrentFrame(VulkanContext& context_,
                                                 render::PipelineHost& pipeline_host_,
                                                 VkFormat depth_format_);
    void BuildAtlasRequests();
    void EnsureStorageBufferCapacity(resource::BufferResource& buffer_,
                                     VkDeviceSize required_bytes_);
    void PrepareAnimationResourcesForFrame();
    void RecordAtlasTransition(VkCommandBuffer command_buffer_,
                               const ShadowAtlasHost::AtlasRecord& atlas_record_,
                               VkImageLayout old_layout_,
                               VkImageLayout new_layout_);
    void RecordOneAtlas(const render::FrameRecordContext& record_context_,
                        ShadowAtlasHost::AtlasRecord& atlas_record_);

private:
    ShadowRenderer3DCreateInfo create_info_cache{};
    ShadowRenderer3DStats stats{};

    ecs::Shadow<ecs::Dim3>* shadow_components = nullptr;
    ecs::Transform<ecs::Dim3>* shadow_transforms = nullptr;
    std::uint32_t shadow_component_count = 0U;
    ecs::Camera<ecs::Dim3>* camera_component = nullptr;
    ecs::Bounds<ecs::Dim3>* caster_bounds = nullptr;
    ecs::Geometry<ecs::Dim3>* geometry_components = nullptr;
    ecs::Transform<ecs::Dim3>* geometry_transforms = nullptr;
    std::uint32_t geometry_component_count = 0U;
    const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs = nullptr;
    std::uint32_t skeletal_output_count = 0U;
    const ecs::VertexDeformOutputState* vertex_deform_outputs = nullptr;
    std::uint32_t vertex_deform_output_count = 0U;
    const ecs::MorphWeightOutputState* morph_outputs = nullptr;
    std::uint32_t morph_output_count = 0U;
    const ecs::FrameSequenceOutputState* frame_sequence_outputs = nullptr;
    std::uint32_t frame_sequence_output_count = 0U;

    geometry::GeometryResourceHost* geometry_resource_host = nullptr;

    VulkanContext* context = nullptr;
    render::DescriptorHost* descriptor_host = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;

    render::ShadowFrameCoordinator<ecs::Dim3> frame_coordinator{};
    render::ShadowPrepareStageResult<ecs::Dim3> last_prepare_result{};
    ShadowAtlasHost atlas_host{};
    ShadowRenderer3DMcVector<AtlasRequestAggregate> atlas_requests{};

    render::PipelineLayoutId pipeline_layout_id{};
    render::DescriptorSetLayoutId descriptor_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    std::array<std::array<std::array<render::GraphicsPipelineId,
                                     static_cast<std::size_t>(CullMode::count)>,
                          static_cast<std::size_t>(TopologyMode::count)>,
               static_cast<std::size_t>(DepthMode::count)> pipeline_ids{};
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;
    VkFormat resolved_depth_format = VK_FORMAT_UNDEFINED;

    struct FrameAnimationResources final {
        resource::BufferResource skeletal_components{};
        resource::BufferResource skeletal_matrices{};
        resource::BufferResource deform_components{};
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        std::uint32_t skeletal_component_count = 0U;
        std::uint32_t skeletal_matrix_count = 0U;
        std::uint32_t deform_component_count = 0U;
        std::uint64_t skeletal_signature = 0U;
        std::uint64_t descriptor_signature = 0U;
    };

    ShadowRenderer3DMcVector<FrameAnimationResources> frame_animation_resources{};
    ShadowRenderer3DMcVector<geometry::GeometrySkeletalComponentGpu> skeletal_component_scratch{};
    ShadowRenderer3DMcVector<geometry::GeometrySkeletalMatrixGpu> skeletal_matrix_scratch{};
    ShadowRenderer3DMcVector<ShadowDeformComponentGpu> deform_component_scratch{};
    std::uint32_t active_frame_index = 0U;

    bool initialized = false;
};

} // namespace vr::shadow

