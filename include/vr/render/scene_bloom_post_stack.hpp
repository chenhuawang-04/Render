#pragma once

#include "vr/render/render_target_bloom_renderer.hpp"
#include "vr/render/scene_render_target_set.hpp"

#include <stdexcept>

namespace vr {
class VulkanContext;
}

namespace vr::render {

class SceneBloomPostStack final {
public:
    SceneBloomPostStack() = default;
    ~SceneBloomPostStack() = default;

    SceneBloomPostStack(const SceneBloomPostStack&) = delete;
    SceneBloomPostStack& operator=(const SceneBloomPostStack&) = delete;

    SceneBloomPostStack(SceneBloomPostStack&&) = delete;
    SceneBloomPostStack& operator=(SceneBloomPostStack&&) = delete;

    void Initialize(const SceneRenderTargetSetCreateInfo& scene_create_info_ = {},
                    const RenderTargetBloomRendererCreateInfo& bloom_create_info_ = {}) noexcept;
    void Shutdown(VulkanContext& context_) noexcept;

    template<typename... BindingTs>
    [[nodiscard]] bool PrepareFrame(const SceneBloomPostStackPrepareView& prepare_view_,
                                    const BindingTs&... bindings_) {
        EnsureInitialized("PrepareFrame");
        const bool ready = scene_targets.PrepareFrameAndConfigure(
                                                                  SceneRenderTargetSetPrepareView{
                                                                      .device = prepare_view_.device,
                                                                      .render_target = prepare_view_.render_target,
                                                                      .render_target_pool = prepare_view_.render_target_pool,
                                                                      .frame = prepare_view_.frame,
                                                                      .progress = prepare_view_.progress,
                                                                  },
                                                                  &bloom_renderer,
                                                                  bindings_...);
        bloom_renderer.PrepareFrame(MakeRenderTargetBloomRendererPrepareView(prepare_view_));
        return ready;
    }

    void Record(const FrameRecordContext& record_context_);

    template<typename... BindingTs>
    [[nodiscard]] bool OnSwapchainRecreated(VulkanContext& context_,
                                            std::uint32_t image_count_,
                                            VkExtent2D extent_,
                                            VkFormat format_,
                                            std::uint64_t last_submitted_value_,
                                            std::uint64_t completed_submit_value_,
                                            RenderTargetHost& render_target_host_,
                                            RenderTargetPool* render_target_pool_,
                                            const BindingTs&... bindings_) {
        EnsureInitialized("OnSwapchainRecreated");
        bloom_renderer.OnSwapchainRecreated(image_count_, extent_, format_);
        return scene_targets.OnSwapchainRecreatedAndConfigure(context_,
                                                              render_target_host_,
                                                              render_target_pool_,
                                                              extent_,
                                                              last_submitted_value_,
                                                              completed_submit_value_,
                                                              &bloom_renderer,
                                                              bindings_...);
    }

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] SceneRenderTargetSet& Targets() noexcept;
    [[nodiscard]] const SceneRenderTargetSet& Targets() const noexcept;
    [[nodiscard]] RenderTargetBloomRenderer& Bloom() noexcept;
    [[nodiscard]] const RenderTargetBloomRenderer& Bloom() const noexcept;
    [[nodiscard]] const RenderTargetBloomRendererStats& Stats() const noexcept;

private:
    void EnsureInitialized(const char* operation_) const;

private:
    SceneRenderTargetSet scene_targets{};
    RenderTargetBloomRenderer bloom_renderer{};
    bool initialized = false;
};

} // namespace vr::render
