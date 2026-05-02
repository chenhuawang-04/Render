#pragma once

#include "vr/render/render_target_composite_renderer.hpp"
#include "vr/render/render_target_format_utils.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/render_target_pool.hpp"
#include "vr/render/runtime_prepare_context.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace vr::render {

struct SceneRenderTargetSetCreateInfo final {
    const char* color_debug_name = "SceneColor";
    const char* depth_debug_name = "SceneDepth";
    RenderTargetScaleMode scale_mode = RenderTargetScaleMode::swapchain_relative;
    std::uint32_t width = 1U;
    std::uint32_t height = 1U;
    std::uint32_t depth = 1U;
    float width_scale = 1.0F;
    float height_scale = 1.0F;
    bool enable_depth = true;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags additional_color_usage = 0U;
    VkImageUsageFlags additional_depth_usage = 0U;
    RenderTargetLifetime color_lifetime = RenderTargetLifetime::persistent;
    RenderTargetLifetime depth_lifetime = RenderTargetLifetime::persistent;
    RenderTargetColorEncoding color_encoding = RenderTargetColorEncoding::linear;
    RenderTargetStateKind color_final_state = RenderTargetStateKind::shader_read;
    RenderTargetStateKind depth_final_state = RenderTargetStateKind::depth_attachment;
    VkClearColorValue clear_color = {{0.02F, 0.02F, 0.03F, 1.0F}};
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
};

enum class SceneRenderPassRole : std::uint8_t {
    single = 0U,
    first = 1U,
    middle = 2U,
    last = 3U
};

template<typename RendererT>
struct SceneRendererBinding final {
    RendererT* renderer = nullptr;
    SceneRenderPassRole pass_role = SceneRenderPassRole::single;
};

template<typename RendererT>
[[nodiscard]] constexpr SceneRendererBinding<RendererT> BindSceneRenderer(
    RendererT& renderer_,
    SceneRenderPassRole pass_role_) noexcept {
    return SceneRendererBinding<RendererT>{.renderer = &renderer_, .pass_role = pass_role_};
}

class SceneRenderTargetSet final {
public:
    SceneRenderTargetSet() = default;
    ~SceneRenderTargetSet() = default;

    SceneRenderTargetSet(const SceneRenderTargetSet&) = delete;
    SceneRenderTargetSet& operator=(const SceneRenderTargetSet&) = delete;

    SceneRenderTargetSet(SceneRenderTargetSet&&) = delete;
    SceneRenderTargetSet& operator=(SceneRenderTargetSet&&) = delete;

    void Initialize(const SceneRenderTargetSetCreateInfo& create_info_ = {}) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool EnsureForSwapchain(VulkanContext& context_,
                                          RenderTargetHost& render_target_host_,
                                          VkExtent2D swapchain_extent_,
                                          std::uint64_t last_submitted_value_,
                                          std::uint64_t completed_submit_value_);
    [[nodiscard]] bool PrepareFrame(const RuntimePrepareContext& prepare_context_);
    [[nodiscard]] bool OnSwapchainRecreated(VulkanContext& context_,
                                            RenderTargetHost& render_target_host_,
                                            RenderTargetPool* render_target_pool_,
                                            VkExtent2D swapchain_extent_,
                                            std::uint64_t last_submitted_value_,
                                            std::uint64_t completed_submit_value_);

    [[nodiscard]] RenderTargetColorOutputConfig BuildColorOutputConfig(bool clear_target_,
                                                                       bool final_pass_) const;
    [[nodiscard]] RenderTargetDepthOutputConfig BuildDepthOutputConfig(bool clear_target_) const;

    template<typename RendererT>
    [[nodiscard]] bool ConfigureSceneRenderer(RendererT& renderer_,
                                              SceneRenderPassRole pass_role_) const {
        if (!IsReady()) {
            ResetSceneRenderer(renderer_);
            return false;
        }
        const std::size_t pass_role_index = PassRoleIndex(pass_role_);
        renderer_.SetOutputTargetConfig(cached_color_outputs[pass_role_index]);
        if (create_info_cache.enable_depth) {
            if constexpr (supports_depth_target_config<RendererT>) {
                renderer_.SetDepthTargetConfig(cached_depth_outputs[pass_role_index]);
            } else {
                throw std::runtime_error(
                    "SceneRenderTargetSet depth target requested for renderer without depth target support");
            }
        }
        return true;
    }

