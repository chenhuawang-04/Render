#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
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
    void RegisterPreSceneRenderer(RendererT& renderer_) {
        EnsureInitialized("RegisterPreSceneRenderer");
        const PreSceneRendererEntry entry{
            .renderer = &renderer_,
            .prepare_fn = &PrepareRenderer<RendererT>,
            .record_fn = &RecordRenderer<RendererT>,
            .swapchain_recreated_fn = &OptionalOnSwapchainRecreatedRenderer<RendererT>,
        };
        UpsertPreSceneRendererEntry(entry);
    }

    void RegisterShadowRenderer(shadow::ShadowRenderer2D& shadow_renderer_) {
        RegisterPreSceneRenderer(shadow_renderer_);
        BindShadowRuntime(&shadow_renderer_.FrameCoordinatorMutable(),
                          &shadow_renderer_.AtlasHostMutable());
    }

    template<typename RendererT>
    void RegisterSceneRenderer(RendererT& renderer_,
                               SceneRenderPassRole pass_role_) {
        EnsureInitialized("RegisterSceneRenderer");
        const SceneRendererEntry entry{
            .renderer = &renderer_,
            .pass_role = pass_role_,
            .prepare_fn = &PrepareRenderer<RendererT>,
            .record_fn = &RecordRenderer<RendererT>,
            .swapchain_recreated_fn = &OnSwapchainRecreatedRenderer<RendererT>,
            .configure_scene_fn = &ConfigureSceneRendererBinding<RendererT>,
            .configure_lighting_fn = &ConfigureSceneLightingBinding<RendererT>,
        };
        UpsertSceneRendererEntry(entry);
    }

    template<typename RendererT>
    void RegisterSceneConsumer(RendererT& renderer_,
                               const RenderTargetColorOutputConfig& output_target_config_ =
                                   MakePresentOverlayOutputConfig()) {
        EnsureInitialized("RegisterSceneConsumer");
        const SceneConsumerEntry entry{
            .renderer = &renderer_,
            .output_target_config = output_target_config_,
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
                                     MakePresentOverlayOutputConfig()) {
        EnsureInitialized("RegisterOverlayRenderer");
        const OverlayRendererEntry entry{
            .renderer = &renderer_,
            .output_target_config = output_target_config_,
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

    void PrepareFrame(const RuntimePrepareContext& prepare_context_);
    void PrepareFrame(const RuntimePrepareContext& prepare_context_,
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
    using PrepareFn = void (*)(void*, const RuntimePrepareContext&);
    using RecordFn = void (*)(void*, const FrameRecordContext&);
    using SwapchainRecreatedFn = void (*)(void*,
                                          std::uint32_t,
                                          VkExtent2D,
                                          VkFormat,
                                          std::uint64_t,
                                          std::uint64_t);
    using ConfigureSceneFn = bool (*)(void*, const SceneRenderTargetSet&, SceneRenderPassRole);
    using ConfigureSceneConsumerFn = bool (*)(void*, const SceneRenderTargetSet&);
    using ConfigureLightingFn = void (*)(void*,
                                         render::LightFrameCoordinator<ecs::Dim2>*,
                                         render::LightShadowLinkCoordinator2D*,
                                         render::ShadowAtlasBindingCoordinator*,
                                         render::ShadowFrameCoordinator<ecs::Dim2>*,
                                         shadow::ShadowAtlasHost*);
    using SetOverlayOutputFn = void (*)(void*, const RenderTargetColorOutputConfig&);

    struct PreSceneRendererEntry final {
        void* renderer = nullptr;
        PrepareFn prepare_fn = nullptr;
        RecordFn record_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
    };

    struct SceneRendererEntry final {
        void* renderer = nullptr;
        SceneRenderPassRole pass_role = SceneRenderPassRole::single;
        PrepareFn prepare_fn = nullptr;
        RecordFn record_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        ConfigureSceneFn configure_scene_fn = nullptr;
        ConfigureLightingFn configure_lighting_fn = nullptr;
    };

    struct SceneConsumerEntry final {
        void* renderer = nullptr;
        RenderTargetColorOutputConfig output_target_config{};
        PrepareFn prepare_fn = nullptr;
        RecordFn record_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        ConfigureSceneConsumerFn configure_scene_consumer_fn = nullptr;
        SetOverlayOutputFn set_output_target_fn = nullptr;
    };

    struct OverlayRendererEntry final {
        void* renderer = nullptr;
        RenderTargetColorOutputConfig output_target_config{};
        PrepareFn prepare_fn = nullptr;
        RecordFn record_fn = nullptr;
        SwapchainRecreatedFn swapchain_recreated_fn = nullptr;
        SetOverlayOutputFn set_output_target_fn = nullptr;
    };

    template<typename RendererT>
    static void PrepareRenderer(void* renderer_,
                                const RuntimePrepareContext& prepare_context_) {
        static_cast<RendererT*>(renderer_)->PrepareFrame(prepare_context_);
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
    void ConfigureSceneRenderersForTargets() noexcept;
    void ConfigureSceneConsumerForTargets() noexcept;
    void EnsureInitialized(const char* operation_) const;
    void EnsureRuntimeBinding(const char* operation_) const;

private:
    SceneRecorder2DCreateInfo create_info_cache{};
    SceneRecorder2DStats stats{};
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
    std::uint64_t active_view_signature = 0U;
    render::LightShadowLinkCoordinator2D light_shadow_link_coordinator{};
    render::ShadowAtlasBindingCoordinator shadow_atlas_binding_coordinator{};
    bool initialized = false;
};

} // namespace vr::render
