#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/environment/background_pass_2d.hpp"
#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/light_shadow_link_coordinator.hpp"
#include "vr/render/scene_prepare_views.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render/scene_render_target_set.hpp"
#include "vr/render/shadow_atlas_binding_coordinator.hpp"
#include "vr/render/shadow_frame_coordinator.hpp"
#include "vr/shadow/shadow_atlas_host.hpp"
#include "vr/shadow/shadow_renderer_2d.hpp"

#include <cstdint>
#include <stdexcept>

namespace vr::runtime::services {
class RenderGraphRuntimeService;
}

namespace vr::render {

template<typename T>
using SceneRecorder2DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SceneRecorder2DCreateInfo final {
    SceneRenderTargetSetCreateInfo scene_target = [] {
        SceneRenderTargetSetCreateInfo create_info{};
        create_info.enable_depth = false;
        return create_info;
    }();
    std::uint32_t reserve_pre_scene_renderer_count = 1U;
    std::uint32_t reserve_scene_renderer_count = 4U;
    std::uint32_t reserve_overlay_renderer_count = 2U;
};

struct SceneRecorder2DStats final {
    std::uint32_t pre_scene_renderer_count = 0U;
    std::uint32_t scene_renderer_count = 0U;
    std::uint32_t scene_consumer_count = 0U;
    std::uint32_t overlay_renderer_count = 0U;
    std::uint32_t prepare_count = 0U;
    std::uint32_t record_count = 0U;
    std::uint32_t swapchain_recreate_count = 0U;
    std::uint32_t frame_packet_bind_count = 0U;
    std::uint32_t frame_packet_prepare_count = 0U;
    std::uint32_t frame_packet_record_count = 0U;
    std::uint32_t background_prepare_count = 0U;
    std::uint32_t background_record_count = 0U;
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

template<typename RendererT>
concept SceneRecorder2DGraphColorPassRecordable =
    requires {
        &RendererT::RecordGraphColorPass;
    };

template<typename RendererT>
concept SceneRecorder2DGraphOverlayRecordable =
    requires {
        &RendererT::RecordGraphOverlay;
    };

template<typename RendererT>
struct SceneRecorder2DGraphRecordSupport
    : std::bool_constant<SceneRecorder2DGraphColorPassRecordable<RendererT> ||
                         SceneRecorder2DGraphOverlayRecordable<RendererT>> {};

template<typename RendererT>
concept SceneRecorder2DGraphSceneConsumerRecordable =
    requires {
        &RendererT::RecordGraphPass;
    };

template<typename RendererT>
concept SceneRecorder2DGraphSceneConsumerAttachmentDescribable =
    requires {
        &RendererT::BuildGraphColorAttachmentDesc;
    };

template<typename RendererT>
concept SceneRecorder2DGraphDescriptorBindable =
    requires {
        &RendererT::DescribeGraphDescriptorBindings;
    };

template<typename RendererT>
concept SceneRecorder2DGraphImportedResourceRegistrable =
    requires(RendererT& renderer_,
             runtime::services::RenderGraphRuntimeService& graph_runtime_service_) {
        renderer_.RegisterGraphImportedResources(graph_runtime_service_);
    };

class SceneRecorder2D final {
public:
    SceneRecorder2D() = default;
    ~SceneRecorder2D() = default;

    SceneRecorder2D(const SceneRecorder2D&) = delete;
    SceneRecorder2D& operator=(const SceneRecorder2D&) = delete;
    SceneRecorder2D(SceneRecorder2D&&) = delete;
    SceneRecorder2D& operator=(SceneRecorder2D&&) = delete;

    void Initialize(const SceneRecorder2DCreateInfo& create_info_ = {}) noexcept;
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

    void BindLightFrameCoordinator(render::LightFrameCoordinator<ecs::Dim2>* light_frame_coordinator_) noexcept;
    void BindShadowRuntime(render::ShadowFrameCoordinator<ecs::Dim2>* shadow_frame_coordinator_,
                           shadow::ShadowAtlasHost* shadow_atlas_host_) noexcept;
    void ClearShadowRuntimeBinding() noexcept;

    void SetFramePacket(const RenderScenePacket2D* frame_packet_) noexcept;
    void ClearFramePacket() noexcept;

    template<typename RendererT>
    void RegisterPreSceneRenderer(RendererT& renderer_,
                                  std::uint32_t submission_layer_mask_ = all_submission_layers);

    void RegisterShadowRenderer(shadow::ShadowRenderer2D& shadow_renderer_,
                                std::uint32_t submission_layer_mask_ = all_submission_layers);

    template<typename RendererT>
    void RegisterSceneRenderer(RendererT& renderer_,
                               SceneRenderPassRole pass_role_,
                               std::uint32_t submission_layer_mask_ = all_submission_layers);