    template<typename RendererT>
    [[nodiscard]] bool ConfigureSceneRenderer(RendererT& renderer_,
                                              bool clear_target_,
                                              bool final_pass_) const {
        if (!IsReady()) {
            ResetSceneRenderer(renderer_);
            return false;
        }
        renderer_.SetOutputTargetConfig(BuildColorOutputConfig(clear_target_, final_pass_));
        if (create_info_cache.enable_depth) {
            if constexpr (supports_depth_target_config<RendererT>) {
                renderer_.SetDepthTargetConfig(BuildDepthOutputConfig(clear_target_));
            } else {
                throw std::runtime_error(
                    "SceneRenderTargetSet depth target requested for renderer without depth target support");
            }
        }
        return true;
    }

    template<typename RendererT>
    void ResetSceneRenderer(RendererT& renderer_) const {
        renderer_.ResetOutputTargetConfig();
        if (create_info_cache.enable_depth) {
            if constexpr (supports_depth_target_config<RendererT>) {
                renderer_.ResetDepthTargetConfig();
            }
        }
    }

    template<typename ConsumerT>
    [[nodiscard]] bool ConfigureSceneConsumer(ConsumerT& consumer_) const noexcept
        requires(
            requires(ConsumerT& candidate_) {
                candidate_.SetSceneSourceTarget(RenderTargetHandle{}, RenderTargetStateKind::shader_read);
                candidate_.ClearSceneSourceTarget();
            } ||
            requires(ConsumerT& candidate_) {
                candidate_.SetSourceTarget(RenderTargetHandle{}, RenderTargetStateKind::shader_read);
                candidate_.ClearSourceTarget();
            }) {
        if (!IsReady()) {
            ResetSceneConsumer(consumer_);
            return false;
        }
        if constexpr (requires(ConsumerT& candidate_) {
                          candidate_.SetSceneSourceTarget(RenderTargetHandle{}, RenderTargetStateKind::shader_read);
                          candidate_.ClearSceneSourceTarget();
                      }) {
            consumer_.SetSceneSourceTarget(color_target, create_info_cache.color_final_state);
        } else {
            consumer_.SetSourceTarget(color_target, create_info_cache.color_final_state);
        }
        if constexpr (requires(ConsumerT& candidate_) {
                          candidate_.ResetOutputTargetConfig();
                      }) {
            consumer_.ResetOutputTargetConfig();
        }
        return true;
    }

    template<typename ConsumerT>
    void ResetSceneConsumer(ConsumerT& consumer_) const noexcept
        requires(
            requires(ConsumerT& candidate_) {
                candidate_.SetSceneSourceTarget(RenderTargetHandle{}, RenderTargetStateKind::shader_read);
                candidate_.ClearSceneSourceTarget();
            } ||
            requires(ConsumerT& candidate_) {
                candidate_.SetSourceTarget(RenderTargetHandle{}, RenderTargetStateKind::shader_read);
                candidate_.ClearSourceTarget();
            }) {
        if constexpr (requires(ConsumerT& candidate_) {
                          candidate_.SetSceneSourceTarget(RenderTargetHandle{}, RenderTargetStateKind::shader_read);
                          candidate_.ClearSceneSourceTarget();
                      }) {
            consumer_.ClearSceneSourceTarget();
        } else {
            consumer_.ClearSourceTarget();
        }
        if constexpr (requires(ConsumerT& candidate_) {
                          candidate_.ResetOutputTargetConfig();
                      }) {
            consumer_.ResetOutputTargetConfig();
        }
    }

    [[nodiscard]] bool ConfigureCompositeRenderer(RenderTargetCompositeRenderer& composite_renderer_) const noexcept;
    void ResetCompositeRenderer(RenderTargetCompositeRenderer& composite_renderer_) const noexcept;

    template<typename... BindingTs>
    [[nodiscard]] bool PrepareFrameAndConfigure(const RuntimePrepareContext& prepare_context_,
                                                RenderTargetCompositeRenderer* composite_renderer_,
                                                const BindingTs&... bindings_) {
        const bool ready = PrepareFrame(prepare_context_);
        ConfigureBindings(bindings_...);
        if (composite_renderer_ != nullptr) {
            (void)ConfigureCompositeRenderer(*composite_renderer_);
        }
        return ready;
    }

    template<typename ConsumerT, typename... BindingTs>
    [[nodiscard]] bool PrepareFrameAndConfigure(const RuntimePrepareContext& prepare_context_,
                                                ConsumerT* scene_consumer_,
                                                const BindingTs&... bindings_)
        requires(requires(SceneRenderTargetSet& target_set_, ConsumerT& consumer_) {
            target_set_.ConfigureSceneConsumer(consumer_);
        }) {
        const bool ready = PrepareFrame(prepare_context_);
        ConfigureBindings(bindings_...);
        if (scene_consumer_ != nullptr) {
            (void)ConfigureSceneConsumer(*scene_consumer_);
        }
        return ready;
    }

