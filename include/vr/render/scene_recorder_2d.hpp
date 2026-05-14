#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/environment/background_pass_2d.hpp"
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/light_shadow_link_coordinator.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render/scene_render_target_set.hpp"
#include "vr/render/shadow_atlas_binding_coordinator.hpp"
#include "vr/render/shadow_frame_coordinator.hpp"
#include "vr/shadow/shadow_atlas_host.hpp"
#include "vr/shadow/shadow_renderer_2d.hpp"

#include <cstdint>
#include <stdexcept>

namespace vr::render {

template<typename T>
using SceneRecorder2DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SceneRecorder2DCreateInfo final {
    SceneRenderTargetSetCreateInfo scene_target{};
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
                              RenderTargetHost& render_target_host_,
                              RenderTargetPool* render_target_pool_) noexcept;

    template<typename RuntimeT>
    void BindRuntime(RuntimeT& runtime_) noexcept {
        BindRuntimeResources(runtime_.Context(),
                             runtime_.RenderTarget(),
                             runtime_.HasRenderTargetPool() ? &runtime_.TargetPool() : nullptr);
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
        };
        UpsertPreSceneRendererEntry(entry);
    }

    void RegisterShadowRenderer(shadow::ShadowRenderer2D& shadow_renderer_,
                               std::uint32_t submission_layer_mask_ = all_submission_layers) {
        EnsureInitialized("RegisterShadowRenderer");
        const PreSceneRendererEntry entry{
            .renderer = &shadow_renderer_,
            .kind = PreSceneRendererKind::shadow,
            .reserved0 = 0U,
            .reserved1 = 0U,
            .submission_layer_mask = submission_layer_mask_,
            .prepare_fn = &PrepareRenderer<shadow::ShadowRenderer2D>,
            .record_fn = &RecordRenderer<shadow::ShadowRenderer2D>,
            .swapchain_recreated_fn = &OptionalOnSwapchainRecreatedRenderer<shadow::ShadowRenderer2D>,
        };
        UpsertPreSceneRendererEntry(entry);
        BindShadowRuntime(&shadow_renderer_.FrameCoordinatorMutable(),
                          &shadow_renderer_.AtlasHostMutable());
    }

    template<typename RendererT>
    void RegisterSceneRenderer(RendererT& renderer_,
                               SceneRenderPassRole pass_role_,
                               std::uint32_t submission_layer_mask_ = all_submission_layers) {
        EnsureInitialized("RegisterSceneRenderer");
        const SceneRendererEntry entry{
            .renderer = &renderer_,
            .pass_role = pass_role_,
            .submission_layer_mask = submission_layer_mask_,
            .prepare_fn = &PrepareRenderer<RendererT>,
            .record_fn = &RecordRenderer<RendererT>,
            .swapchain_recreated_fn = &OnSwapchainRecreatedRenderer<RendererT>,
            .configure_scene_fn = &ConfigureSceneRendererBinding<RendererT>,
            .configure_direct_scene_fn = &ConfigureDirectSceneRendererBinding<RendererT>,
            .configure_lighting_fn = &ConfigureSceneLightingBinding<RendererT>,
            .set_output_target_fn = &SetOverlayOutputTarget<RendererT>,
        };
        UpsertSceneRendererEntry(entry);
    }

    template<typename RendererT>
    void RegisterSceneConsumer(RendererT& renderer_,
                               const RenderTargetColorOutputConfig& output_target_config_ =
                                   MakePresentOverlayOutputConfig(),
                               std::uint32_t submission_layer_mask_ = all_submission_layers) {
        EnsureInitialized("RegisterSceneConsumer");
        const SceneConsumerEntry entry{
            .renderer = &renderer_,
            .output_target_config = output_target_config_,
            .submission_layer_mask = submission_layer_mask_,
            .prepare_fn = &PrepareRenderer<RendererT>,
            .record_fn = &RecordRenderer<RendererT>,
            .swapchain_recreated_fn = &OnSwapchainRecreatedRenderer<RendererT>,
            .configure_scene_consumer_fn = &ConfigureSceneConsumerBinding<RendererT>,
            .set_output_target_fn = &SetOverlayOutputTarget<RendererT>,
        };
        SetSceneConsumerEntry(entry);
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
            .swapchain_recreated_fn = &OnSwapchainRecreatedRenderer<RendererT>,
            .set_output_target_fn = &SetOverlayOutputTarget<RendererT>,
        };
        UpsertOverlayRendererEntry(entry);
    }

    void ClearPreSceneRenderers() noexcept;
    void ClearSceneRenderers() noexcept;
    void ClearSceneConsumer() noexcept;
    void ClearOverlayRenderers() noexcept;
    void ClearRendererRegistrations() noexcept;

    void PrepareFrame(const SceneRecorder2DPrepareView& prepare_view_);
    void PrepareFrame(const SceneRecorder2DPrepareView& prepare_view_,
                      const RenderScenePacket2D& frame_packet_);
    void Record(const FrameRecordContext& record_context_);
    void Record(const FrameRecordContext& record_context_,
                const RenderScenePacket2D& frame_packet_);
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
    [[nodiscard]] SceneRenderTargetSet& SceneTargets() noexcept;
    [[nodiscard]] const SceneRenderTargetSet& SceneTargets() const noexcept;

    [[nodiscard]] static RenderTargetColorOutputConfig MakePresentOverlayOutputConfig() noexcept;