    template<typename RendererT>
    void RegisterSceneConsumer(RendererT& renderer_,
                               std::uint32_t submission_layer_mask_ = all_submission_layers);

    template<typename RendererT>
    void RegisterOverlayRenderer(RendererT& renderer_,
                                 std::uint32_t submission_layer_mask_ = all_submission_layers);

    void ClearPreSceneRenderers() noexcept;
    void ClearSceneRenderers() noexcept;
    void ClearSceneConsumer() noexcept;
    void ClearOverlayRenderers() noexcept;
    void ClearRendererRegistrations() noexcept;

    void PrepareFrame(const SceneRecorder2DPrepareView& prepare_view_);
    void PrepareFrame(const SceneRecorder2DPrepareView& prepare_view_,
                      const RenderScenePacket2D& frame_packet_);
    void BuildRenderGraph(render_graph::RenderGraphBuilder& builder_,
                          const render_graph::FrameSnapshot2D& snapshot_,
                          const render_graph::MinimalFrameGraphBuildResult<ecs::Dim2>& build_result_,
                          render_graph::ResourceVersionHandle& color_chain_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool HasRuntimeBinding() const noexcept;
    [[nodiscard]] bool HasSceneConsumer() const noexcept;
    [[nodiscard]] const SceneRecorder2DCreateInfo& CreateInfo() const noexcept;
    [[nodiscard]] const SceneRecorder2DStats& Stats() const noexcept;
    [[nodiscard]] const RenderScenePacket2D* FramePacket() const noexcept;
    [[nodiscard]] const RenderView2D* ActiveView() const noexcept;

private:
    static constexpr std::uint32_t all_submission_layers = 0xFFFF'FFFFU;

    using PrepareFn = void (*)(void*, const SceneRecorder2DPrepareView&);
    using RecordFn = void (*)(void*, const FrameRecordContext&);
    using GraphRecordFn = void (*)(void*,
                                   render_graph::GraphCommandContext&,
                                   render_graph::ResourceHandle);
    using GraphSceneConsumerRecordFn = void (*)(void*,
                                                render_graph::GraphCommandContext&,
                                                render_graph::ResourceHandle,
                                                render_graph::ResourceHandle);
    using BuildGraphColorAttachmentFn = render_graph::RasterColorAttachmentDesc (*)(
        void*,
        render_graph::ResourceHandle,
        bool);
    using DescribeGraphBindingsFn = void (*)(void*,
                                             render_graph::RenderGraphBuilder&,
                                             render_graph::PassHandle);
    using RegisterGraphImportedResourcesFn = void (*)(
        void*,
        runtime::services::RenderGraphRuntimeService&);
    using SwapchainRecreatedFn = void (*)(void*,
                                          std::uint32_t,
                                          VkExtent2D,
                                          VkFormat,
                                          std::uint64_t,
                                          std::uint64_t);
    using ConfigureLightingFn = void (*)(void*,
                                         render::LightFrameCoordinator<ecs::Dim2>*,
                                         render::LightShadowLinkCoordinator2D*,
                                         render::ShadowAtlasBindingCoordinator*,
                                         render::ShadowFrameCoordinator<ecs::Dim2>*,
                                         shadow::ShadowAtlasHost*);

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
    };

    struct SceneRendererEntry final {
        void* renderer = nullptr;
        SceneRenderPassRole pass_role = SceneRenderPassRole::single;
        std::uint32_t submission_layer_mask = all_submission_layers;
        PrepareFn prepare_fn = nullptr;
        GraphRecordFn graph_record_fn = nullptr;
        DescribeGraphBindingsFn describe_graph_bindings_fn = nullptr;
        RegisterGraphImportedResourcesFn register_graph_imported_resources_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        ConfigureLightingFn configure_lighting_fn = nullptr;
    };

    struct SceneConsumerEntry final {
        void* renderer = nullptr;
        std::uint32_t submission_layer_mask = all_submission_layers;
        PrepareFn prepare_fn = nullptr;
        GraphSceneConsumerRecordFn graph_record_fn = nullptr;
        BuildGraphColorAttachmentFn build_graph_color_attachment_fn = nullptr;
        DescribeGraphBindingsFn describe_graph_bindings_fn = nullptr;
        RegisterGraphImportedResourcesFn register_graph_imported_resources_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
    };

    struct OverlayRendererEntry final {
        void* renderer = nullptr;
        std::uint32_t submission_layer_mask = all_submission_layers;
        PrepareFn prepare_fn = nullptr;
        GraphRecordFn graph_record_fn = nullptr;
        DescribeGraphBindingsFn describe_graph_bindings_fn = nullptr;
        RegisterGraphImportedResourcesFn register_graph_imported_resources_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
    };

    template<typename RendererT>
    static void PrepareRenderer(void* renderer_,
                                const SceneRecorder2DPrepareView& prepare_view_);

