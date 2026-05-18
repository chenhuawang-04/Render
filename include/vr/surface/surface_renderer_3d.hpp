#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/bounds_component.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/system/culling_system.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/surface_runtime_system.hpp"
#include "vr/render/appearance_prepare_bridge.hpp"
#include "vr/render/appearance_gpu_prepare.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/scene_render_stage.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/surface/surface_upload_host.hpp"

#include <array>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::asset {
class TextureHost;
}

namespace vr::render {
struct SurfaceRenderer3DPrepareView;
struct FrameRecordContext;
class UploadHost;
class IblHost;
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
using SurfaceRenderer3DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SurfaceRenderer3DCreateInfo final {
    Surface3DRuntimeUploadOptions runtime_upload_options{};
    std::uint32_t reserve_component_count = 4096U;
    std::uint32_t reserve_instance_count = 8192U;
    std::uint32_t reserve_dirty_component_count = 512U;
    bool enable_depth = true;
    VkFormat preferred_depth_format = VK_FORMAT_D32_SFLOAT;
    bool clear_depth = true;
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
};

struct SurfaceRenderer3DStats final {
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
    std::uint32_t uploaded_instance_count = 0U;
    std::uint32_t uploaded_patch_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t ibl_descriptor_set_bind_count = 0U;
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
    bool used_partial_upload = false;
    bool skipped_upload = true;
    bool used_bounds_culling = false;
};

class SurfaceRenderer3D final {
public:
    SurfaceRenderer3D() = default;
    ~SurfaceRenderer3D() = default;

    SurfaceRenderer3D(const SurfaceRenderer3D&) = delete;
    SurfaceRenderer3D& operator=(const SurfaceRenderer3D&) = delete;

    SurfaceRenderer3D(SurfaceRenderer3D&&) = delete;
    SurfaceRenderer3D& operator=(SurfaceRenderer3D&&) = delete;

    void Initialize(const SurfaceRenderer3DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetHost(SurfaceUploadHost* upload_host_) noexcept;
    void SetImageHost(SurfaceImageHost* image_host_) noexcept;
    void SetHosts(SurfaceUploadHost* upload_host_,
                  SurfaceImageHost* image_host_) noexcept;
    void SetSceneData(ecs::Surface<ecs::Dim3>* surface_components_,
                      ecs::Transform<ecs::Dim3>* transforms_,
                      std::uint32_t component_count_,
                      ecs::Camera<ecs::Dim3>* camera_component_,
                      ecs::Transform<ecs::Dim3>* camera_transform_,
                      ecs::Bounds<ecs::Dim3>* bounds_components_ = nullptr) noexcept;
    void SetAppearanceData(ecs::Appearance<ecs::Dim3>* appearance_components_,
                           std::uint32_t appearance_component_count_) noexcept;
    void SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                               std::uint32_t dirty_component_count_) noexcept;
    void SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                std::uint32_t dirty_component_count_) noexcept;
    void SetAppearanceCoordinator(render::AppearanceFrameCoordinator<ecs::Dim3>* appearance_frame_coordinator_) noexcept;
    void SetOutputTargetConfig(const render::RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;
    void SetDepthTargetConfig(const render::RenderTargetDepthOutputConfig& depth_output_target_config_) noexcept;
    void ResetDepthTargetConfig() noexcept;

    void PrepareFrame(const render::SurfaceRenderer3DPrepareView& prepare_view_);
    void DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                         render_graph::PassHandle pass_) const;
    void Record(const render::FrameRecordContext& record_context_);
    void RecordSceneStage(const render::FrameRecordContext& record_context_,
                          render::SceneRenderStage stage_);
    void RecordGraphSceneStage(render_graph::GraphCommandContext& context_,
                               render::SceneRenderStage stage_,
                               render_graph::ResourceHandle color_target_,
                               render_graph::ResourceHandle depth_target_ = render_graph::invalid_resource_handle);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const SurfaceRenderer3DStats& Stats() const noexcept;

private:
    struct PushConstants final {
        ecs::Matrix4x4 view_projection;
        ecs::Float4 camera_position;
        std::uint32_t params;
        std::uint32_t ibl_specular_texture_slot;
        std::uint32_t ibl_brdf_lut_texture_slot;
        std::uint32_t ibl_sampler_slot;
    };

    enum class PipelineMode : std::uint8_t {
        no_depth = 0U,
        depth_read = 1U,
        depth_read_write = 2U,
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

    struct FrameAppearanceResources final {
        resource::BufferResource appearance_records{};
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        std::uint32_t appearance_record_count = 0U;
        std::uint64_t appearance_content_revision = 0U;
        std::uint64_t appearance_bindless_revision = 0U;
        std::uint32_t appearance_texture_host_revision = 0U;
        std::uint64_t descriptor_payload_signature = 0U;
        std::uint64_t descriptor_buffer_signature = 0U;
    };

    static_assert(sizeof(PushConstants) == 96U);

    [[nodiscard]] static bool IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept;
    [[nodiscard]] static bool DepthFormatHasStencil(VkFormat format_) noexcept;
    [[nodiscard]] static VkImageAspectFlags DepthImageAspectMask(VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_);