private:
    static constexpr std::uint32_t all_submission_layers = 0xFFFF'FFFFU;

    using PrepareFn = void (*)(void*, const SceneRecorder2DPrepareView&);
    using RecordFn = void (*)(void*, const FrameRecordContext&);
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
    using ConfigureSceneConsumerFn = bool (*)(void*, const SceneRenderTargetSet&);
    using ConfigureLightingFn = void (*)(void*,
                                         render::LightFrameCoordinator<ecs::Dim2>*,
                                         render::LightShadowLinkCoordinator2D*,
                                         render::ShadowAtlasBindingCoordinator*,
                                         render::ShadowFrameCoordinator<ecs::Dim2>*,
                                         shadow::ShadowAtlasHost*);
    using SetOverlayOutputFn = void (*)(void*, const RenderTargetColorOutputConfig&);

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
        RecordFn record_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        ConfigureSceneFn configure_scene_fn = nullptr;
        ConfigureDirectSceneFn configure_direct_scene_fn = nullptr;
        ConfigureLightingFn configure_lighting_fn = nullptr;
        SetOverlayOutputFn set_output_target_fn = nullptr;
    };

    struct SceneConsumerEntry final {
        void* renderer = nullptr;
        RenderTargetColorOutputConfig output_target_config{};
        std::uint32_t submission_layer_mask = all_submission_layers;
        PrepareFn prepare_fn = nullptr;
        RecordFn record_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        ConfigureSceneConsumerFn configure_scene_consumer_fn = nullptr;
        SetOverlayOutputFn set_output_target_fn = nullptr;
    };

    struct OverlayRendererEntry final {
        void* renderer = nullptr;
        RenderTargetColorOutputConfig output_target_config{};
        std::uint32_t submission_layer_mask = all_submission_layers;
        PrepareFn prepare_fn = nullptr;
        RecordFn record_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        SetOverlayOutputFn set_output_target_fn = nullptr;
    };

    template<typename RendererT>
    static void PrepareRenderer(void* renderer_,
                                const SceneRecorder2DPrepareView& prepare_view_) {
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
    static void RecordRenderer(void* renderer_,
                               const FrameRecordContext& record_context_) {
        static_cast<RendererT*>(renderer_)->Record(record_context_);
    }

    template<typename RendererT>
    static void OnSwapchainRecreatedRenderer(void* renderer_,
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
        bool require_depth_support_) {
        RendererT& renderer_ref = *static_cast<RendererT*>(renderer_);
        renderer_ref.SetOutputTargetConfig(color_output_target_config_);
        if (depth_output_target_config_ == nullptr) {
            return;
        }
        if constexpr (requires(RendererT& candidate_,
                               const RenderTargetDepthOutputConfig& depth_output_target_config_ref_) {
                          candidate_.SetDepthTargetConfig(depth_output_target_config_ref_);
                      }) {
            renderer_ref.SetDepthTargetConfig(*depth_output_target_config_);
        } else if (require_depth_support_) {
            throw std::runtime_error(
                "SceneRecorder2D direct scene output requires depth target support");
        }
    }

    template<typename ConsumerT>
    static bool ConfigureSceneConsumerBinding(void* renderer_,
                                              const SceneRenderTargetSet& target_set_) {
        return target_set_.ConfigureSceneConsumer(*static_cast<ConsumerT*>(renderer_));
    }

    template<typename RendererT>
    static void ConfigureSceneLightingBinding(void* renderer_,
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

    template<typename RendererT>
    static void SetOverlayOutputTarget(void* renderer_,
                                       const RenderTargetColorOutputConfig& output_target_config_) {
        static_cast<RendererT*>(renderer_)->SetOutputTargetConfig(output_target_config_);
    }

    void UpsertPreSceneRendererEntry(const PreSceneRendererEntry& entry_);
    void UpsertSceneRendererEntry(const SceneRendererEntry& entry_);
    void SetSceneConsumerEntry(const SceneConsumerEntry& entry_) noexcept;
    void UpsertOverlayRendererEntry(const OverlayRendererEntry& entry_);
    void RefreshFramePacketBinding() noexcept;
    void RefreshSceneLightingBindings() noexcept;
    void RefreshRendererCounts() noexcept;
    [[nodiscard]] bool HasSceneViewForSubmission() const noexcept;
    [[nodiscard]] bool HasExplicitSceneTargetForSubmission() const noexcept;
    [[nodiscard]] bool HasExplicitOverlayTargetForSubmission() const noexcept;
    [[nodiscard]] std::uint32_t EffectiveLayerMask() const noexcept;
    [[nodiscard]] std::uint32_t OverlayLayerMask() const noexcept;
    [[nodiscard]] bool IsShadowEnabledForSubmission() const noexcept;
    [[nodiscard]] bool IsOverlayEnabledForSubmission() const noexcept;
    [[nodiscard]] bool IsPostProcessEnabledForSubmission() const noexcept;
    [[nodiscard]] bool HasBackgroundPassForSubmission() const noexcept;
    [[nodiscard]] bool IsLayerVisibleForSubmission(std::uint32_t submission_layer_mask_) const noexcept;
    [[nodiscard]] bool IsOverlayLayerVisibleForSubmission(std::uint32_t submission_layer_mask_) const noexcept;
    void ConfigureBackgroundPassForTargets();
    void ConfigureSceneRenderersForTargets();
    void ConfigureSceneConsumerForTargets();
    [[nodiscard]] RenderTargetColorOutputConfig BuildBackgroundOutputConfig() const noexcept;
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
    void EnsureInitialized(const char* operation_) const;
    void EnsureRuntimeBinding(const char* operation_) const;

private:
    SceneRecorder2DCreateInfo create_info_cache{};
    SceneRecorder2DStats stats{};
    BackgroundPass2D background_pass{};
    SceneRenderTargetSet scene_targets{};
    SceneRecorder2DMcVector<PreSceneRendererEntry> pre_scene_renderer_entries{};
    SceneRecorder2DMcVector<SceneRendererEntry> scene_renderer_entries{};
    SceneConsumerEntry scene_consumer_entry{};
    SceneRecorder2DMcVector<OverlayRendererEntry> overlay_renderer_entries{};
    VulkanContext* context = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    RenderTargetPool* render_target_pool = nullptr;
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