    template<typename RendererT>
    static void RecordRenderer(void* renderer_,
                               const FrameRecordContext& record_context_);

    template<typename RendererT>
    static void RecordGraphRenderer(void* renderer_,
                                    render_graph::GraphCommandContext& context_,
                                    render_graph::ResourceHandle color_target_);

    template<typename RendererT>
    static constexpr GraphRecordFn ResolveGraphRecordFn() noexcept;

    template<typename RendererT>
    static void RecordGraphSceneConsumer(void* renderer_,
                                         render_graph::GraphCommandContext& context_,
                                         render_graph::ResourceHandle source_color_,
                                         render_graph::ResourceHandle output_target_);

    template<typename RendererT>
    static constexpr GraphSceneConsumerRecordFn ResolveSceneConsumerGraphRecordFn() noexcept;

    template<typename RendererT>
    static render_graph::RasterColorAttachmentDesc BuildSceneConsumerGraphColorAttachment(
        void* renderer_,
        render_graph::ResourceHandle output_target_,
        bool has_previous_content_);

    template<typename RendererT>
    static constexpr BuildGraphColorAttachmentFn ResolveSceneConsumerGraphColorAttachmentFn() noexcept;

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
                                              render::LightFrameCoordinator<ecs::Dim2>* light_frame_coordinator_,
                                              render::LightShadowLinkCoordinator2D* light_shadow_link_coordinator_,
                                              render::ShadowAtlasBindingCoordinator* shadow_atlas_binding_coordinator_,
                                              render::ShadowFrameCoordinator<ecs::Dim2>* shadow_frame_coordinator_,
                                              shadow::ShadowAtlasHost* shadow_atlas_host_);

    void UpsertPreSceneRendererEntry(const PreSceneRendererEntry& entry_);
    void UpsertSceneRendererEntry(const SceneRendererEntry& entry_);
    void SetSceneConsumerEntry(const SceneConsumerEntry& entry_) noexcept;
    void UpsertOverlayRendererEntry(const OverlayRendererEntry& entry_);
    void RefreshFramePacketBinding() noexcept;
    void RefreshSceneLightingBindings() noexcept;
    void RefreshRendererCounts() noexcept;
    [[nodiscard]] bool HasSceneViewForSubmission() const noexcept;
    [[nodiscard]] std::uint32_t EffectiveLayerMask() const noexcept;
    [[nodiscard]] std::uint32_t OverlayLayerMask() const noexcept;
    [[nodiscard]] bool IsShadowEnabledForSubmission() const noexcept;
    [[nodiscard]] bool IsOverlayEnabledForSubmission() const noexcept;
    [[nodiscard]] bool IsPostProcessEnabledForSubmission() const noexcept;
    [[nodiscard]] bool HasBackgroundPassForSubmission() const noexcept;
    [[nodiscard]] bool IsLayerVisibleForSubmission(std::uint32_t submission_layer_mask_) const noexcept;
    [[nodiscard]] bool IsOverlayLayerVisibleForSubmission(std::uint32_t submission_layer_mask_) const noexcept;
    [[nodiscard]] bool SupportsGraphExecution(const VulkanContext& device_) const noexcept;
    [[nodiscard]] bool UsesGraphManagedSceneOutput() const noexcept;
    void EnsureInitialized(const char* operation_) const;
    void EnsureRuntimeBinding(const char* operation_) const;

private:
    SceneRecorder2DCreateInfo create_info_cache{};
    SceneRecorder2DStats stats{};
    BackgroundPass2D background_pass{};
    SceneRecorder2DMcVector<PreSceneRendererEntry> pre_scene_renderer_entries{};
    SceneRecorder2DMcVector<SceneRendererEntry> scene_renderer_entries{};
    SceneConsumerEntry scene_consumer_entry{};
    SceneRecorder2DMcVector<OverlayRendererEntry> overlay_renderer_entries{};
    VulkanContext* context = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    runtime::services::RenderGraphRuntimeService* graph_runtime_service = nullptr;
    render::LightFrameCoordinator<ecs::Dim2>* light_frame_coordinator = nullptr;
    render::ShadowFrameCoordinator<ecs::Dim2>* shadow_frame_coordinator = nullptr;
    shadow::ShadowAtlasHost* shadow_atlas_host = nullptr;
    const RenderScenePacket2D* frame_packet = nullptr;
    const RenderView2D* active_view = nullptr;
    const RenderView2D* scene_view = nullptr;
    const RenderView2D* overlay_view = nullptr;
    std::uint64_t active_view_signature = 0U;
    render::LightShadowLinkCoordinator2D light_shadow_link_coordinator{};
    render::ShadowAtlasBindingCoordinator shadow_atlas_binding_coordinator{};
    bool initialized = false;
};

} // namespace vr::render

#include "vr/render/detail/scene_recorder_2d_registration_detail.hpp"

