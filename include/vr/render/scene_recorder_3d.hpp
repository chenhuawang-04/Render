#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/animation_frame_coordinator.hpp"
#include "vr/render/environment/sky_environment_pass.hpp"
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/light_shadow_link_coordinator.hpp"
#include "vr/render/render_target_bloom_renderer.hpp"
#include "vr/render/scene_prepare_views.hpp"
#include "vr/render/scene_render_stage.hpp"
#include "vr/render/scene_render_target_set.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render/shadow_atlas_binding_coordinator.hpp"
#include "vr/render/shadow_frame_coordinator.hpp"
#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/shadow/shadow_renderer_3d.hpp"

#include <cstdint>

namespace vr::geometry {
class GeometryRenderer3D;
}

namespace vr::particle {
class ParticleRenderer2D;
class ParticleRenderer3D;
}

namespace vr::surface {
class SurfaceRenderer2D;
class SurfaceRenderer3D;
}

namespace vr::shadow {
class ShadowAtlasHost;
}

namespace vr::text {
class TextRenderer2D;
class TextRenderer3D;
}

namespace vr::runtime::services {
class RenderGraphRuntimeService;
}

namespace vr::render {

template<typename T>
using SceneRecorder3DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SceneRecorder3DCreateInfo final {
    SceneRenderTargetSetCreateInfo scene_target{};
    RenderTargetBloomRendererCreateInfo bloom{};
    std::uint32_t reserve_pre_scene_renderer_count = 1U;
    std::uint32_t reserve_scene_renderer_count = 4U;
    std::uint32_t reserve_overlay_renderer_count = 2U;
};

struct SceneRecorder3DStats final {
    std::uint32_t pre_scene_renderer_count = 0U;
    std::uint32_t scene_renderer_count = 0U;
    std::uint32_t opaque_scene_renderer_count = 0U;
    std::uint32_t transparent_scene_renderer_count = 0U;
    std::uint32_t overlay_renderer_count = 0U;
    std::uint32_t prepare_count = 0U;
    std::uint32_t record_count = 0U;
    std::uint32_t swapchain_recreate_count = 0U;
    std::uint32_t frame_packet_bind_count = 0U;
    std::uint32_t frame_packet_prepare_count = 0U;
    std::uint32_t frame_packet_record_count = 0U;
    std::uint32_t animation_binding_refresh_count = 0U;
    std::uint32_t environment_gpu_resolve_count = 0U;
    std::uint32_t environment_prepare_count = 0U;
    std::uint32_t environment_record_count = 0U;
    std::uint32_t frame_view_count = 0U;
    std::uint32_t active_view_index = 0xFFFF'FFFFU;
    std::uint32_t scene_view_index = 0xFFFF'FFFFU;
    std::uint32_t overlay_view_index = 0xFFFF'FFFFU;
    std::uint32_t effective_layer_mask = 0xFFFF'FFFFU;
    std::uint32_t overlay_layer_mask = 0U;
    std::uint32_t debug_flags = render_view_debug_none_flag;
    std::uint8_t active_view_kind = static_cast<std::uint8_t>(RenderViewKind::custom);
    std::uint8_t has_active_view = 0U;
    std::uint8_t shadow_enabled = 1U;
    std::uint8_t overlay_enabled = 1U;
    std::uint8_t postprocess_enabled = 1U;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
};

class SceneRecorder3D final {
public:
    SceneRecorder3D() = default;
    ~SceneRecorder3D() = default;

    SceneRecorder3D(const SceneRecorder3D&) = delete;
    SceneRecorder3D& operator=(const SceneRecorder3D&) = delete;

    SceneRecorder3D(SceneRecorder3D&&) = delete;
    SceneRecorder3D& operator=(SceneRecorder3D&&) = delete;

    void Initialize(const SceneRecorder3DCreateInfo& create_info_ = {}) noexcept;
    void Shutdown(VulkanContext& context_) noexcept;