    [[nodiscard]] static std::size_t PipelineModeIndex(PipelineMode mode_) noexcept;
    [[nodiscard]] static std::size_t CullModeIndex(CullMode mode_) noexcept;
    [[nodiscard]] static std::size_t BlendModeIndex(BlendMode mode_) noexcept;
    [[nodiscard]] static PipelineMode ResolvePipelineMode(std::uint32_t batch_params_,
                                                          bool use_depth_) noexcept;
    [[nodiscard]] static CullMode ResolveCullMode(std::uint32_t batch_params_) noexcept;
    [[nodiscard]] static BlendMode ResolveBlendMode(std::uint32_t batch_params_) noexcept;
    [[nodiscard]] static std::uint64_t ComposeBindlessUploadRevision(
        const ecs::Surface3DRuntimeBuildStats& runtime_stats_,
        std::uint32_t image_revision_) noexcept;
    void BuildAppearanceRecordsAndAssignIndices();

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::BindlessResourceSystem& bindless_resources_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_,
                               VkFormat depth_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsurePipelineForMode(
        VulkanContext& context_,
        render::PipelineHost& pipeline_host_,
        VkFormat color_format_,
        VkFormat depth_format_,
        BlendMode blend_mode_,
        PipelineMode mode_,
        CullMode cull_mode_);
    void EnsureAppearanceDescriptorObjects(VulkanContext& context_,
                                           render::DescriptorHost& descriptor_host_);
    void EnsureStorageBufferCapacity(resource::BufferResource& buffer_,
                                     VkDeviceSize required_bytes_);
    void DestroyStorageBuffer(resource::BufferResource& buffer_) noexcept;
    void EnsureAppearanceResourcesForFrame(std::uint64_t bindless_revision_now);
    void PrepareAppearanceDescriptorSetForFrame(std::uint32_t frame_index_);

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
    void RecordGraphInternal(render_graph::GraphCommandContext& context_,
                             std::uint32_t pass_bucket_,
                             bool filter_by_pass_bucket_,
                             render_graph::ResourceHandle color_target_,
                             render_graph::ResourceHandle depth_target_);
    void RecordInternal(const render::FrameRecordContext& record_context_,
                        std::uint32_t pass_bucket_,
                        bool filter_by_pass_bucket_);

private:
    SurfaceRenderer3DCreateInfo create_info_cache{};
    SurfaceRenderer3DStats stats{};

    ecs::Surface<ecs::Dim3>* surface_components = nullptr;
    ecs::Transform<ecs::Dim3>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    std::uint32_t appearance_component_count = 0U;
    ecs::Camera<ecs::Dim3>* camera_component = nullptr;
    ecs::Transform<ecs::Dim3>* camera_transform = nullptr;
    ecs::Bounds<ecs::Dim3>* bounds_components = nullptr;

    ecs::Surface3DRuntimeScratch runtime_scratch{};
    ecs::SurfaceUploadPlanScratch<ecs::Dim3> plan_scratch{};
    render::AppearancePrepareBridge<ecs::Dim3> appearance_prepare_bridge{};
    ecs::AppearanceRuntimeBuildStats appearance_runtime_stats{};
    ecs::AppearanceLinkStats appearance_link_stats{};
    bool appearance_build_invoked = false;
    ecs::CullingScratch<ecs::Dim3> culling_scratch{};
    ecs::CullingBuildStats culling_stats{};
    Surface3DRuntimeUploadResult last_upload_result{};

    SurfaceUploadHost* surface_upload_host = nullptr;
    SurfaceImageHost* surface_image_host = nullptr;
    asset::TextureHost* texture_host = nullptr;
    VulkanContext* context = nullptr;
    render::UploadHost* upload_host = nullptr;
    render::DescriptorHost* descriptor_host = nullptr;
    render::BindlessResourceSystem* bindless_resources = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    render::IblHost* ibl_host = nullptr;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;

    render::DescriptorSetLayoutId appearance_descriptor_layout_id{};
    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    std::array<std::array<std::array<render::GraphicsPipelineId,
                                     static_cast<std::size_t>(CullMode::count)>,
                          static_cast<std::size_t>(PipelineMode::count)>,
               static_cast<std::size_t>(BlendMode::count)> pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;

    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    SurfaceRenderer3DMcVector<resource::ImageResource> depth_images{};
    SurfaceRenderer3DMcVector<std::uint8_t> depth_image_initialized{};
    SurfaceRenderer3DMcVector<RetiredDepthImage> retired_depth_images{};
    VkDescriptorSet active_ibl_params_descriptor_set = VK_NULL_HANDLE;
    std::uint32_t active_ibl_specular_texture_slot = 0U;
    std::uint32_t active_ibl_brdf_lut_texture_slot = 0U;
    std::uint32_t active_ibl_sampler_slot = 0U;
    SurfaceRenderer3DMcVector<std::uint8_t> image_initialized{};
    SurfaceRenderer3DMcVector<FrameAppearanceResources> frame_appearance_resources{};
    SurfaceRenderer3DMcVector<ecs::AppearanceGpuRecord<ecs::Dim3>> appearance_source_record_scratch{};
    SurfaceRenderer3DMcVector<ecs::AppearanceGpuRecord<ecs::Dim3>> appearance_record_scratch{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorImageWrite> descriptor_image_write_scratch{};

    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    render::RenderTargetColorOutputConfig output_target_config{};
    render::RenderTargetDepthOutputConfig depth_output_target_config{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    std::uint64_t appearance_record_bindless_revision_seen = 0U;
    std::uint32_t appearance_record_texture_host_revision_seen = 0U;
    std::uint64_t appearance_record_content_revision = 0U;

    const std::uint32_t* pending_dirty_component_indices = nullptr;
    std::uint32_t pending_dirty_component_count = 0U;
    bool initialized = false;
};

} // namespace vr::surface

