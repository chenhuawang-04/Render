#pragma once

#include "vr/render/renderer_prepare_views_2d.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"

#include <type_traits>

namespace vr::render::detail {

template<typename RendererT>
concept SceneRecorder3DGraphSceneRecordable =
    requires {
        &RendererT::RecordGraphSceneStage;
    };

template<typename RendererT>
concept SceneRecorder3DGraphOverlayRecordable =
    requires {
        &RendererT::RecordGraphOverlay;
    };

template<typename RendererT>
concept SceneRecorder3DGraphDescriptorBindable =
    requires {
        &RendererT::DescribeGraphDescriptorBindings;
    };

template<typename RendererT>
concept SceneRecorder3DGraphImportedResourceRegistrable =
    requires(RendererT& renderer_,
             runtime::services::RenderGraphRuntimeService& graph_runtime_service_) {
        renderer_.RegisterGraphImportedResources(graph_runtime_service_);
    };

template<typename RendererT>
struct SceneRecorder3DGraphSceneSupport : std::bool_constant<SceneRecorder3DGraphSceneRecordable<RendererT>> {};

template<>
struct SceneRecorder3DGraphSceneSupport<geometry::GeometryRenderer3D> : std::true_type {};

template<>
struct SceneRecorder3DGraphSceneSupport<surface::SurfaceRenderer3D> : std::true_type {};

template<>
struct SceneRecorder3DGraphSceneSupport<text::TextRenderer3D> : std::true_type {};

template<>
struct SceneRecorder3DGraphSceneSupport<particle::ParticleRenderer3D> : std::true_type {};

template<typename RendererT>
struct SceneRecorder3DGraphOverlaySupport : std::bool_constant<SceneRecorder3DGraphOverlayRecordable<RendererT>> {};

template<>
struct SceneRecorder3DGraphOverlaySupport<text::TextRenderer2D> : std::true_type {};

template<>
struct SceneRecorder3DGraphOverlaySupport<surface::SurfaceRenderer2D> : std::true_type {};

template<>
struct SceneRecorder3DGraphOverlaySupport<particle::ParticleRenderer2D> : std::true_type {};

} // namespace vr::render::detail

namespace vr::render {

template<typename RendererT>
void SceneRecorder3D::RegisterPreSceneRenderer(RendererT& renderer_,
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
        .configure_animation_fn = &ConfigurePreSceneAnimationBinding<RendererT>,
    };
    UpsertPreSceneRendererEntry(entry);
}

template<typename RendererT>
void SceneRecorder3D::RegisterSceneRenderer(RendererT& renderer_,
                                            SceneRenderPassRole pass_role_,
                                            SceneRecorder3DSceneStage stage_,
                                            std::uint32_t submission_layer_mask_) {
    EnsureInitialized("RegisterSceneRenderer");
    const SceneRendererEntry entry{
        .renderer = &renderer_,
        .pass_role = pass_role_,
        .stage = stage_,
        .reserved0 = 0U,
        .submission_layer_mask = submission_layer_mask_,
        .prepare_fn = &PrepareRenderer<RendererT>,
        .graph_record_fn = ResolveGraphSceneRecordFn<RendererT>(),
        .describe_graph_bindings_fn = ResolveGraphDescriptorBindingsFn<RendererT>(),
        .register_graph_imported_resources_fn =
            ResolveGraphImportedResourcesFn<RendererT>(),
        .swapchain_recreated_fn = &OnSwapchainRecreatedRenderer<RendererT>,
        .configure_lighting_fn = &ConfigureSceneLightingBinding<RendererT>,
        .configure_animation_fn = &ConfigureSceneAnimationBinding<RendererT>,
    };
    UpsertSceneRendererEntry(entry);
}

template<typename RendererT>
void SceneRecorder3D::RegisterOpaqueSceneRenderer(RendererT& renderer_,
                                                  SceneRenderPassRole pass_role_,
                                                  std::uint32_t submission_layer_mask_) {
    RegisterSceneRenderer(renderer_,
                          pass_role_,
                          SceneRecorder3DSceneStage::opaque,
                          submission_layer_mask_);
}

template<typename RendererT>
void SceneRecorder3D::RegisterTransparentSceneRenderer(RendererT& renderer_,
                                                       SceneRenderPassRole pass_role_,
                                                       std::uint32_t submission_layer_mask_) {
    RegisterSceneRenderer(renderer_,
                          pass_role_,
                          SceneRecorder3DSceneStage::transparent,
                          submission_layer_mask_);
}

template<typename RendererT>
void SceneRecorder3D::RegisterOverlayRenderer(RendererT& renderer_,
                                              std::uint32_t submission_layer_mask_) {
    EnsureInitialized("RegisterOverlayRenderer");
    const OverlayRendererEntry entry{
        .renderer = &renderer_,
        .submission_layer_mask = submission_layer_mask_,
        .prepare_fn = &PrepareRenderer<RendererT>,
        .graph_record_fn = ResolveGraphOverlayRecordFn<RendererT>(),
        .describe_graph_bindings_fn = ResolveGraphDescriptorBindingsFn<RendererT>(),
        .register_graph_imported_resources_fn =
            ResolveGraphImportedResourcesFn<RendererT>(),
        .swapchain_recreated_fn = &OptionalOnSwapchainRecreatedRenderer<RendererT>,
    };
    UpsertOverlayRendererEntry(entry);
}

