#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/render_target_composite_renderer.hpp"
#include "vr/render/scene_render_target_set.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <cstdint>
#include <functional>

namespace vr {
class VulkanContext;
}

namespace vr::render {

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
    using ImportedTextureRegisterFn = std::function<void(
        render_graph::ResourceHandle,
        RenderTargetHandle)>;

    void Initialize(const FrameComposerHostCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    [[nodiscard]] bool PrepareFrame(const FrameComposerPrepareView& prepare_view_);
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

    void BuildRenderGraph(render_graph::RenderGraphBuilder& builder_,
                          render_graph::ResourceHandle present_target_,
                          const render_graph::Extent3D& reference_extent_,
                          render_graph::ResourceVersionHandle& present_ready_version_,
                          const ImportedTextureRegisterFn& register_imported_texture_);
    void RecordTonemapPass(const FrameRecordContext& record_context_);

    [[nodiscard]] const FrameComposerTargets& Targets(std::uint32_t frame_index_) const;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const FrameComposerHostStats& Stats() const noexcept;
    [[nodiscard]] const FrameComposerHostCreateInfo& CreateInfo() const noexcept;

private:
    [[nodiscard]] static SceneRenderTargetSetCreateInfo BuildSceneRenderTargetSetCreateInfo(
        const FrameComposerHostCreateInfo& create_info_) noexcept;
    void EnsureGraphManagedSceneTargetContract(const char* operation_) const;
    [[nodiscard]] bool PrepareGraphManagedSceneTargets(const FrameComposerPrepareView& prepare_view_);
    [[nodiscard]] bool RefreshGraphManagedSceneTargets(VulkanContext& context_,
                                                       RenderTargetHost& render_target_host_,
                                                       VkExtent2D swapchain_extent_,
                                                       std::uint64_t last_submitted_value_,
                                                       std::uint64_t completed_submit_value_);
    void RefreshFrameTargets(std::uint32_t frame_index_) noexcept;
    void ClearFrameTargets() noexcept;
    void DestroyOwnedTargets(VulkanContext& context_) noexcept;
    void AccumulateTonemapStats(std::uint32_t previous_draw_call_count_,
                                std::uint32_t previous_skipped_draw_count_) noexcept;

private:
    FrameComposerHostCreateInfo create_info_cache{};
    FrameComposerHostStats stats{};
    SceneRenderTargetSet scene_targets{};
    RenderTargetCompositeRenderer tonemap_renderer{};
    FrameComposerMcVector<FrameComposerTargets> frame_targets{};
    RenderTargetColorOutputConfig tonemap_output_target_config{};
    VulkanContext* context = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    bool tonemap_output_override = false;
    bool initialized = false;
};

} // namespace vr::render

