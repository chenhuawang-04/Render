#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/render_target_composite_renderer.hpp"
#include "vr/render/scene_render_target_set.hpp"

#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render {

struct RuntimePrepareContext;
class RenderTargetHost;
class RenderTargetPool;

template<typename T>
using FrameComposerMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct FrameComposerHostCreateInfo final {
    const char* color_debug_name = "FrameComposerHdrColor";
    const char* depth_debug_name = "FrameComposerDepth";
    VkFormat hdr_color_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    RenderTargetLifetime color_lifetime = RenderTargetLifetime::persistent;
    RenderTargetLifetime depth_lifetime = RenderTargetLifetime::persistent;
    RenderTargetStateKind color_final_state = RenderTargetStateKind::shader_read;
    RenderTargetStateKind depth_final_state = RenderTargetStateKind::depth_attachment;
    VkClearColorValue clear_color = {{0.02F, 0.02F, 0.03F, 1.0F}};
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
    bool clear_swapchain = true;
    bool enable_reinhard_tonemap = true;
    float exposure = 1.0F;
    float output_gamma = 2.2F;
    bool apply_manual_gamma = false;
};

struct FrameComposerTargets final {
    RenderTargetHandle hdr_color_target{};
    RenderTargetHandle depth_target{};
    RenderTargetColorOutputConfig scene_color_output{};
    RenderTargetDepthOutputConfig scene_depth_output{};
    bool ready = false;
};

struct FrameComposerHostStats final {
    std::uint32_t prepared_frame_count = 0U;
    std::uint32_t ready_frame_count = 0U;
    std::uint32_t swapchain_recreate_count = 0U;
    std::uint32_t tonemap_record_count = 0U;
    std::uint32_t tonemap_skipped_count = 0U;
    std::uint32_t revision = 0U;
};

class FrameComposerHost final {
public:
    void Initialize(const FrameComposerHostCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    [[nodiscard]] bool PrepareFrame(const RuntimePrepareContext& prepare_context_);
    [[nodiscard]] bool OnSwapchainRecreated(VulkanContext& context_,
                                            RenderTargetHost& render_target_host_,
                                            RenderTargetPool* render_target_pool_,
                                            VkExtent2D swapchain_extent_,
                                            std::uint64_t last_submitted_value_,
                                            std::uint64_t completed_submit_value_);

    template<typename RendererT>
    [[nodiscard]] bool ConfigureSceneRenderer(RendererT& renderer_,
                                              SceneRenderPassRole pass_role_) const {
        return scene_targets.ConfigureSceneRenderer(renderer_, pass_role_);
    }

    template<typename RendererT>
    void ResetSceneRenderer(RendererT& renderer_) const {
        scene_targets.ResetSceneRenderer(renderer_);
    }

    void SetTonemapOutputTargetConfig(const RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetTonemapOutputTargetConfig() noexcept;

    void RecordTonemapPass(const FrameRecordContext& record_context_);

    [[nodiscard]] const FrameComposerTargets& Targets(std::uint32_t frame_index_) const;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const FrameComposerHostStats& Stats() const noexcept;
    [[nodiscard]] const FrameComposerHostCreateInfo& CreateInfo() const noexcept;

private:
    [[nodiscard]] static SceneRenderTargetSetCreateInfo BuildSceneRenderTargetSetCreateInfo(
        const FrameComposerHostCreateInfo& create_info_) noexcept;
    void RefreshFrameTargets(std::uint32_t frame_index_) noexcept;
    void ClearFrameTargets() noexcept;
    void DestroyOwnedTargets(VulkanContext& context_) noexcept;

private:
    FrameComposerHostCreateInfo create_info_cache{};
    FrameComposerHostStats stats{};
    SceneRenderTargetSet scene_targets{};
    RenderTargetCompositeRenderer tonemap_renderer{};
    FrameComposerMcVector<FrameComposerTargets> frame_targets{};
    RenderTargetColorOutputConfig tonemap_output_target_config{};
    VulkanContext* context = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    RenderTargetPool* render_target_pool = nullptr;
    bool tonemap_output_override = false;
    bool initialized = false;
};

} // namespace vr::render