template<typename RendererT>
void SceneRecorder3D::PrepareRenderer(void* renderer_,
                                      const SceneRecorder3DPrepareView& prepare_view_) {
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
                                  const TextRenderer3DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeTextRenderer3DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const IblBakeCoordinatorPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeIblBakeCoordinatorPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const ParticleRenderer3DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeParticleRenderer3DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const ParticleRenderer2DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeParticleRenderer2DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const ShadowRenderer3DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeShadowRenderer3DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const GeometryRenderer2DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeGeometryRenderer2DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const GeometryRenderer3DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeGeometryRenderer3DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const SurfaceRenderer2DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeSurfaceRenderer2DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const SurfaceRenderer3DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(MakeSurfaceRenderer3DPrepareView(prepare_view_));
    } else if constexpr (requires(RendererT& candidate_,
                                  const SceneRecorder3DPrepareView& renderer_prepare_view_) {
                             candidate_.PrepareFrame(renderer_prepare_view_);
                         }) {
        renderer_ref.PrepareFrame(prepare_view_);
    } else {
        static_assert(sizeof(RendererT) == 0,
                      "SceneRecorder3D renderer must expose a typed PrepareFrame overload");
    }
}

template<typename RendererT>
void SceneRecorder3D::RecordRenderer(void* renderer_,
                                     const FrameRecordContext& record_context_) {
    static_cast<RendererT*>(renderer_)->Record(record_context_);
}

template<typename RendererT>
void SceneRecorder3D::RecordGraphSceneRenderer(void* renderer_,
                                               render_graph::GraphCommandContext& context_,
                                               SceneRenderStage stage_,
                                               render_graph::ResourceHandle color_target_,
                                               render_graph::ResourceHandle depth_target_) {
    static_cast<RendererT*>(renderer_)->RecordGraphSceneStage(context_,
                                                              stage_,
                                                              color_target_,
                                                              depth_target_);
}

template<typename RendererT>
constexpr SceneRecorder3D::GraphSceneRecordFn
SceneRecorder3D::ResolveGraphSceneRecordFn() noexcept {
    if constexpr (detail::SceneRecorder3DGraphSceneSupport<RendererT>::value) {
        return &RecordGraphSceneRenderer<RendererT>;
    } else {
        return nullptr;
    }
}

template<typename RendererT>
void SceneRecorder3D::RecordGraphOverlayRenderer(
    void* renderer_,
    render_graph::GraphCommandContext& context_,
    render_graph::ResourceHandle color_target_) {
    static_cast<RendererT*>(renderer_)->RecordGraphOverlay(context_, color_target_);
}

template<typename RendererT>
constexpr SceneRecorder3D::GraphOverlayRecordFn
SceneRecorder3D::ResolveGraphOverlayRecordFn() noexcept {
    if constexpr (detail::SceneRecorder3DGraphOverlaySupport<RendererT>::value) {
        return &RecordGraphOverlayRenderer<RendererT>;
    } else {
        return nullptr;
    }
}

template<typename RendererT>
void SceneRecorder3D::DescribeGraphBindings(void* renderer_,
                                            render_graph::RenderGraphBuilder& builder_,
                                            render_graph::PassHandle pass_) {
    static_cast<RendererT*>(renderer_)->DescribeGraphDescriptorBindings(builder_, pass_);
}

template<typename RendererT>
constexpr SceneRecorder3D::DescribeGraphBindingsFn
SceneRecorder3D::ResolveGraphDescriptorBindingsFn() noexcept {
    if constexpr (detail::SceneRecorder3DGraphDescriptorBindable<RendererT>) {
        return &DescribeGraphBindings<RendererT>;
    } else {
        return nullptr;
    }
}

template<typename RendererT>
void SceneRecorder3D::RegisterGraphImportedResources(
    void* renderer_,
    runtime::services::RenderGraphRuntimeService& graph_runtime_service_) {
    static_cast<RendererT*>(renderer_)->RegisterGraphImportedResources(
        graph_runtime_service_);
}

template<typename RendererT>
constexpr SceneRecorder3D::RegisterGraphImportedResourcesFn
SceneRecorder3D::ResolveGraphImportedResourcesFn() noexcept {
    if constexpr (detail::SceneRecorder3DGraphImportedResourceRegistrable<RendererT>) {
        return &RegisterGraphImportedResources<RendererT>;
    } else {
        return nullptr;
    }
}

