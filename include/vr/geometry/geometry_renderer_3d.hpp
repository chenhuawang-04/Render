#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/bounds_component.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/system/culling_system.hpp"
#include "vr/ecs/system/geometry_runtime_system.hpp"
#include "vr/ecs/system/light_culling_system.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/geometry/geometry_material_gpu.hpp"
#include "vr/geometry/geometry_material_host.hpp"
#include "vr/geometry/geometry_resource_host.hpp"
#include "vr/geometry/geometry_skeletal_palette_builder.hpp"
#include "vr/geometry/geometry_upload_host.hpp"
#include "vr/light/light_shadow_upload_host.hpp"
#include "vr/render/appearance_prepare_bridge.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/light_shadow_link_coordinator.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/scene_render_stage.hpp"
#include "vr/render/shadow_atlas_binding_coordinator.hpp"
#include "vr/render/shadow_frame_coordinator.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/shadow/shadow_atlas_host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::asset {
class TextureHost;
}

namespace vr::render {
struct GeometryRenderer3DPrepareView;
struct FrameRecordContext;
class IblHost;
class BindlessResourceSystem;
}

namespace vr::geometry {

template<typename T>
using GeometryRenderer3DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct GeometryRenderer3DCreateInfo {
    ecs::Geometry3DRuntimeBuildConfig runtime_build{};
    std::uint32_t reserve_component_count = 4096U;
    std::uint32_t reserve_instance_count = 8192U;
    std::uint32_t reserve_material_set_count = 512U;
    bool enable_depth = true;
    VkFormat preferred_depth_format = VK_FORMAT_D32_SFLOAT;
    bool clear_depth = true;
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
    float directional_light_x = 0.5F;
    float directional_light_y = -1.0F;
    float directional_light_z = 0.35F;
    float directional_light_intensity = 1.0F;
    bool compile_required_pipelines_in_prepare = true;
    bool prewarm_common_pipelines = true;
    bool prewarm_depth_read_variant = true;
    bool prewarm_double_sided_variant = true;
    bool prewarm_line_and_point_variants = false;
    bool enable_light_shadow = true;
    std::uint32_t max_fragment_lights = 64U;
    ecs::LightCullingBuildConfig<ecs::Dim3> light_culling_config =
        ecs::LightCullingSystem<ecs::Dim3>::DefaultBuildConfig();
};

struct GeometryRenderer3DStats {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t appearance_component_count = 0U;
    std::uint32_t appearance_visible_count = 0U;
    std::uint32_t appearance_updated_record_count = 0U;
    std::uint32_t appearance_link_scanned_count = 0U;
    std::uint32_t appearance_link_updated_count = 0U;
    std::uint32_t instance_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t opaque_draw_call_count = 0U;
    std::uint32_t transparent_draw_call_count = 0U;
    std::uint32_t stage_filtered_batch_count = 0U;
    std::uint32_t empty_stage_pass_count = 0U;
    std::uint32_t skipped_batch_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
    std::uint32_t shadow_cast_batch_count = 0U;
    std::uint32_t skeletal_animated_instance_count = 0U;
    std::uint32_t vertex_deform_animated_instance_count = 0U;
    std::uint32_t morph_animated_instance_count = 0U;
    std::uint32_t frame_sequence_animated_instance_count = 0U;
    std::uint32_t skeletal_palette_component_count = 0U;
    std::uint32_t skeletal_palette_matrix_count = 0U;
    std::uint32_t skeletal_palette_upload_count = 0U;
    std::uint32_t uploaded_instance_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t material_push_constant_update_count = 0U;
    std::uint32_t material_resolve_cache_hit_count = 0U;
    std::uint32_t material_resolve_cache_miss_count = 0U;
    std::uint32_t material_set_count = 0U;
    std::uint32_t material_resolve_cache_entry_count = 0U;
    std::uint32_t prewarmed_pipeline_count = 0U;
    std::uint32_t prepare_compiled_pipeline_count = 0U;
    std::uint32_t culling_input_count = 0U;
    std::uint32_t culling_visible_count = 0U;
    std::uint32_t culling_culled_count = 0U;
    std::uint32_t culling_mask_reject_count = 0U;
    std::uint32_t culling_frustum_reject_count = 0U;
    std::uint32_t culling_invalid_bounds_count = 0U;
    std::uint32_t culling_plane_test_count = 0U;
    std::uint32_t light_count = 0U;
    std::uint32_t visible_light_count = 0U;
    std::uint32_t shadow_view_count = 0U;
    std::uint32_t light_upload_range_count = 0U;
    std::uint32_t shadow_view_upload_range_count = 0U;
    std::uint32_t light_cluster_count = 0U;
    std::uint32_t light_cluster_index_count = 0U;
    std::uint32_t light_buffer_upload_count = 0U;
    std::uint32_t light_descriptor_set_bind_count = 0U;
    std::uint32_t ibl_descriptor_set_bind_count = 0U;
    std::uint32_t light_descriptor_set_reuse_hit_count = 0U;
    std::uint32_t light_shadow_atlas_binding_cache_hit_count = 0U;
    std::uint32_t light_shadow_linked_count = 0U;
    std::uint32_t light_shadow_link_cache_hit_count = 0U;
    std::uint32_t light_shadow_namespace_drop_count = 0U;
    std::uint32_t light_shadow_unmapped_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    bool cache_reused = false;
    bool appearance_cache_reused = false;
    bool transform_only_update = false;
    bool used_bounds_culling = false;
};

class GeometryRenderer3D final {
public:
    GeometryRenderer3D() = default;
    ~GeometryRenderer3D() = default;