    template<typename... BindingTs>
    [[nodiscard]] bool OnSwapchainRecreatedAndConfigure(VulkanContext& context_,
                                                        RenderTargetHost& render_target_host_,
                                                        RenderTargetPool* render_target_pool_,
                                                        VkExtent2D swapchain_extent_,
                                                        std::uint64_t last_submitted_value_,
                                                        std::uint64_t completed_submit_value_,
                                                        RenderTargetCompositeRenderer* composite_renderer_,
                                                        const BindingTs&... bindings_) {
        const bool ready = OnSwapchainRecreated(context_,
                                                render_target_host_,
                                                render_target_pool_,
                                                swapchain_extent_,
                                                last_submitted_value_,
                                                completed_submit_value_);
        ConfigureBindings(bindings_...);
        if (composite_renderer_ != nullptr) {
            (void)ConfigureCompositeRenderer(*composite_renderer_);
        }
        return ready;
    }

    template<typename ConsumerT, typename... BindingTs>
    [[nodiscard]] bool OnSwapchainRecreatedAndConfigure(VulkanContext& context_,
                                                        RenderTargetHost& render_target_host_,
                                                        RenderTargetPool* render_target_pool_,
                                                        VkExtent2D swapchain_extent_,
                                                        std::uint64_t last_submitted_value_,
                                                        std::uint64_t completed_submit_value_,
                                                        ConsumerT* scene_consumer_,
                                                        const BindingTs&... bindings_)
        requires(requires(SceneRenderTargetSet& target_set_, ConsumerT& consumer_) {
            target_set_.ConfigureSceneConsumer(consumer_);
        }) {
        const bool ready = OnSwapchainRecreated(context_,
                                                render_target_host_,
                                                render_target_pool_,
                                                swapchain_extent_,
                                                last_submitted_value_,
                                                completed_submit_value_);
        ConfigureBindings(bindings_...);
        if (scene_consumer_ != nullptr) {
            (void)ConfigureSceneConsumer(*scene_consumer_);
        }
        return ready;
    }

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] bool HasDepthTarget() const noexcept;
    [[nodiscard]] RenderTargetHandle ColorTarget() const noexcept;
    [[nodiscard]] RenderTargetHandle DepthTarget() const noexcept;
    [[nodiscard]] VkFormat ColorFormat() const noexcept;
    [[nodiscard]] VkFormat DepthFormat() const noexcept;
    [[nodiscard]] const SceneRenderTargetSetCreateInfo& CreateInfo() const noexcept;

private:
    static constexpr std::size_t scene_render_pass_role_count = 4U;

    [[nodiscard]] bool SupportsSwapchainRelativeExtent() const noexcept;
    [[nodiscard]] static bool HasNonZeroExtent(VkExtent2D extent_) noexcept;
    [[nodiscard]] static constexpr std::size_t PassRoleIndex(SceneRenderPassRole pass_role_) noexcept {
        return static_cast<std::size_t>(pass_role_);
    }
    void InvalidateCurrentFrameTargets() noexcept;
    void RebuildCachedOutputConfigs() noexcept;

    template<typename RendererT>
    static constexpr bool supports_depth_target_config =
        requires(RendererT& renderer_,
                 const RenderTargetDepthOutputConfig& depth_output_config_) {
            renderer_.SetDepthTargetConfig(depth_output_config_);
            renderer_.ResetDepthTargetConfig();
        };

    template<typename BindingT>
    void ConfigureBinding(const BindingT& binding_) const {
        if (binding_.renderer == nullptr) {
            throw std::invalid_argument("SceneRenderTargetSet binding requires non-null renderer");
        }
        (void)ConfigureSceneRenderer(*binding_.renderer, binding_.pass_role);
    }

    template<typename... BindingTs>
    void ConfigureBindings(const BindingTs&... bindings_) const {
        (ConfigureBinding(bindings_), ...);
    }

    [[nodiscard]] static VkFormat ResolveColorFormat(VulkanContext& context_,
                                                     VkFormat requested_format_);
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_,
                                                     VkFormat requested_format_);
    void AcquireTransientTargets(VulkanContext& context_,
                                 RenderTargetHost& render_target_host_,
                                 RenderTargetPool& render_target_pool_,
                                 VkExtent2D swapchain_extent_);

private:
    SceneRenderTargetSetCreateInfo create_info_cache{};
    RenderTargetHandle color_target{};
    RenderTargetHandle depth_target{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    std::array<RenderTargetColorOutputConfig, scene_render_pass_role_count> cached_color_outputs{};
    std::array<RenderTargetDepthOutputConfig, scene_render_pass_role_count> cached_depth_outputs{};
    bool frame_ready = false;
    bool initialized = false;
};

} // namespace vr::render
