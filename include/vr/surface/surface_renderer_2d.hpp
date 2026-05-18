#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/ecs/system/light_culling_system.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/surface_runtime_system.hpp"
#include "vr/light/light_shadow_upload_host.hpp"
#include "vr/render/appearance_prepare_bridge.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/light_shadow_link_coordinator.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/shadow_atlas_binding_coordinator.hpp"
#include "vr/render/shadow_frame_coordinator.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/shadow/shadow_atlas_host.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/surface/surface_upload_host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render {
struct SurfaceRenderer2DPrepareView;
struct FrameRecordContext;
class UploadHost;
class BindlessResourceSystem;
}

namespace vr::render_graph {
class GraphCommandContext;
class RenderGraphBuilder;
}

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::surface {

template<typename T>
using SurfaceRenderer2DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SurfaceRenderer2DCreateInfo final {
    Surface2DRuntimeUploadOptions runtime_upload_options{};
    std::uint32_t reserve_component_count = 4096U;
    std::uint32_t reserve_instance_count = 16384U;
    std::uint32_t reserve_dirty_component_count = 512U;
    bool input_positions_pixel_space = true;
    bool pixel_space_origin_top_left = true;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
    bool enable_light_shadow = true;
    std::uint32_t max_fragment_lights = 64U;
    float light_ambient = 0.08F;
    ecs::LightCullingBuildConfig<ecs::Dim2> light_culling_config =
        ecs::LightCullingSystem<ecs::Dim2>::DefaultBuildConfig();
};

struct SurfaceRenderer2DStats final {
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
    std::uint32_t uploaded_instance_count = 0U;
    std::uint32_t uploaded_patch_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t light_count = 0U;
    std::uint32_t visible_light_count = 0U;
    std::uint32_t shadow_view_count = 0U;
    std::uint32_t light_upload_range_count = 0U;
    std::uint32_t shadow_view_upload_range_count = 0U;
    std::uint32_t light_cluster_count = 0U;
    std::uint32_t light_cluster_index_count = 0U;
    std::uint32_t light_buffer_upload_count = 0U;
    std::uint32_t light_descriptor_set_bind_count = 0U;
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
    bool used_partial_upload = false;
    bool skipped_upload = true;
};

class SurfaceRenderer2D final {
public:
    SurfaceRenderer2D() = default;
    ~SurfaceRenderer2D() = default;

    SurfaceRenderer2D(const SurfaceRenderer2D&) = delete;
    SurfaceRenderer2D& operator=(const SurfaceRenderer2D&) = delete;

    SurfaceRenderer2D(SurfaceRenderer2D&&) = delete;
    SurfaceRenderer2D& operator=(SurfaceRenderer2D&&) = delete;

    void Initialize(const SurfaceRenderer2DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetHost(SurfaceUploadHost* upload_host_) noexcept;
    void SetImageHost(SurfaceImageHost* image_host_) noexcept;
    void SetHosts(SurfaceUploadHost* upload_host_,
                  SurfaceImageHost* image_host_) noexcept;
    void SetSceneData(ecs::Surface<ecs::Dim2>* surface_components_,
                      ecs::Transform<ecs::Dim2>* transforms_,
                      std::uint32_t component_count_) noexcept;
    void SetAppearanceData(ecs::Appearance<ecs::Dim2>* appearance_components_,
                           std::uint32_t appearance_component_count_) noexcept;
    void SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                               std::uint32_t dirty_component_count_) noexcept;
    void SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                std::uint32_t dirty_component_count_) noexcept;
    void SetAppearanceCoordinator(render::AppearanceFrameCoordinator<ecs::Dim2>* appearance_frame_coordinator_) noexcept;
    void SetLightFrameCoordinator(render::LightFrameCoordinator<ecs::Dim2>* light_frame_coordinator_) noexcept;
    void SetLightShadowLinkCoordinator(render::LightShadowLinkCoordinator2D* light_shadow_link_coordinator_) noexcept;
    void SetShadowAtlasBindingCoordinator(render::ShadowAtlasBindingCoordinator* shadow_atlas_binding_coordinator_) noexcept;
    void SetShadowFrameCoordinator(render::ShadowFrameCoordinator<ecs::Dim2>* shadow_frame_coordinator_) noexcept;
    void SetShadowAtlasHost(shadow::ShadowAtlasHost* shadow_atlas_host_) noexcept;
    void SetOutputTargetConfig(const render::RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;

    void PrepareFrame(const render::SurfaceRenderer2DPrepareView& prepare_view_);
    void DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                         render_graph::PassHandle pass_) const;
    void Record(const render::FrameRecordContext& record_context_);
    void RecordGraphOverlay(render_graph::GraphCommandContext& context_,
                            render_graph::ResourceHandle color_target_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const SurfaceRenderer2DStats& Stats() const noexcept;

private:
    enum class BlendModeKind : std::uint8_t {
        opaque = 0U,
        alpha = 1U,
        additive = 2U,
        multiply = 3U,
        premultiplied_alpha = 4U,
        screen = 5U,
        count = 6U
    };

    struct PushConstants final {
        float viewport_width;
        float viewport_height;
        float inv_viewport_width_2x;
        float inv_viewport_height_2x;
        std::uint32_t params;
        std::uint32_t reserved0;
        std::uint32_t reserved1;
        std::uint32_t reserved2;
    };

    struct alignas(16) LightingParamsGpu final {
        float world_to_ndc_x = 0.0F;
        float world_to_ndc_y = 0.0F;
        float world_to_ndc_bias_x = 0.0F;
        float world_to_ndc_bias_y = 0.0F;

        float light_count = 0.0F;
        float max_fragment_lights = 0.0F;
        float shadow_view_count = 0.0F;
        float light_ambient = 0.0F;

        std::uint32_t tile_count_x = 1U;
        std::uint32_t tile_count_y = 1U;
        std::uint32_t reverse_z = 0U;
        std::uint32_t reserved0 = 0U;

        float framebuffer_width = 1.0F;
        float framebuffer_height = 1.0F;
        std::uint32_t shadow_atlas_texture_slot = 0U;
        std::uint32_t shadow_atlas_sampler_slot = 0U;
    };

    struct FrameLightingResources final {
        light::LightShadowBufferRange light_records{};
        light::LightShadowBufferRange cluster_headers{};
        light::LightShadowBufferRange cluster_indices{};
        light::LightShadowBufferRange shadow_views{};
        light::LightShadowBufferRange lighting_uniform{};
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        std::uint32_t shadow_namespace_id = 0U;
        std::uint64_t upload_signature = 0U;
        std::uint64_t descriptor_payload_signature = 0U;
        std::uint64_t descriptor_buffer_signature = 0U;
        std::uint64_t descriptor_image_signature = 0U;
        std::uint64_t descriptor_set_signature = 0U;
    };

    static_assert(sizeof(PushConstants) == 32U);
    static_assert(sizeof(LightingParamsGpu) == 64U);
    static_assert(offsetof(LightingParamsGpu, world_to_ndc_x) == 0U);
    static_assert(offsetof(LightingParamsGpu, light_count) == 16U);
    static_assert(offsetof(LightingParamsGpu, tile_count_x) == 32U);
    static_assert(offsetof(LightingParamsGpu, framebuffer_width) == 48U);

    [[nodiscard]] static std::size_t BlendModeIndex(BlendModeKind mode_) noexcept;
    [[nodiscard]] static BlendModeKind ResolveBlendModeFromBatchParams(std::uint32_t params_) noexcept;
    [[nodiscard]] static render_graph::ExternalBufferBindingPayload ResolveLightRecordsExternalBufferBinding(
        const void* user_data_);
    [[nodiscard]] static render_graph::ExternalBufferBindingPayload ResolveClusterHeadersExternalBufferBinding(
        const void* user_data_);
    [[nodiscard]] static render_graph::ExternalBufferBindingPayload ResolveClusterIndicesExternalBufferBinding(
        const void* user_data_);
    [[nodiscard]] static render_graph::ExternalBufferBindingPayload ResolveShadowViewsExternalBufferBinding(
        const void* user_data_);
    [[nodiscard]] static render_graph::ExternalBufferBindingPayload ResolveLightingUniformExternalBufferBinding(
        const void* user_data_);

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::DescriptorHost& descriptor_host_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsurePipelineForBlendMode(
        VulkanContext& context_,
        render::PipelineHost& pipeline_host_,
        VkFormat color_format_,
        BlendModeKind blend_mode_);
    void RecordGraphInternal(render_graph::GraphCommandContext& context_,
                             render_graph::ResourceHandle color_target_);
    void RecordDrawBatches(VkCommandBuffer command_buffer_,
                           VkExtent2D render_extent_,
                           VkFormat color_format_,
                           const render_graph::GraphCommandContext* graph_context_ = nullptr);
    void EnsureFallbackTexture(VulkanContext& context_,
                               render::UploadHost& upload_host_,
                               std::uint32_t frame_index_);
    void EnsureLightingDescriptorObjects(VulkanContext& context_,
                                         render::DescriptorHost& descriptor_host_);
    void EnsureLightingResourcesForFrame(VulkanContext& context_);
    void PrepareLightingDescriptorSetForFrame(std::uint32_t frame_index_);
    [[nodiscard]] LightingParamsGpu BuildLightingParamsGpu(VkExtent2D extent_) const noexcept;
    void RemapInstancesToBindless(ecs::Surface2DGpuInstance* instances_,
                                  std::uint32_t instance_count_) noexcept;
    [[nodiscard]] std::uint32_t ResolveImageSlot(std::uint32_t surface_id_) const noexcept;
    [[nodiscard]] std::uint32_t ResolveSamplerSlot(std::uint32_t surface_id_) const noexcept;

private:
    SurfaceRenderer2DCreateInfo create_info_cache{};
    SurfaceRenderer2DStats stats{};

    ecs::Surface<ecs::Dim2>* surface_components = nullptr;
    ecs::Transform<ecs::Dim2>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    std::uint32_t appearance_component_count = 0U;

    ecs::Surface2DRuntimeScratch runtime_scratch{};
    ecs::SurfaceUploadPlanScratch<ecs::Dim2> plan_scratch{};
    render::AppearancePrepareBridge<ecs::Dim2> appearance_prepare_bridge{};
    ecs::AppearanceRuntimeBuildStats appearance_runtime_stats{};
    ecs::AppearanceLinkStats appearance_link_stats{};
    Surface2DRuntimeUploadResult last_upload_result{};

    SurfaceUploadHost* surface_upload_host = nullptr;
    SurfaceImageHost* surface_image_host = nullptr;
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
    std::array<render::GraphicsPipelineId,
               static_cast<std::size_t>(BlendModeKind::count)> pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;

    SurfaceRenderer2DMcVector<FrameLightingResources> frame_lighting_resources{};
    render::DescriptorMcVector<render::DescriptorImageWrite> descriptor_image_write_scratch{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};
    resource::ImageResource fallback_texture{};
    resource::SamplerId fallback_sampler_id{};
    VkImageLayout fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageView fallback_shadow_array_view = VK_NULL_HANDLE;
    resource::SamplerId shadow_sampler_id{};

    SurfaceRenderer2DMcVector<std::uint8_t> image_initialized{};
    render::RenderTargetColorOutputConfig output_target_config{};

    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    std::uint32_t bindless_mapping_revision = 0U;
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    render::LightFrameCoordinator<ecs::Dim2>* light_frame_coordinator = nullptr;
    render::LightShadowLinkCoordinator2D* light_shadow_link_coordinator = nullptr;
    render::LightShadowLinkCoordinator2D local_light_shadow_link_coordinator{};
    render::ShadowAtlasBindingCoordinator* shadow_atlas_binding_coordinator = nullptr;
    render::ShadowAtlasBindingCoordinator local_shadow_atlas_binding_coordinator{};
    render::ShadowFrameCoordinator<ecs::Dim2>* shadow_frame_coordinator = nullptr;
    shadow::ShadowAtlasHost* shadow_atlas_host = nullptr;
    light::LightShadowUploadHost light_shadow_upload_host{};

    const std::uint32_t* pending_dirty_component_indices = nullptr;
    std::uint32_t pending_dirty_component_count = 0U;
    bool initialized = false;
};

} // namespace vr::surface