    GeometryRenderer3D(const GeometryRenderer3D&) = delete;
    GeometryRenderer3D& operator=(const GeometryRenderer3D&) = delete;

    GeometryRenderer3D(GeometryRenderer3D&&) = delete;
    GeometryRenderer3D& operator=(GeometryRenderer3D&&) = delete;

    void Initialize(const GeometryRenderer3DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetHosts(GeometryResourceHost* resource_host_,
                  GeometryUploadHost* upload_host_) noexcept;
    void SetMaterialHosts(GeometryMaterialHost* material_host_,
                          GeometryImageHost* image_host_) noexcept;
    void SetSceneData(ecs::Geometry<ecs::Dim3>* geometry_components_,
                      ecs::Transform<ecs::Dim3>* transforms_,
                      std::uint32_t component_count_,
                      ecs::Camera<ecs::Dim3>* camera_component_,
                      ecs::Transform<ecs::Dim3>* camera_transform_,
                      ecs::Bounds<ecs::Dim3>* bounds_components_ = nullptr) noexcept;
    void SetAppearanceData(ecs::Appearance<ecs::Dim3>* appearance_components_,
                           std::uint32_t appearance_component_count_) noexcept;
    void SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                std::uint32_t dirty_component_count_) noexcept;
    void SetAnimationOutputs(const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
                             std::uint32_t skeletal_output_count_,
                             const ecs::VertexDeformOutputState* vertex_deform_outputs_,
                             std::uint32_t vertex_deform_output_count_,
                             const ecs::MorphWeightOutputState* morph_outputs_,
                             std::uint32_t morph_output_count_,
                             const ecs::FrameSequenceOutputState* frame_sequence_outputs_,
                             std::uint32_t frame_sequence_output_count_) noexcept;
    void SetAppearanceCoordinator(render::AppearanceFrameCoordinator<ecs::Dim3>* appearance_frame_coordinator_) noexcept;
    void SetLightFrameCoordinator(render::LightFrameCoordinator<ecs::Dim3>* light_frame_coordinator_) noexcept;
    void SetLightShadowLinkCoordinator(render::LightShadowLinkCoordinator3D* light_shadow_link_coordinator_) noexcept;
    void SetShadowAtlasBindingCoordinator(render::ShadowAtlasBindingCoordinator* shadow_atlas_binding_coordinator_) noexcept;
    void SetShadowFrameCoordinator(render::ShadowFrameCoordinator<ecs::Dim3>* shadow_frame_coordinator_) noexcept;
    void SetShadowAtlasHost(shadow::ShadowAtlasHost* shadow_atlas_host_) noexcept;
    void SetOutputTargetConfig(const render::RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;
    void SetDepthTargetConfig(const render::RenderTargetDepthOutputConfig& depth_output_target_config_) noexcept;
    void ResetDepthTargetConfig() noexcept;

    void PrepareFrame(const render::GeometryRenderer3DPrepareView& prepare_view_);
    void Record(const render::FrameRecordContext& record_context_);
    void RecordSceneStage(const render::FrameRecordContext& record_context_,
                          render::SceneRenderStage stage_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GeometryRenderer3DStats& Stats() const noexcept;

private:
    struct FramePushConstants final {
        ecs::Matrix4x4 view_projection;
        float directional_light_x;
        float directional_light_y;
        float directional_light_z;
        float directional_light_intensity;
        float camera_position_x;
        float camera_position_y;
        float camera_position_z;
        float padding0;
    };

    struct MaterialPushConstants final {
        float uv_scale_u;
        float uv_scale_v;
        float uv_bias_u;
        float uv_bias_v;
        std::uint32_t flags;
        float alpha_cutoff;
        std::uint32_t texture_slot;
        std::uint32_t sampler_slot;
    };

    struct PushConstants final {
        FramePushConstants frame{};
        MaterialPushConstants material{};
    };

    static_assert(sizeof(FramePushConstants) == 96U);
    static_assert(sizeof(MaterialPushConstants) == 32U);
    static_assert(sizeof(PushConstants) == 128U);

    struct alignas(16) LightingParamsGpu final {
        float camera_position_x;
        float camera_position_y;
        float camera_position_z;
        float light_count;

        float camera_forward_x;
        float camera_forward_y;
        float camera_forward_z;
        float max_fragment_lights;

        std::uint32_t cluster_count_x;
        std::uint32_t cluster_count_y;
        std::uint32_t cluster_count_z;
        std::uint32_t reverse_z;

        float near_plane;
        float far_plane;
        float z_slice_scale;
        float z_slice_bias;

        float framebuffer_width;
        float framebuffer_height;
        float shadow_view_count;
        float reserved0;

        std::uint32_t shadow_atlas_texture_slot;
        std::uint32_t shadow_atlas_sampler_slot;
        std::uint32_t reserved1;
        std::uint32_t reserved2;
    };

    struct FrameLightingResources final {
        light::LightShadowBufferRange light_records{};
        light::LightShadowBufferRange cluster_headers{};
        light::LightShadowBufferRange cluster_indices{};
        light::LightShadowBufferRange shadow_views{};
        light::LightShadowBufferRange lighting_uniform{};
        resource::BufferResource material_records{};
        resource::BufferResource skeletal_components{};
        resource::BufferResource skeletal_matrices{};
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        std::uint32_t shadow_namespace_id = 0U;
        std::uint32_t material_record_count = 0U;
        std::uint32_t skeletal_component_count = 0U;
        std::uint32_t skeletal_matrix_count = 0U;
        std::uint64_t material_content_revision = 0U;
        std::uint64_t material_bindless_revision = 0U;
        std::uint32_t material_texture_host_revision = 0U;
        std::uint64_t upload_signature = 0U;
        std::uint64_t descriptor_payload_signature = 0U;
        std::uint64_t descriptor_buffer_signature = 0U;
        std::uint64_t descriptor_image_signature = 0U;
        std::uint64_t descriptor_set_signature = 0U;
    };
    static_assert(sizeof(LightingParamsGpu) == 96U);
    static_assert(offsetof(LightingParamsGpu, camera_position_x) == 0U);
    static_assert(offsetof(LightingParamsGpu, camera_forward_x) == 16U);
    static_assert(offsetof(LightingParamsGpu, cluster_count_x) == 32U);
    static_assert(offsetof(LightingParamsGpu, near_plane) == 48U);
    static_assert(offsetof(LightingParamsGpu, framebuffer_width) == 64U);
    static_assert(offsetof(LightingParamsGpu, shadow_atlas_texture_slot) == 80U);

    enum class PipelineMode : std::uint8_t {
        no_depth = 0U,
        depth_read = 1U,
        depth_read_write = 2U,
        count = 3U
    };

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

    enum class BlendMode : std::uint8_t {
        opaque = 0U,
        alpha = 1U,
        additive = 2U,
        multiply = 3U,
        premultiplied_alpha = 4U,
        screen = 5U,
        count = 6U
    };

    struct RetiredDepthImage final {
        resource::ImageResource resource{};
        std::uint64_t retire_value = 0U;
    };

    struct ResolvedMaterialEntry final {
        std::uint32_t material_id = 0U;
        std::uint32_t material_revision = 0U;
        std::uint32_t image_id = 0U;
        std::uint32_t image_revision = 0U;
        MaterialPushConstants material_push_constants{};
    };

    [[nodiscard]] static bool IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept;
    [[nodiscard]] static bool DepthFormatHasStencil(VkFormat format_) noexcept;
    [[nodiscard]] static VkImageAspectFlags DepthImageAspectMask(VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_);
    [[nodiscard]] static std::size_t PipelineModeIndex(PipelineMode mode_) noexcept;
    [[nodiscard]] static std::size_t TopologyModeIndex(TopologyMode mode_) noexcept;
    [[nodiscard]] static std::size_t CullModeIndex(CullMode mode_) noexcept;
    [[nodiscard]] static std::size_t BlendModeIndex(BlendMode mode_) noexcept;
    [[nodiscard]] static std::size_t LowerBoundResolvedMaterialIndex(
        const GeometryRenderer3DMcVector<ResolvedMaterialEntry>& entries_,
        std::uint32_t material_id_) noexcept;
    static void ApplyFallbackMaterialFactorsToInstance(
        ecs::Geometry3DGpuInstance& instance_,
        const ecs::Geometry<ecs::Dim3>* component_,
        const GeometryMaterialDesc* material_desc_) noexcept;
    [[nodiscard]] std::uint64_t ApplyMaterialFactorOverrides();
    [[nodiscard]] static PipelineMode ResolvePipelineMode(const ecs::Geometry3DDrawBatch& batch_,
                                                          bool use_depth_) noexcept;
    [[nodiscard]] static TopologyMode ResolveTopologyMode(VkPrimitiveTopology mesh_topology_,
                                                          const ecs::Geometry3DDrawBatch& batch_) noexcept;
    [[nodiscard]] static CullMode ResolveCullMode(const ecs::Geometry3DDrawBatch& batch_) noexcept;
    [[nodiscard]] static BlendMode ResolveBlendMode(const ecs::Geometry3DDrawBatch& batch_) noexcept;

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_,
                               VkFormat depth_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsurePipelineForMode(VulkanContext& context_,
                                                                   render::PipelineHost& pipeline_host_,
                                                                   VkFormat color_format_,
                                                                   VkFormat depth_format_,
                                                                   BlendMode blend_mode_,
                                                                   PipelineMode mode_,
                                                                   TopologyMode topology_mode_,
                                                                   CullMode cull_mode_);
    void PrewarmCommonPipelines(VulkanContext& context_,
                                render::PipelineHost& pipeline_host_,
                                VkFormat color_format_,
                                VkFormat depth_format_);
    void CompileRequiredPipelinesForCurrentFrame(VulkanContext& context_,
                                                 render::PipelineHost& pipeline_host_,
                                                 VkFormat color_format_,
                                                 VkFormat depth_format_);
    void EnsureLightingDescriptorObjects(VulkanContext& context_,
                                         render::DescriptorHost& descriptor_host_);
    void EnsureLightingResourcesForFrame(VulkanContext& context_);
    void EnsureStorageBufferCapacity(resource::BufferResource& buffer_,
                                     VkDeviceSize required_bytes_);
    void DestroyStorageBuffer(resource::BufferResource& buffer_) noexcept;
    void PrepareLightingDescriptorSetForFrame(std::uint32_t frame_index_);
    [[nodiscard]] LightingParamsGpu BuildLightingParamsGpu(VkExtent2D extent_) const noexcept;
    [[nodiscard]] static MaterialPushConstants BuildMaterialPushConstants(
        const GeometryMaterialDesc* material_desc_) noexcept;
    [[nodiscard]] bool ResolveMaterialBinding(std::uint32_t material_id_,
                                              MaterialPushConstants& out_material_push_constants_);
    void EnsureDepthResources(VulkanContext& context_,
                              std::uint32_t image_count_,
                              VkExtent2D extent_);
    void RetireDepthResources(std::uint64_t retire_value_);
    void CollectRetiredDepthResources(VulkanContext& context_,
                                      std::uint64_t completed_value_);
    void DestroyDepthResources(VulkanContext& context_);
    void DestroyRetiredDepthResources(VulkanContext& context_);
    void RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_,
                                                bool has_previous_content_) const;
    void RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const;
    void RecordDepthTransitionToAttachment(VkCommandBuffer command_buffer_,
                                           const resource::ImageResource& depth_resource_,
                                           bool initialized_) const;
    void RecordInternal(const render::FrameRecordContext& record_context_,
                        std::uint32_t pass_bucket_,
                        bool filter_by_pass_bucket_);

private:
    GeometryRenderer3DCreateInfo create_info_cache{};
    GeometryRenderer3DStats stats{};

    ecs::Geometry<ecs::Dim3>* geometry_components = nullptr;
    ecs::Transform<ecs::Dim3>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    std::uint32_t appearance_component_count = 0U;
    ecs::Camera<ecs::Dim3>* camera_component = nullptr;
    ecs::Transform<ecs::Dim3>* camera_transform = nullptr;
    ecs::Bounds<ecs::Dim3>* bounds_components = nullptr;
    const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs = nullptr;
    std::uint32_t skeletal_output_count = 0U;
    const ecs::VertexDeformOutputState* vertex_deform_outputs = nullptr;
    std::uint32_t vertex_deform_output_count = 0U;
    const ecs::MorphWeightOutputState* morph_outputs = nullptr;
    std::uint32_t morph_output_count = 0U;
    const ecs::FrameSequenceOutputState* frame_sequence_outputs = nullptr;
    std::uint32_t frame_sequence_output_count = 0U;

    ecs::Geometry3DRuntimeScratch runtime_scratch{};
    ecs::Geometry3DRuntimeBuildStats runtime_stats{};
    render::AppearancePrepareBridge<ecs::Dim3> appearance_prepare_bridge{};
    ecs::AppearanceRuntimeBuildStats appearance_runtime_stats{};
    ecs::AppearanceLinkStats appearance_link_stats{};
    bool appearance_build_invoked = false;
    bool appearance_full_rebuild = false;
    ecs::CullingScratch<ecs::Dim3> culling_scratch{};
    ecs::CullingBuildStats culling_stats{};

    GeometryResourceHost* geometry_resource_host = nullptr;
    GeometryUploadHost* geometry_upload_host = nullptr;
    GeometryMaterialHost* geometry_material_host = nullptr;
    GeometryImageHost* geometry_image_host = nullptr;
    asset::TextureHost* texture_host = nullptr;

    VulkanContext* context = nullptr;
    render::UploadHost* upload_host = nullptr;
    render::DescriptorHost* descriptor_host = nullptr;
    render::BindlessResourceSystem* bindless_resources = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;

    render::DescriptorSetLayoutId lighting_descriptor_layout_id{};
    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    std::array<std::array<std::array<std::array<render::GraphicsPipelineId,
                                                static_cast<std::size_t>(CullMode::count)>,
                                     static_cast<std::size_t>(TopologyMode::count)>,
                          static_cast<std::size_t>(PipelineMode::count)>,
               static_cast<std::size_t>(BlendMode::count)> pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;

    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    GeometryRenderer3DMcVector<resource::ImageResource> depth_images{};
    GeometryRenderer3DMcVector<std::uint8_t> depth_image_initialized{};
    GeometryRenderer3DMcVector<RetiredDepthImage> retired_depth_images{};
    GeometryRenderer3DMcVector<std::uint8_t> image_initialized{};
    GeometryRenderer3DMcVector<FrameLightingResources> frame_lighting_resources{};
    GeometryRenderer3DMcVector<ResolvedMaterialEntry> resolved_materials{};
    GeometryRenderer3DMcVector<MaterialGpuRecord> material_record_scratch{};
    GeometryRenderer3DMcVector<GeometrySkeletalComponentGpu> skeletal_component_scratch{};
    GeometryRenderer3DMcVector<GeometrySkeletalMatrixGpu> skeletal_matrix_scratch{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};

    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    render::RenderTargetColorOutputConfig output_target_config{};
    render::RenderTargetDepthOutputConfig depth_output_target_config{};
    GeometryUploadRange instance_range{};

    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    std::uint64_t bindless_revision_seen = 0U;
    std::uint64_t material_record_bindless_revision_seen = 0U;
    std::uint32_t material_record_texture_host_revision_seen = 0U;
    std::uint64_t material_record_content_revision = 0U;
    std::uint32_t material_host_revision_seen = 0U;
    std::uint32_t image_host_revision_seen = 0U;
    render::LightFrameCoordinator<ecs::Dim3>* light_frame_coordinator = nullptr;
    render::IblHost* ibl_host = nullptr;
    render::LightShadowLinkCoordinator3D* light_shadow_link_coordinator = nullptr;
    render::LightShadowLinkCoordinator3D local_light_shadow_link_coordinator{};
    render::ShadowAtlasBindingCoordinator* shadow_atlas_binding_coordinator = nullptr;
    render::ShadowAtlasBindingCoordinator local_shadow_atlas_binding_coordinator{};
    render::ShadowFrameCoordinator<ecs::Dim3>* shadow_frame_coordinator = nullptr;
    shadow::ShadowAtlasHost* shadow_atlas_host = nullptr;
    light::LightShadowUploadHost light_shadow_upload_host{};
    bool initialized = false;
};

} // namespace vr::geometry
