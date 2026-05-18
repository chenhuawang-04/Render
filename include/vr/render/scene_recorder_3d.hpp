#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/animation_frame_coordinator.hpp"
#include "vr/render/environment/sky_environment_pass.hpp"
#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/light_shadow_link_coordinator.hpp"
#include "vr/render/scene_bloom_post_stack.hpp"
#include "vr/render/scene_render_stage.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render/shadow_atlas_binding_coordinator.hpp"
#include "vr/render/shadow_frame_coordinator.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/shadow/shadow_atlas_host.hpp"
#include "vr/shadow/shadow_renderer_3d.hpp"

#include <cstdint>
#include <stdexcept>

namespace vr::render {

} // namespace vr::render

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

namespace vr::text {
class TextRenderer2D;
class TextRenderer3D;
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
                              RenderTargetHost& render_target_host_,
                              RenderTargetPool* render_target_pool_) noexcept;

    template<typename RuntimeT>
    void BindRuntime(RuntimeT& runtime_) noexcept {
        BindRuntimeResources(runtime_.Context(),
                             runtime_.RenderTarget(),
                             runtime_.HasRenderTargetPool() ? &runtime_.TargetPool() : nullptr);
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
                                 std::uint32_t submission_layer_mask_ = all_submission_layers) {
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

    void RegisterShadowRenderer(shadow::ShadowRenderer3D& shadow_renderer_,
                               std::uint32_t submission_layer_mask_ = all_submission_layers) {
        EnsureInitialized("RegisterShadowRenderer");
        const PreSceneRendererEntry entry{
            .renderer = &shadow_renderer_,
            .kind = PreSceneRendererKind::shadow,
            .reserved0 = 0U,
            .reserved1 = 0U,
            .submission_layer_mask = submission_layer_mask_,
            .prepare_fn = &PrepareRenderer<shadow::ShadowRenderer3D>,
            .record_fn = &RecordRenderer<shadow::ShadowRenderer3D>,
            .swapchain_recreated_fn = &OptionalOnSwapchainRecreatedRenderer<shadow::ShadowRenderer3D>,
            .configure_animation_fn = &ConfigurePreSceneAnimationBinding<shadow::ShadowRenderer3D>,
        };
        UpsertPreSceneRendererEntry(entry);
        BindShadowRuntime(&shadow_renderer_.FrameCoordinatorMutable(),
                          &shadow_renderer_.AtlasHostMutable());
    }

    template<typename RendererT>
    void RegisterSceneRenderer(RendererT& renderer_,
                               SceneRenderPassRole pass_role_,
                               SceneRecorder3DSceneStage stage_ =
                                   SceneRecorder3DSceneStage::opaque,
                               std::uint32_t submission_layer_mask_ = all_submission_layers) {
        EnsureInitialized("RegisterSceneRenderer");
        const SceneRendererEntry entry{
            .renderer = &renderer_,
            .pass_role = pass_role_,
            .stage = stage_,
            .reserved0 = 0U,
            .submission_layer_mask = submission_layer_mask_,
            .prepare_fn = &PrepareRenderer<RendererT>,
            .record_fn = &RecordSceneRenderer<RendererT>,
            .graph_record_fn = ResolveGraphSceneRecordFn<RendererT>(),
            .describe_graph_bindings_fn = ResolveGraphDescriptorBindingsFn<RendererT>(),
            .swapchain_recreated_fn = &OnSwapchainRecreatedRenderer<RendererT>,
            .configure_scene_fn = &ConfigureSceneRendererBinding<RendererT>,
            .configure_direct_scene_fn = &ConfigureDirectSceneRendererBinding<RendererT>,
            .configure_lighting_fn = &ConfigureSceneLightingBinding<RendererT>,
            .configure_animation_fn = &ConfigureSceneAnimationBinding<RendererT>,
        };
        UpsertSceneRendererEntry(entry);
    }

    template<typename RendererT>
    void RegisterOpaqueSceneRenderer(RendererT& renderer_,
                                     SceneRenderPassRole pass_role_,
                                     std::uint32_t submission_layer_mask_ = all_submission_layers) {
        RegisterSceneRenderer(renderer_, pass_role_, SceneRecorder3DSceneStage::opaque, submission_layer_mask_);
    }

    template<typename RendererT>
    void RegisterTransparentSceneRenderer(RendererT& renderer_,
                                          SceneRenderPassRole pass_role_,
                                          std::uint32_t submission_layer_mask_ = all_submission_layers) {
        RegisterSceneRenderer(renderer_, pass_role_, SceneRecorder3DSceneStage::transparent, submission_layer_mask_);
    }

    template<typename RendererT>
    void RegisterOverlayRenderer(RendererT& renderer_,
                                 const RenderTargetColorOutputConfig& output_target_config_ =
                                     MakePresentOverlayOutputConfig(),
                                 std::uint32_t submission_layer_mask_ = all_submission_layers) {
        EnsureInitialized("RegisterOverlayRenderer");
        const OverlayRendererEntry entry{
            .renderer = &renderer_,
            .output_target_config = output_target_config_,
            .submission_layer_mask = submission_layer_mask_,
            .prepare_fn = &PrepareRenderer<RendererT>,
            .record_fn = &RecordRenderer<RendererT>,
            .graph_record_fn = ResolveGraphOverlayRecordFn<RendererT>(),
            .describe_graph_bindings_fn = ResolveGraphDescriptorBindingsFn<RendererT>(),
            .swapchain_recreated_fn = &OptionalOnSwapchainRecreatedRenderer<RendererT>,
            .set_output_target_fn = &SetOverlayOutputTarget<RendererT>,
        };
        UpsertOverlayRendererEntry(entry);
    }

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
    void Record(const FrameRecordContext& record_context_);
    void Record(const FrameRecordContext& record_context_,
                const RenderScenePacket3D& frame_packet_);
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
    [[nodiscard]] SceneBloomPostStack& PostStack() noexcept;
    [[nodiscard]] const SceneBloomPostStack& PostStack() const noexcept;

    [[nodiscard]] static RenderTargetColorOutputConfig MakePresentOverlayOutputConfig() noexcept;

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
    using SwapchainRecreatedFn = void (*)(void*,
                                          std::uint32_t,
                                          VkExtent2D,
                                          VkFormat,
                                          std::uint64_t,
                                          std::uint64_t);
    using ConfigureSceneFn = bool (*)(void*, const SceneRenderTargetSet&, SceneRenderPassRole);
    using ConfigureDirectSceneFn = void (*)(void*,
                                            SceneRenderPassRole,
                                            const RenderTargetColorOutputConfig&,
                                            const RenderTargetDepthOutputConfig*,
                                            bool);
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
    using SetOverlayOutputFn = void (*)(void*, const RenderTargetColorOutputConfig&);
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
        SceneRecordFn record_fn = nullptr;
        GraphSceneRecordFn graph_record_fn = nullptr;
        DescribeGraphBindingsFn describe_graph_bindings_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        ConfigureSceneFn configure_scene_fn = nullptr;
        ConfigureDirectSceneFn configure_direct_scene_fn = nullptr;
        ConfigureLightingFn configure_lighting_fn = nullptr;
        ConfigureSceneAnimationFn configure_animation_fn = nullptr;
    };

    struct OverlayRendererEntry final {
        void* renderer = nullptr;
        RenderTargetColorOutputConfig output_target_config{};
        std::uint32_t submission_layer_mask = all_submission_layers;
        PrepareFn prepare_fn = nullptr;
        RecordFn record_fn = nullptr;
        GraphOverlayRecordFn graph_record_fn = nullptr;
        DescribeGraphBindingsFn describe_graph_bindings_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        SetOverlayOutputFn set_output_target_fn = nullptr;
    };

    template<typename RendererT>
    static void PrepareRenderer(void* renderer_,
                                const SceneRecorder3DPrepareView& prepare_view_) {
        RendererT& renderer_ref = *static_cast<RendererT*>(renderer_);
        if constexpr (requires(RendererT& candidate_,
                               const RenderTargetCompositeRendererPrepareView& renderer_prepare_view_) {
                          candidate_.PrepareFrame(renderer_prepare_view_);
                      }) {
            renderer_ref.PrepareFrame(MakeRenderTargetCompositeRendererPrepareView(prepare_view_));
        } else if constexpr (requires(RendererT& candidate_,
                                      const RenderTargetBloomRendererPrepareView& renderer_prepare_view_) {
                                 candidate_.PrepareFrame(renderer_prepare_view_);
                             }) {
            renderer_ref.PrepareFrame(MakeRenderTargetBloomRendererPrepareView(prepare_view_));
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
    static void RecordRenderer(void* renderer_,
                               const FrameRecordContext& record_context_) {
        static_cast<RendererT*>(renderer_)->Record(record_context_);
    }

    template<typename RendererT>
    static void RecordSceneRenderer(void* renderer_,
                                    const FrameRecordContext& record_context_,
                                    SceneRecorder3DSceneStage stage_) {
        if constexpr (requires(RendererT& renderer_ref_) {
                          renderer_ref_.RecordSceneStage(record_context_, stage_);
                      }) {
            static_cast<RendererT*>(renderer_)->RecordSceneStage(record_context_, stage_);
        } else {
            static_cast<RendererT*>(renderer_)->Record(record_context_);
        }
    }

    template<typename RendererT>
    static void RecordGraphSceneRenderer(void* renderer_,
                                         render_graph::GraphCommandContext& context_,
                                         SceneRenderStage stage_,
                                         render_graph::ResourceHandle color_target_,
                                         render_graph::ResourceHandle depth_target_) {
        static_cast<RendererT*>(renderer_)->RecordGraphSceneStage(context_, stage_, color_target_, depth_target_);
    }

    template<typename RendererT>
    static constexpr GraphSceneRecordFn ResolveGraphSceneRecordFn() noexcept {
        if constexpr (SceneRecorder3DGraphSceneSupport<RendererT>::value) {
            return &RecordGraphSceneRenderer<RendererT>;
        } else {
            return nullptr;
        }
    }

    template<typename RendererT>
    static void RecordGraphOverlayRenderer(void* renderer_,
                                           render_graph::GraphCommandContext& context_,
                                           render_graph::ResourceHandle color_target_) {
        static_cast<RendererT*>(renderer_)->RecordGraphOverlay(context_, color_target_);
    }

    template<typename RendererT>
    static constexpr GraphOverlayRecordFn ResolveGraphOverlayRecordFn() noexcept {
        if constexpr (SceneRecorder3DGraphOverlaySupport<RendererT>::value) {
            return &RecordGraphOverlayRenderer<RendererT>;
        } else {
            return nullptr;
        }
    }

    template<typename RendererT>
    static void DescribeGraphBindings(void* renderer_,
                                      render_graph::RenderGraphBuilder& builder_,
                                      render_graph::PassHandle pass_) {
        static_cast<RendererT*>(renderer_)->DescribeGraphDescriptorBindings(builder_, pass_);
    }

    template<typename RendererT>
    static constexpr DescribeGraphBindingsFn ResolveGraphDescriptorBindingsFn() noexcept {
        if constexpr (SceneRecorder3DGraphDescriptorBindable<RendererT>) {
            return &DescribeGraphBindings<RendererT>;
        } else {
            return nullptr;
        }
    }

    template<typename RendererT>
    static void OnSwapchainRecreatedRenderer(void* renderer_,
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
    static void OptionalOnSwapchainRecreatedRenderer(void* renderer_,
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
    static bool ConfigureSceneRendererBinding(void* renderer_,
                                              const SceneRenderTargetSet& target_set_,
                                              SceneRenderPassRole pass_role_) {
        return target_set_.ConfigureSceneRenderer(*static_cast<RendererT*>(renderer_), pass_role_);
    }

    template<typename RendererT>
    static void ConfigureDirectSceneRendererBinding(
        void* renderer_,
        SceneRenderPassRole,
        const RenderTargetColorOutputConfig& color_output_target_config_,
        const RenderTargetDepthOutputConfig* depth_output_target_config_,
        bool enable_depth_) {
        RendererT& renderer_ref = *static_cast<RendererT*>(renderer_);
        renderer_ref.SetOutputTargetConfig(color_output_target_config_);
        if (!enable_depth_) {
            return;
        }
        if constexpr (requires(RendererT& candidate_,
                               const RenderTargetDepthOutputConfig& depth_output_target_config_ref_) {
                          candidate_.SetDepthTargetConfig(depth_output_target_config_ref_);
                      }) {
            if (depth_output_target_config_ != nullptr) {
                renderer_ref.SetDepthTargetConfig(*depth_output_target_config_);
            }
        } else {
            throw std::runtime_error(
                "SceneRecorder3D direct scene output requires depth target support");
        }
    }

    template<typename RendererT>
    static void ConfigureSceneLightingBinding(void* renderer_,
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
    static void ConfigureSceneAnimationBinding(
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
    static void ConfigurePreSceneAnimationBinding(
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

    template<typename RendererT>
    static void SetOverlayOutputTarget(void* renderer_,
                                       const RenderTargetColorOutputConfig& output_target_config_) {
        static_cast<RendererT*>(renderer_)->SetOutputTargetConfig(output_target_config_);
    }

    template<typename Fn>
    void ForEachSceneRendererInStageOrder(Fn& fn_) const {
        for (const SceneRecorder3DSceneStage stage : scene_stage_record_order) {
            for (const SceneRendererEntry& entry : scene_renderer_entries) {
                if (entry.stage == stage) {
                    fn_(entry);
                }
            }
        }
    }

    void UpsertPreSceneRendererEntry(const PreSceneRendererEntry& entry_);
    void UpsertSceneRendererEntry(const SceneRendererEntry& entry_);
    void UpsertOverlayRendererEntry(const OverlayRendererEntry& entry_);
    void RefreshFramePacketBinding() noexcept;
    void RefreshSceneLightingBindings() noexcept;
    void RefreshAnimationBindings() noexcept;
    void RefreshRendererCounts() noexcept;
    [[nodiscard]] bool HasSceneViewForSubmission() const noexcept;
    [[nodiscard]] bool HasExplicitSceneTargetForSubmission() const noexcept;
    [[nodiscard]] bool HasExplicitOverlayTargetForSubmission() const noexcept;
    [[nodiscard]] RenderTargetColorOutputConfig BuildDirectSceneOutputConfig(
        SceneRenderPassRole pass_role_) const noexcept;
    [[nodiscard]] RenderTargetDepthOutputConfig BuildDirectDepthOutputConfig(
        SceneRenderPassRole pass_role_) const noexcept;
    [[nodiscard]] RenderTargetColorOutputConfig BuildExplicitSceneOutputConfig(
        SceneRenderPassRole pass_role_) const noexcept;
    [[nodiscard]] RenderTargetDepthOutputConfig BuildExplicitDepthOutputConfig(
        SceneRenderPassRole pass_role_) const noexcept;
    [[nodiscard]] RenderTargetColorOutputConfig BuildOverlayOutputConfig(
        const RenderTargetColorOutputConfig& fallback_output_target_config_) const noexcept;
    [[nodiscard]] bool ShouldUsePostStackForSubmission() const noexcept;
    [[nodiscard]] std::uint32_t EffectiveLayerMask() const noexcept;
    [[nodiscard]] std::uint32_t OverlayLayerMask() const noexcept;
    [[nodiscard]] bool IsShadowEnabledForSubmission() const noexcept;
    [[nodiscard]] bool IsOverlayEnabledForSubmission() const noexcept;
    [[nodiscard]] bool IsPostProcessEnabledForSubmission() const noexcept;
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
    void ConfigureSkyEnvironmentPassForTargets();
    [[nodiscard]] RenderTargetColorOutputConfig BuildSkyEnvironmentOutputConfig() const noexcept;
    [[nodiscard]] RenderTargetDepthOutputConfig BuildSkyEnvironmentDepthOutputConfig() const noexcept;
    void EnsureInitialized(const char* operation_) const;
    void EnsureRuntimeBinding(const char* operation_) const;

private:
    SceneRecorder3DCreateInfo create_info_cache{};
    SceneRecorder3DStats stats{};
    SkyEnvironmentPass sky_environment_pass{};
    SceneBloomPostStack post_stack{};
    SceneRecorder3DMcVector<PreSceneRendererEntry> pre_scene_renderer_entries{};
    SceneRecorder3DMcVector<SceneRendererEntry> scene_renderer_entries{};
    SceneRecorder3DMcVector<OverlayRendererEntry> overlay_renderer_entries{};
    VulkanContext* context = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    RenderTargetPool* render_target_pool = nullptr;
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