    void BindRuntimeResources(VulkanContext& context_,
                              RenderTargetHost& render_target_host_) noexcept;

    template<typename RuntimeT>
    void BindRuntime(RuntimeT& runtime_) noexcept {
        BindRuntimeResources(runtime_.Context(), runtime_.RenderTarget());
        if constexpr (requires(RuntimeT& runtime_ref_) {
                          runtime_ref_.Services();
                      }) {
            graph_runtime_service =
                runtime_.Services().template TryGet<runtime::services::RenderGraphRuntimeService>();
        } else {
            graph_runtime_service = nullptr;
        }
    }

    void ClearRuntimeBinding() noexcept;

    void BindLightFrameCoordinator(render::LightFrameCoordinator<ecs::Dim3>* light_frame_coordinator_) noexcept;
    void BindAnimationFrameCoordinator(render::AnimationFrameCoordinator<ecs::Dim3>* animation_frame_coordinator_) noexcept;
    void BindShadowRuntime(render::ShadowFrameCoordinator<ecs::Dim3>* shadow_frame_coordinator_,
                           shadow::ShadowAtlasHost* shadow_atlas_host_) noexcept;
    void ClearShadowRuntimeBinding() noexcept;
    void ClearAnimationFrameBinding() noexcept;
    void SetFramePacket(const RenderScenePacket3D* frame_packet_) noexcept;
    void ClearFramePacket() noexcept;

    template<typename RendererT>
    void RegisterPreSceneRenderer(RendererT& renderer_,
                                  std::uint32_t submission_layer_mask_ = all_submission_layers);

    void RegisterShadowRenderer(shadow::ShadowRenderer3D& shadow_renderer_,
                                std::uint32_t submission_layer_mask_ = all_submission_layers);

    template<typename RendererT>
    void RegisterSceneRenderer(RendererT& renderer_,
                               SceneRenderPassRole pass_role_,
                               SceneRecorder3DSceneStage stage_ =
                                   SceneRecorder3DSceneStage::opaque,
                               std::uint32_t submission_layer_mask_ = all_submission_layers);

    template<typename RendererT>
    void RegisterOpaqueSceneRenderer(RendererT& renderer_,
                                     SceneRenderPassRole pass_role_,
                                     std::uint32_t submission_layer_mask_ = all_submission_layers);

    template<typename RendererT>
    void RegisterTransparentSceneRenderer(RendererT& renderer_,
                                          SceneRenderPassRole pass_role_,
                                          std::uint32_t submission_layer_mask_ = all_submission_layers);

    template<typename RendererT>
    void RegisterOverlayRenderer(RendererT& renderer_,
                                 std::uint32_t submission_layer_mask_ = all_submission_layers);

    void ClearPreSceneRenderers() noexcept;
    void ClearSceneRenderers() noexcept;
    void ClearOverlayRenderers() noexcept;
    void ClearRendererRegistrations() noexcept;

