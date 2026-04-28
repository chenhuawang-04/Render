#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/bounds_component.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/system/culling_system.hpp"
#include "vr/ecs/system/geometry_runtime_system.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/geometry/geometry_material_host.hpp"
#include "vr/geometry/geometry_resource_host.hpp"
#include "vr/geometry/geometry_upload_host.hpp"
#include "vr/render/appearance_prepare_bridge.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/resource/image_host.hpp"

#include <array>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render {
struct RuntimePrepareContext;
struct FrameRecordContext;
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
    std::uint32_t skipped_batch_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
    std::uint32_t shadow_cast_batch_count = 0U;
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
    void SetAppearanceCoordinator(render::AppearanceFrameCoordinator<ecs::Dim3>* appearance_frame_coordinator_) noexcept;

    void PrepareFrame(const render::RuntimePrepareContext& prepare_context_);
    void Record(const render::FrameRecordContext& record_context_);
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
    };

    struct MaterialPushConstants final {
        float uv_scale_u;
        float uv_scale_v;
        float uv_bias_u;
        float uv_bias_v;
        std::uint32_t flags;
        float alpha_cutoff;
        float reserved0;
        float reserved1;
    };

    struct PushConstants final {
        FramePushConstants frame{};
        MaterialPushConstants material{};
    };

    static_assert(sizeof(FramePushConstants) == 80U);
    static_assert(sizeof(MaterialPushConstants) == 32U);
    static_assert(sizeof(PushConstants) == 112U);

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

    struct RetiredDepthImage final {
        resource::ImageResource resource{};
        std::uint64_t retire_value = 0U;
    };

    struct MaterialSetEntry final {
        std::uint32_t material_id = 0U;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        MaterialPushConstants material_push_constants{};
    };

    struct ResolvedMaterialEntry final {
        std::uint32_t material_id = 0U;
        std::uint32_t material_revision = 0U;
        std::uint32_t image_id = 0U;
        std::uint32_t image_revision = 0U;
        VkImageView image_view = VK_NULL_HANDLE;
        VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkSampler sampler = VK_NULL_HANDLE;
        MaterialPushConstants material_push_constants{};
    };

    [[nodiscard]] static bool IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept;
    [[nodiscard]] static bool DepthFormatHasStencil(VkFormat format_) noexcept;
    [[nodiscard]] static VkImageAspectFlags DepthImageAspectMask(VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_);
    [[nodiscard]] static std::size_t PipelineModeIndex(PipelineMode mode_) noexcept;
    [[nodiscard]] static std::size_t TopologyModeIndex(TopologyMode mode_) noexcept;
    [[nodiscard]] static std::size_t CullModeIndex(CullMode mode_) noexcept;
    [[nodiscard]] static std::size_t LowerBoundMaterialSetIndex(
        const GeometryRenderer3DMcVector<MaterialSetEntry>& entries_,
        std::uint32_t material_id_) noexcept;
    [[nodiscard]] static std::size_t LowerBoundResolvedMaterialIndex(
        const GeometryRenderer3DMcVector<ResolvedMaterialEntry>& entries_,
        std::uint32_t material_id_) noexcept;
    [[nodiscard]] static PipelineMode ResolvePipelineMode(const ecs::Geometry3DDrawBatch& batch_,
                                                          bool use_depth_) noexcept;
    [[nodiscard]] static TopologyMode ResolveTopologyMode(VkPrimitiveTopology mesh_topology_,
                                                          const ecs::Geometry3DDrawBatch& batch_) noexcept;
    [[nodiscard]] static CullMode ResolveCullMode(const ecs::Geometry3DDrawBatch& batch_) noexcept;

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_,
                               VkFormat depth_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsurePipelineForMode(VulkanContext& context_,
                                                                   render::PipelineHost& pipeline_host_,
                                                                   VkFormat color_format_,
                                                                   VkFormat depth_format_,
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
    void EnsureMaterialPipelineObjects(VulkanContext& context_,
                                       render::DescriptorHost& descriptor_host_);
    void EnsureFallbackMaterialResources(VulkanContext& context_);
    [[nodiscard]] static MaterialPushConstants BuildMaterialPushConstants(
        const GeometryMaterialDesc* material_desc_) noexcept;
    [[nodiscard]] bool ResolveMaterialBinding(std::uint32_t material_id_,
                                              VkSampler& out_sampler_,
                                              VkImageView& out_image_view_,
                                              VkImageLayout& out_image_layout_,
                                              MaterialPushConstants& out_material_push_constants_);
    [[nodiscard]] VkDescriptorSet AcquireMaterialDescriptorSet(std::uint32_t frame_index_,
                                                               std::uint32_t material_id_,
                                                               MaterialPushConstants* out_material_push_constants_ = nullptr);
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

    ecs::Geometry3DRuntimeScratch runtime_scratch{};
    ecs::Geometry3DRuntimeBuildStats runtime_stats{};
    render::AppearancePrepareBridge<ecs::Dim3> appearance_prepare_bridge{};
    ecs::AppearanceRuntimeBuildStats appearance_runtime_stats{};
    ecs::AppearanceLinkStats appearance_link_stats{};
    ecs::CullingScratch<ecs::Dim3> culling_scratch{};
    ecs::CullingBuildStats culling_stats{};

    GeometryResourceHost* geometry_resource_host = nullptr;
    GeometryUploadHost* geometry_upload_host = nullptr;
    GeometryMaterialHost* geometry_material_host = nullptr;
    GeometryImageHost* geometry_image_host = nullptr;

    VulkanContext* context = nullptr;
    render::UploadHost* upload_host = nullptr;
    render::DescriptorHost* descriptor_host = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;

    render::DescriptorSetLayoutId descriptor_layout_id{};
    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    std::array<std::array<std::array<render::GraphicsPipelineId,
                                     static_cast<std::size_t>(CullMode::count)>,
                          static_cast<std::size_t>(TopologyMode::count)>,
               static_cast<std::size_t>(PipelineMode::count)> pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;

    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    GeometryRenderer3DMcVector<resource::ImageResource> depth_images{};
    GeometryRenderer3DMcVector<std::uint8_t> depth_image_initialized{};
    GeometryRenderer3DMcVector<RetiredDepthImage> retired_depth_images{};
    GeometryRenderer3DMcVector<std::uint8_t> image_initialized{};
    GeometryRenderer3DMcVector<GeometryRenderer3DMcVector<MaterialSetEntry>> frame_material_sets{};
    GeometryRenderer3DMcVector<ResolvedMaterialEntry> resolved_materials{};
    render::DescriptorMcVector<render::DescriptorImageWrite> descriptor_image_write_scratch{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};

    resource::ImageResource fallback_material_image{};
    resource::SamplerId fallback_material_sampler_id{};
    VkImageLayout fallback_material_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    GeometryUploadRange instance_range{};

    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    std::uint32_t material_host_revision_seen = 0U;
    std::uint32_t image_host_revision_seen = 0U;
    bool initialized = false;
};

} // namespace vr::geometry
