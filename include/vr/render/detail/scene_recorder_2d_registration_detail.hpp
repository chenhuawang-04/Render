#pragma once

#include "vr/render/renderer_prepare_views_2d.hpp"
#include "vr/render/scene_prepare_views.hpp"

namespace vr::render {

template<typename RendererT>
void SceneRecorder2D::RegisterPreSceneRenderer(RendererT& renderer_,
                             std::uint32_t submission_layer_mask_) {
    EnsureInitialized("RegisterPreSceneRenderer");
    const PreSceneRendererEntry entry{
        .renderer = &renderer_,
        .kind = PreSceneRendererKind::generic,
        .reserved0 = 0U,
        .reserved1 = 0U,
        .submission_layer_mask = submission_layer_mask_,
        .prepare_fn = &PrepareRenderer<RendererT>,
        .record_fn = &RecordRenderer<RendererT>,
        .swapchain_recreated_fn = &OptionalOnSwapchainRecreatedRenderer<RendererT>,
    };
    UpsertPreSceneRendererEntry(entry);
}

template<typename RendererT>
void SceneRecorder2D::RegisterSceneRenderer(RendererT& renderer_,
                           SceneRenderPassRole pass_role_,
                           std::uint32_t submission_layer_mask_) {
    EnsureInitialized("RegisterSceneRenderer");
    const SceneRendererEntry entry{
        .renderer = &renderer_,
        .pass_role = pass_role_,
        .submission_layer_mask = submission_layer_mask_,
        .prepare_fn = &PrepareRenderer<RendererT>,
        .graph_record_fn = ResolveGraphRecordFn<RendererT>(),
        .describe_graph_bindings_fn = ResolveGraphDescriptorBindingsFn<RendererT>(),
        .register_graph_imported_resources_fn =
            ResolveGraphImportedResourcesFn<RendererT>(),
        .swapchain_recreated_fn = &OnSwapchainRecreatedRenderer<RendererT>,
        .configure_lighting_fn = &ConfigureSceneLightingBinding<RendererT>,
    };
    UpsertSceneRendererEntry(entry);
}

template<typename RendererT>
void SceneRecorder2D::RegisterSceneConsumer(RendererT& renderer_,
                           std::uint32_t submission_layer_mask_) {
    EnsureInitialized("RegisterSceneConsumer");
    const SceneConsumerEntry entry{
        .renderer = &renderer_,
        .submission_layer_mask = submission_layer_mask_,
        .prepare_fn = &PrepareRenderer<RendererT>,
        .graph_record_fn = ResolveSceneConsumerGraphRecordFn<RendererT>(),
        .build_graph_color_attachment_fn = ResolveSceneConsumerGraphColorAttachmentFn<RendererT>(),
        .describe_graph_bindings_fn = ResolveGraphDescriptorBindingsFn<RendererT>(),
        .register_graph_imported_resources_fn =
            ResolveGraphImportedResourcesFn<RendererT>(),
        .swapchain_recreated_fn = &OnSwapchainRecreatedRenderer<RendererT>,
    };
    SetSceneConsumerEntry(entry);
}

template<typename RendererT>
void SceneRecorder2D::RegisterOverlayRenderer(RendererT& renderer_,
                             std::uint32_t submission_layer_mask_) {
    EnsureInitialized("RegisterOverlayRenderer");
    const OverlayRendererEntry entry{
        .renderer = &renderer_,
        .submission_layer_mask = submission_layer_mask_,
        .prepare_fn = &PrepareRenderer<RendererT>,
        .graph_record_fn = ResolveGraphRecordFn<RendererT>(),
        .describe_graph_bindings_fn = ResolveGraphDescriptorBindingsFn<RendererT>(),
        .register_graph_imported_resources_fn =
            ResolveGraphImportedResourcesFn<RendererT>(),
        .swapchain_recreated_fn = &OnSwapchainRecreatedRenderer<RendererT>,
    };
    UpsertOverlayRendererEntry(entry);
}

template<typename RendererT>
void SceneRecorder2D::PrepareRenderer(void* renderer_,
                            const SceneRecorder2DPrepareView& prepare_view_) {
    RendererT& renderer_ref = *static_cast<RendererT*>(renderer_);
    if constexpr (requires(RendererT& candidate_,
                           const RenderTargetCompositeRendererPrepareView& renderer_prepare_view_) {
                      candidate_.PrepareFrame(renderer_prepare_view_);
                  }) {
        renderer_ref.PrepareFrame(MakeRenderTargetCompositeRendererPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                           const TextRenderer2DPrepareView& renderer_prepare_view_) {
                      candidate_.PrepareFrame(renderer_prepare_view_);
                  }) {
        renderer_ref.PrepareFrame(MakeTextRenderer2DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const GeometryRenderer2DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeGeometryRenderer2DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const SurfaceRenderer2DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeSurfaceRenderer2DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const ParticleRenderer2DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeParticleRenderer2DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const ShadowRenderer2DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeShadowRenderer2DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const SceneRecorder2DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(prepare_view_);
    } else {
        static_assert(sizeof(RendererT) == 0,
                      "SceneRecorder2D renderer must expose a typed PrepareFrame overload");
    }
}

template<typename RendererT>
void SceneRecorder2D::RecordRenderer(void* renderer_,
                           const FrameRecordContext& record_context_) {
    static_cast<RendererT*>(renderer_)->Record(record_context_);
}

template<typename RendererT>
void SceneRecorder2D::RecordGraphRenderer(void* renderer_,
                                render_graph::GraphCommandContext& context_,
                                render_graph::ResourceHandle color_target_) {
    if constexpr (SceneRecorder2DGraphColorPassRecordable<RendererT>) {
        static_cast<RendererT*>(renderer_)->RecordGraphColorPass(context_, color_target_);
    } else {
        static_cast<RendererT*>(renderer_)->RecordGraphOverlay(context_, color_target_);
    }
}

template<typename RendererT>
constexpr SceneRecorder2D::GraphRecordFn SceneRecorder2D::ResolveGraphRecordFn() noexcept {
    if constexpr (SceneRecorder2DGraphRecordSupport<RendererT>::value) {
        return &RecordGraphRenderer<RendererT>;
    } else {
        return nullptr;
    }
}

template<typename RendererT>
void SceneRecorder2D::RecordGraphSceneConsumer(void* renderer_,
                                     render_graph::GraphCommandContext& context_,
                                     render_graph::ResourceHandle source_color_,
                                     render_graph::ResourceHandle output_target_) {
    static_cast<RendererT*>(renderer_)->RecordGraphPass(context_, source_color_, output_target_);
}

template<typename RendererT>
constexpr SceneRecorder2D::GraphSceneConsumerRecordFn SceneRecorder2D::ResolveSceneConsumerGraphRecordFn() noexcept {
    if constexpr (SceneRecorder2DGraphSceneConsumerRecordable<RendererT>) {
        return &RecordGraphSceneConsumer<RendererT>;
    } else {
        return nullptr;
    }
}

template<typename RendererT>
render_graph::RasterColorAttachmentDesc SceneRecorder2D::BuildSceneConsumerGraphColorAttachment(
    void* renderer_,
    render_graph::ResourceHandle output_target_,
    bool has_previous_content_) {
    return static_cast<RendererT*>(renderer_)->BuildGraphColorAttachmentDesc(output_target_,
                                                                             has_previous_content_);
}

template<typename RendererT>
constexpr SceneRecorder2D::BuildGraphColorAttachmentFn SceneRecorder2D::ResolveSceneConsumerGraphColorAttachmentFn() noexcept {
    if constexpr (SceneRecorder2DGraphSceneConsumerAttachmentDescribable<RendererT>) {
        return &BuildSceneConsumerGraphColorAttachment<RendererT>;
    } else {
        return nullptr;
    }
}

template<typename RendererT>
void SceneRecorder2D::DescribeGraphBindings(void* renderer_,
                                  render_graph::RenderGraphBuilder& builder_,
                                  render_graph::PassHandle pass_) {
    static_cast<RendererT*>(renderer_)->DescribeGraphDescriptorBindings(builder_, pass_);
}

template<typename RendererT>
constexpr SceneRecorder2D::DescribeGraphBindingsFn SceneRecorder2D::ResolveGraphDescriptorBindingsFn() noexcept {
    if constexpr (SceneRecorder2DGraphDescriptorBindable<RendererT>) {
        return &DescribeGraphBindings<RendererT>;
    } else {
        return nullptr;
    }
}

template<typename RendererT>
void SceneRecorder2D::RegisterGraphImportedResources(
    void* renderer_,
    runtime::services::RenderGraphRuntimeService& graph_runtime_service_) {
    static_cast<RendererT*>(renderer_)->RegisterGraphImportedResources(
        graph_runtime_service_);
}

template<typename RendererT>
constexpr SceneRecorder2D::RegisterGraphImportedResourcesFn SceneRecorder2D::ResolveGraphImportedResourcesFn() noexcept {
    if constexpr (SceneRecorder2DGraphImportedResourceRegistrable<RendererT>) {
        return &RegisterGraphImportedResources<RendererT>;
    } else {
        return nullptr;
    }
}

template<typename RendererT>
void SceneRecorder2D::OnSwapchainRecreatedRenderer(void* renderer_,
                                         std::uint32_t image_count_,
                                         VkExtent2D extent_,
                                         VkFormat format_,
                                         std::uint64_t last_submitted_value_,
                                         std::uint64_t completed_submit_value_) {
    if constexpr (requires(RendererT& renderer_ref_) {
                      renderer_ref_.OnSwapchainRecreated(image_count_,
                                                         extent_,
                                                         format_,
                                                         last_submitted_value_,
                                                         completed_submit_value_);
                  }) {
        static_cast<RendererT*>(renderer_)->OnSwapchainRecreated(image_count_,
                                                                 extent_,
                                                                 format_,
                                                                 last_submitted_value_,
                                                                 completed_submit_value_);
    } else {
        static_cast<RendererT*>(renderer_)->OnSwapchainRecreated(image_count_,
                                                                 extent_,
                                                                 format_);
    }
}

template<typename RendererT>
void SceneRecorder2D::OptionalOnSwapchainRecreatedRenderer(void* renderer_,
                                                 std::uint32_t image_count_,
                                                 VkExtent2D extent_,
                                                 VkFormat format_,
                                                 std::uint64_t last_submitted_value_,
                                                 std::uint64_t completed_submit_value_) {
    if constexpr (requires(RendererT& renderer_ref_) {
                      renderer_ref_.OnSwapchainRecreated(image_count_,
                                                         extent_,
                                                         format_,
                                                         last_submitted_value_,
                                                         completed_submit_value_);
                  }) {
        static_cast<RendererT*>(renderer_)->OnSwapchainRecreated(image_count_,
                                                                 extent_,
                                                                 format_,
                                                                 last_submitted_value_,
                                                                 completed_submit_value_);
    } else if constexpr (requires(RendererT& renderer_ref_) {
                             renderer_ref_.OnSwapchainRecreated(image_count_,
                                                                extent_,
                                                                format_);
                         }) {
        static_cast<RendererT*>(renderer_)->OnSwapchainRecreated(image_count_,
                                                                 extent_,
                                                                 format_);
    }
}

template<typename RendererT>
void SceneRecorder2D::ConfigureSceneLightingBinding(void* renderer_,
                                          render::LightFrameCoordinator<ecs::Dim2>* light_frame_coordinator_,
                                          render::LightShadowLinkCoordinator2D* light_shadow_link_coordinator_,
                                          render::ShadowAtlasBindingCoordinator* shadow_atlas_binding_coordinator_,
                                          render::ShadowFrameCoordinator<ecs::Dim2>* shadow_frame_coordinator_,
                                          shadow::ShadowAtlasHost* shadow_atlas_host_) {
    RendererT& renderer_ref = *static_cast<RendererT*>(renderer_);
    if constexpr (requires(RendererT& candidate_,
                           render::LightFrameCoordinator<ecs::Dim2>* coordinator_) {
                      candidate_.SetLightFrameCoordinator(coordinator_);
                  }) {
        renderer_ref.SetLightFrameCoordinator(light_frame_coordinator_);
    }
    if constexpr (requires(RendererT& candidate_,
                           render::LightShadowLinkCoordinator2D* coordinator_) {
                      candidate_.SetLightShadowLinkCoordinator(coordinator_);
                  }) {
        renderer_ref.SetLightShadowLinkCoordinator(light_shadow_link_coordinator_);
    }
    if constexpr (requires(RendererT& candidate_,
                           render::ShadowAtlasBindingCoordinator* coordinator_) {
                      candidate_.SetShadowAtlasBindingCoordinator(coordinator_);
                  }) {
        renderer_ref.SetShadowAtlasBindingCoordinator(shadow_atlas_binding_coordinator_);
    }
    if constexpr (requires(RendererT& candidate_,
                           render::ShadowFrameCoordinator<ecs::Dim2>* coordinator_) {
                      candidate_.SetShadowFrameCoordinator(coordinator_);
                  }) {
        renderer_ref.SetShadowFrameCoordinator(shadow_frame_coordinator_);
    }
    if constexpr (requires(RendererT& candidate_,
                           shadow::ShadowAtlasHost* host_) {
                      candidate_.SetShadowAtlasHost(host_);
                  }) {
        renderer_ref.SetShadowAtlasHost(shadow_atlas_host_);
    }
}

} // namespace vr::render