template<typename RendererT>
void SceneRecorder3D::OnSwapchainRecreatedRenderer(void* renderer_,
                                                   std::uint32_t image_count_,
                                                   VkExtent2D extent_,
                                                   VkFormat format_,
                                                   std::uint64_t last_submitted_value_,
                                                   std::uint64_t completed_submit_value_) {
    static_cast<RendererT*>(renderer_)->OnSwapchainRecreated(image_count_,
                                                             extent_,
                                                             format_,
                                                             last_submitted_value_,
                                                             completed_submit_value_);
}

template<typename RendererT>
void SceneRecorder3D::OptionalOnSwapchainRecreatedRenderer(
    void* renderer_,
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
    }
}

template<typename RendererT>
void SceneRecorder3D::ConfigureSceneLightingBinding(
    void* renderer_,
    render::LightFrameCoordinator<ecs::Dim3>* light_frame_coordinator_,
    render::LightShadowLinkCoordinator3D* light_shadow_link_coordinator_,
    render::ShadowAtlasBindingCoordinator* shadow_atlas_binding_coordinator_,
    render::ShadowFrameCoordinator<ecs::Dim3>* shadow_frame_coordinator_,
    shadow::ShadowAtlasHost* shadow_atlas_host_) {
    RendererT& renderer_ref = *static_cast<RendererT*>(renderer_);
    if constexpr (requires(RendererT& candidate_,
                           render::LightFrameCoordinator<ecs::Dim3>* coordinator_) {
                      candidate_.SetLightFrameCoordinator(coordinator_);
                  }) {
        renderer_ref.SetLightFrameCoordinator(light_frame_coordinator_);
    }
    if constexpr (requires(RendererT& candidate_,
                           render::LightShadowLinkCoordinator3D* coordinator_) {
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
                           render::ShadowFrameCoordinator<ecs::Dim3>* coordinator_) {
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

template<typename RendererT>
void SceneRecorder3D::ConfigureSceneAnimationBinding(
    void* renderer_,
    render::AnimationFrameCoordinator<ecs::Dim3>* animation_frame_coordinator_) {
    RendererT& renderer_ref = *static_cast<RendererT*>(renderer_);
    if (animation_frame_coordinator_ != nullptr) {
        animation_frame_coordinator_->ApplyToSceneRenderer(renderer_ref);
        return;
    }
    if constexpr (requires(RendererT& candidate_) {
                      candidate_.SetAnimationOutputs(static_cast<const ecs::SkeletalPoseOutputState<ecs::Dim3>*>(nullptr),
                                                     0U,
                                                     static_cast<const ecs::VertexDeformOutputState*>(nullptr),
                                                     0U,
                                                     static_cast<const ecs::MorphWeightOutputState*>(nullptr),
                                                     0U,
                                                     static_cast<const ecs::FrameSequenceOutputState*>(nullptr),
                                                     0U);
                  }) {
        renderer_ref.SetAnimationOutputs(nullptr, 0U, nullptr, 0U, nullptr, 0U, nullptr, 0U);
    }
}

template<typename RendererT>
void SceneRecorder3D::ConfigurePreSceneAnimationBinding(
    void* renderer_,
    render::AnimationFrameCoordinator<ecs::Dim3>* animation_frame_coordinator_) {
    RendererT& renderer_ref = *static_cast<RendererT*>(renderer_);
    if (animation_frame_coordinator_ != nullptr) {
        animation_frame_coordinator_->ApplyToShadowRenderer(renderer_ref);
        return;
    }
    if constexpr (requires(RendererT& candidate_) {
                      candidate_.SetAnimationOutputs(static_cast<const ecs::SkeletalPoseOutputState<ecs::Dim3>*>(nullptr),
                                                     0U,
                                                     static_cast<const ecs::VertexDeformOutputState*>(nullptr),
                                                     0U,
                                                     static_cast<const ecs::MorphWeightOutputState*>(nullptr),
                                                     0U,
                                                     static_cast<const ecs::FrameSequenceOutputState*>(nullptr),
                                                     0U);
                  }) {
        renderer_ref.SetAnimationOutputs(nullptr, 0U, nullptr, 0U, nullptr, 0U, nullptr, 0U);
    } else if constexpr (requires(RendererT& candidate_) {
                             candidate_.SetAnimationOutputs(static_cast<const ecs::SkeletalPoseOutputState<ecs::Dim3>*>(nullptr),
                                                            0U,
                                                            static_cast<const ecs::MorphWeightOutputState*>(nullptr),
                                                            0U,
                                                            static_cast<const ecs::FrameSequenceOutputState*>(nullptr),
                                                            0U);
                         }) {
        renderer_ref.SetAnimationOutputs(nullptr, 0U, nullptr, 0U, nullptr, 0U);
    }
}

template<typename Fn>
void SceneRecorder3D::ForEachSceneRendererInStageOrder(Fn& fn_) const {
    for (const SceneRecorder3DSceneStage stage : scene_stage_record_order) {
        for (const SceneRendererEntry& entry : scene_renderer_entries) {
            if (entry.stage == stage) {
                fn_(entry);
            }
        }
    }
}

} // namespace vr::render