    void PrepareFrame(const SceneRecorder3DPrepareView& prepare_view_);
    void PrepareFrame(const SceneRecorder3DPrepareView& prepare_view_,
                      const RenderScenePacket3D& frame_packet_);
    void BuildRenderGraph(render_graph::RenderGraphBuilder& builder_,
                          const render_graph::FrameSnapshot3D& snapshot_,
                          const render_graph::MinimalFrameGraphBuildResult<ecs::Dim3>& build_result_,
                          render_graph::ResourceVersionHandle& color_chain_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool HasRuntimeBinding() const noexcept;
    [[nodiscard]] const SceneRecorder3DCreateInfo& CreateInfo() const noexcept;
    [[nodiscard]] const SceneRecorder3DStats& Stats() const noexcept;
    [[nodiscard]] const RenderScenePacket3D* FramePacket() const noexcept;
    [[nodiscard]] const RenderView3D* ActiveView() const noexcept;
    [[nodiscard]] SkyEnvironmentPass& EnvironmentPass() noexcept;
    [[nodiscard]] const SkyEnvironmentPass& EnvironmentPass() const noexcept;
    [[nodiscard]] const RenderTargetBloomRendererStats& BloomStats() const noexcept;

private:
    static constexpr std::uint32_t all_submission_layers = 0xFFFF'FFFFU;

    using PrepareFn = void (*)(void*, const SceneRecorder3DPrepareView&);
    using RecordFn = void (*)(void*, const FrameRecordContext&);
    using SceneRecordFn = void (*)(void*, const FrameRecordContext&, SceneRecorder3DSceneStage);
    using GraphSceneRecordFn = void (*)(void*,
                                        render_graph::GraphCommandContext&,
                                        SceneRenderStage,
                                        render_graph::ResourceHandle,
                                        render_graph::ResourceHandle);
    using DescribeGraphBindingsFn = void (*)(void*,
                                             render_graph::RenderGraphBuilder&,
                                             render_graph::PassHandle);
    using RegisterGraphImportedResourcesFn = void (*)(void*,
                                                      runtime::services::RenderGraphRuntimeService&);
    using SwapchainRecreatedFn = void (*)(void*,
                                          std::uint32_t,
                                          VkExtent2D,
                                          VkFormat,
                                          std::uint64_t,
                                          std::uint64_t);
    using ConfigureLightingFn = void (*)(void*,
                                         render::LightFrameCoordinator<ecs::Dim3>*,
                                         render::LightShadowLinkCoordinator3D*,
                                         render::ShadowAtlasBindingCoordinator*,
                                         render::ShadowFrameCoordinator<ecs::Dim3>*,
                                         shadow::ShadowAtlasHost*);
    using ConfigureSceneAnimationFn = void (*)(void*,
                                               render::AnimationFrameCoordinator<ecs::Dim3>*);
    using ConfigurePreSceneAnimationFn = void (*)(void*,
                                                  render::AnimationFrameCoordinator<ecs::Dim3>*);
    using GraphOverlayRecordFn = void (*)(void*,
                                          render_graph::GraphCommandContext&,
                                          render_graph::ResourceHandle);

    static constexpr SceneRecorder3DSceneStage scene_stage_record_order[2] = {
        SceneRecorder3DSceneStage::opaque,
        SceneRecorder3DSceneStage::transparent,
    };

    enum class PreSceneRendererKind : std::uint8_t {
        generic = 0U,
        shadow = 1U,
    };

    struct PreSceneRendererEntry final {
        void* renderer = nullptr;
        PreSceneRendererKind kind = PreSceneRendererKind::generic;
        std::uint8_t reserved0 = 0U;
        std::uint16_t reserved1 = 0U;
        std::uint32_t submission_layer_mask = all_submission_layers;
        PrepareFn prepare_fn = nullptr;
        RecordFn record_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        ConfigurePreSceneAnimationFn configure_animation_fn = nullptr;
    };

    struct SceneRendererEntry final {
        void* renderer = nullptr;
        SceneRenderPassRole pass_role = SceneRenderPassRole::single;
        SceneRecorder3DSceneStage stage = SceneRecorder3DSceneStage::opaque;
        std::uint16_t reserved0 = 0U;
        std::uint32_t submission_layer_mask = all_submission_layers;
        PrepareFn prepare_fn = nullptr;
        GraphSceneRecordFn graph_record_fn = nullptr;
        DescribeGraphBindingsFn describe_graph_bindings_fn = nullptr;
        RegisterGraphImportedResourcesFn register_graph_imported_resources_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        ConfigureLightingFn configure_lighting_fn = nullptr;
        ConfigureSceneAnimationFn configure_animation_fn = nullptr;
    };

    struct OverlayRendererEntry final {
        void* renderer = nullptr;
        std::uint32_t submission_layer_mask = all_submission_layers;
        PrepareFn prepare_fn = nullptr;
        GraphOverlayRecordFn graph_record_fn = nullptr;
        DescribeGraphBindingsFn describe_graph_bindings_fn = nullptr;
        RegisterGraphImportedResourcesFn register_graph_imported_resources_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
    };

    template<typename RendererT>
    static void PrepareRenderer(void* renderer_,
                                const SceneRecorder3DPrepareView& prepare_view_);

    template<typename RendererT>
    static void RecordRenderer(void* renderer_,
                               const FrameRecordContext& record_context_);

    template<typename RendererT>
    static void RecordGraphSceneRenderer(void* renderer_,
                                         render_graph::GraphCommandContext& context_,
                                         SceneRenderStage stage_,
                                         render_graph::ResourceHandle color_target_,
                                         render_graph::ResourceHandle depth_target_);

    template<typename RendererT>
    static constexpr GraphSceneRecordFn ResolveGraphSceneRecordFn() noexcept;

    template<typename RendererT>
    static void RecordGraphOverlayRenderer(void* renderer_,
                                           render_graph::GraphCommandContext& context_,
                                           render_graph::ResourceHandle color_target_);

    template<typename RendererT>
    static constexpr GraphOverlayRecordFn ResolveGraphOverlayRecordFn() noexcept;

    template<typename RendererT>
    static void DescribeGraphBindings(void* renderer_,
                                      render_graph::RenderGraphBuilder& builder_,
                                      render_graph::PassHandle pass_);

    template<typename RendererT>
    static constexpr DescribeGraphBindingsFn ResolveGraphDescriptorBindingsFn() noexcept;

    template<typename RendererT>
    static void RegisterGraphImportedResources(
        void* renderer_,
        runtime::services::RenderGraphRuntimeService& graph_runtime_service_);

    template<typename RendererT>
    static constexpr RegisterGraphImportedResourcesFn ResolveGraphImportedResourcesFn() noexcept;

    template<typename RendererT>
    static void OnSwapchainRecreatedRenderer(void* renderer_,
                                             std::uint32_t image_count_,
                                             VkExtent2D extent_,
                                             VkFormat format_,
                                             std::uint64_t last_submitted_value_,
                                             std::uint64_t completed_submit_value_);

    template<typename RendererT>
    static void OptionalOnSwapchainRecreatedRenderer(void* renderer_,
                                                     std::uint32_t image_count_,
                                                     VkExtent2D extent_,
                                                     VkFormat format_,
                                                     std::uint64_t last_submitted_value_,
                                                     std::uint64_t completed_submit_value_);

    template<typename RendererT>
    static void ConfigureSceneLightingBinding(void* renderer_,
                                              render::LightFrameCoordinator<ecs::Dim3>* light_frame_coordinator_,
                                              render::LightShadowLinkCoordinator3D* light_shadow_link_coordinator_,
                                              render::ShadowAtlasBindingCoordinator* shadow_atlas_binding_coordinator_,
                                              render::ShadowFrameCoordinator<ecs::Dim3>* shadow_frame_coordinator_,
                                              shadow::ShadowAtlasHost* shadow_atlas_host_);

    template<typename RendererT>
    static void ConfigureSceneAnimationBinding(
        void* renderer_,
        render::AnimationFrameCoordinator<ecs::Dim3>* animation_frame_coordinator_);

    template<typename RendererT>
    static void ConfigurePreSceneAnimationBinding(
        void* renderer_,
        render::AnimationFrameCoordinator<ecs::Dim3>* animation_frame_coordinator_);

    template<typename Fn>
    void ForEachSceneRendererInStageOrder(Fn& fn_) const;

    void UpsertPreSceneRendererEntry(const PreSceneRendererEntry& entry_);
    void UpsertSceneRendererEntry(const SceneRendererEntry& entry_);
    void UpsertOverlayRendererEntry(const OverlayRendererEntry& entry_);
    void RefreshFramePacketBinding() noexcept;
    void RefreshSceneLightingBindings() noexcept;
    void RefreshAnimationBindings() noexcept;
    void RefreshRendererCounts() noexcept;
    [[nodiscard]] bool HasSceneViewForSubmission() const noexcept;
    [[nodiscard]] bool ShouldUseBloomChainForSubmission() const noexcept;
    [[nodiscard]] std::uint32_t EffectiveLayerMask() const noexcept;
    [[nodiscard]] std::uint32_t OverlayLayerMask() const noexcept;
    [[nodiscard]] bool IsShadowEnabledForSubmission() const noexcept;
    [[nodiscard]] bool IsOverlayEnabledForSubmission() const noexcept;
    [[nodiscard]] bool IsPostProcessEnabledForSubmission() const noexcept;
    [[nodiscard]] bool SupportsGraphExecution(const VulkanContext& device_) const noexcept;
    [[nodiscard]] bool UsesGraphManagedBloomChain() const noexcept;
    [[nodiscard]] bool HasSkyEnvironmentPassForSubmission() const noexcept;
    [[nodiscard]] bool HasVisibleSceneRendererForSubmission() const noexcept;
    [[nodiscard]] bool HasVisibleOpaqueSceneRendererForSubmission() const noexcept;
    [[nodiscard]] bool HasDepthTargetForSkyAfterOpaqueSubmission() const noexcept;
    [[nodiscard]] scene::SkyEnvironmentDrawOrder SkyEnvironmentDrawOrderForSubmission() const noexcept;
    [[nodiscard]] bool ShouldRecordSkyEnvironmentBeforeOpaque() const noexcept;
    [[nodiscard]] bool ShouldRecordSkyEnvironmentAfterOpaque() const noexcept;
    [[nodiscard]] bool IsLayerVisibleForSubmission(std::uint32_t submission_layer_mask_) const noexcept;
    [[nodiscard]] bool IsOverlayLayerVisibleForSubmission(std::uint32_t submission_layer_mask_) const noexcept;
    [[nodiscard]] bool IsFirstSceneRendererEntryForRenderer(
        const SceneRendererEntry& entry_) const noexcept;
    void EnsureInitialized(const char* operation_) const;
    void EnsureRuntimeBinding(const char* operation_) const;

private:
    SceneRecorder3DCreateInfo create_info_cache{};
    SceneRecorder3DStats stats{};
    SkyEnvironmentPass sky_environment_pass{};
    RenderTargetBloomRenderer bloom_renderer{};
    SceneRecorder3DMcVector<PreSceneRendererEntry> pre_scene_renderer_entries{};
    SceneRecorder3DMcVector<SceneRendererEntry> scene_renderer_entries{};
    SceneRecorder3DMcVector<OverlayRendererEntry> overlay_renderer_entries{};
    VulkanContext* context = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    runtime::services::RenderGraphRuntimeService* graph_runtime_service = nullptr;
    render::LightFrameCoordinator<ecs::Dim3>* light_frame_coordinator = nullptr;
    render::AnimationFrameCoordinator<ecs::Dim3>* animation_frame_coordinator = nullptr;
    render::ShadowFrameCoordinator<ecs::Dim3>* shadow_frame_coordinator = nullptr;
    shadow::ShadowAtlasHost* shadow_atlas_host = nullptr;
    const RenderScenePacket3D* frame_packet = nullptr;
    const RenderView3D* active_view = nullptr;
    const RenderView3D* scene_view = nullptr;
    const RenderView3D* overlay_view = nullptr;
    bool sky_environment_pass_ready = false;
    scene::SkyEnvironmentGpuHandle resolved_environment_gpu{};
    std::uint64_t active_view_signature = 0U;
    render::LightShadowLinkCoordinator3D light_shadow_link_coordinator{};
    render::ShadowAtlasBindingCoordinator shadow_atlas_binding_coordinator{};
    bool initialized = false;
};

} // namespace vr::render

#include "vr/render/detail/scene_recorder_3d_registration_detail.hpp"
