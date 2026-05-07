#include "vr/render/frame_composer_host.hpp"

#include "vr/render/runtime_prepare_context.hpp"

#include <limits>
#include <stdexcept>

namespace vr::render {

void FrameComposerHost::Initialize(const FrameComposerHostCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    scene_targets.Initialize(BuildSceneRenderTargetSetCreateInfo(create_info_cache));

    RenderTargetCompositeRendererCreateInfo tonemap_create_info{};
    tonemap_create_info.clear_swapchain = create_info_cache.clear_swapchain;
    tonemap_create_info.clear_color = create_info_cache.clear_color;
    tonemap_create_info.enable_reinhard_tonemap = create_info_cache.enable_reinhard_tonemap;
    tonemap_create_info.exposure = create_info_cache.exposure;
    tonemap_create_info.output_gamma = create_info_cache.output_gamma;
    tonemap_create_info.apply_manual_gamma = create_info_cache.apply_manual_gamma;
    tonemap_renderer.Initialize(tonemap_create_info);

    frame_targets.clear();
    tonemap_output_target_config = {};
    context = nullptr;
    render_target_host = nullptr;
    render_target_pool = nullptr;
    tonemap_output_override = false;
    initialized = true;
}

void FrameComposerHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    DestroyOwnedTargets(context_);
    tonemap_renderer.Shutdown(context_);
    scene_targets.Reset();
    ClearFrameTargets();
    tonemap_output_target_config = {};
    context = nullptr;
    render_target_host = nullptr;
    render_target_pool = nullptr;
    tonemap_output_override = false;
    stats = {};
    initialized = false;
}

bool FrameComposerHost::PrepareFrame(const RuntimePrepareContext& prepare_context_) {
    if (!initialized) {
        throw std::runtime_error("FrameComposerHost::PrepareFrame called before Initialize");
    }
    if (prepare_context_.context == nullptr ||
        prepare_context_.render_target_host == nullptr ||
        prepare_context_.descriptor_host == nullptr ||
        prepare_context_.pipeline_host == nullptr ||
        prepare_context_.sampler_host == nullptr) {
        throw std::runtime_error("FrameComposerHost::PrepareFrame missing runtime dependencies");
    }

    context = prepare_context_.context;
    render_target_host = prepare_context_.render_target_host;
    render_target_pool = prepare_context_.render_target_pool;

    const bool ready = scene_targets.PrepareFrameAndConfigure(prepare_context_, &tonemap_renderer);
    if (tonemap_output_override) {
        tonemap_renderer.SetOutputTargetConfig(tonemap_output_target_config);
    } else {
        tonemap_renderer.ResetOutputTargetConfig();
    }

    if (prepare_context_.frame_index >= frame_targets.size()) {
        frame_targets.resize(prepare_context_.frame_index + 1U);
    }
    if (ready) {
        RefreshFrameTargets(prepare_context_.frame_index);
        ++stats.ready_frame_count;
    } else {
        frame_targets[prepare_context_.frame_index] = {};
    }

    ++stats.prepared_frame_count;
    ++stats.revision;
    return ready;
}

bool FrameComposerHost::OnSwapchainRecreated(VulkanContext& context_,
                                             RenderTargetHost& render_target_host_,
                                             RenderTargetPool* render_target_pool_,
                                             VkExtent2D swapchain_extent_,
                                             std::uint64_t last_submitted_value_,
                                             std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("FrameComposerHost::OnSwapchainRecreated called before Initialize");
    }

    context = &context_;
    render_target_host = &render_target_host_;
    render_target_pool = render_target_pool_;

    const bool ready =
        scene_targets.OnSwapchainRecreatedAndConfigure(context_,
                                                       render_target_host_,
                                                       render_target_pool_,
                                                       swapchain_extent_,
                                                       last_submitted_value_,
                                                       completed_submit_value_,
                                                       &tonemap_renderer);
    if (tonemap_output_override) {
        tonemap_renderer.SetOutputTargetConfig(tonemap_output_target_config);
    } else {
        tonemap_renderer.ResetOutputTargetConfig();
    }

    tonemap_renderer.OnSwapchainRecreated(frame_targets.empty()
                                              ? 0U
                                              : static_cast<std::uint32_t>(frame_targets.size()),
                                          swapchain_extent_,
                                          VK_FORMAT_UNDEFINED);

    for (std::uint32_t frame_index = 0U; frame_index < frame_targets.size(); ++frame_index) {
        if (ready) {
            RefreshFrameTargets(frame_index);
        } else {
            frame_targets[frame_index] = {};
        }
    }

    ++stats.swapchain_recreate_count;
    ++stats.revision;
    return ready;
}

void FrameComposerHost::SetTonemapOutputTargetConfig(
    const RenderTargetColorOutputConfig& output_target_config_) noexcept {
    tonemap_output_target_config = output_target_config_;
    tonemap_output_override = true;
}

void FrameComposerHost::ResetTonemapOutputTargetConfig() noexcept {
    tonemap_output_target_config = {};
    tonemap_output_override = false;
}

void FrameComposerHost::RecordTonemapPass(const FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("FrameComposerHost::RecordTonemapPass called before Initialize");
    }
    tonemap_renderer.Record(record_context_);
    const auto& tonemap_stats = tonemap_renderer.Stats();
    if (tonemap_stats.draw_call_count > 0U) {
        ++stats.tonemap_record_count;
    }
    if (tonemap_stats.skipped_draw_count > 0U) {
        ++stats.tonemap_skipped_count;
    }
    ++stats.revision;
}

const FrameComposerTargets& FrameComposerHost::Targets(std::uint32_t frame_index_) const {
    if (!initialized) {
        throw std::runtime_error("FrameComposerHost::Targets called before Initialize");
    }
    if (frame_index_ >= frame_targets.size()) {
        throw std::out_of_range("FrameComposerHost::Targets frame index out of range");
    }
    return frame_targets[frame_index_];
}

bool FrameComposerHost::IsInitialized() const noexcept {
    return initialized;
}

const FrameComposerHostStats& FrameComposerHost::Stats() const noexcept {
    return stats;
}

const FrameComposerHostCreateInfo& FrameComposerHost::CreateInfo() const noexcept {
    return create_info_cache;
}

SceneRenderTargetSetCreateInfo FrameComposerHost::BuildSceneRenderTargetSetCreateInfo(
    const FrameComposerHostCreateInfo& create_info_) noexcept {
    SceneRenderTargetSetCreateInfo target_set_create_info{};
    target_set_create_info.color_debug_name = create_info_.color_debug_name;
    target_set_create_info.depth_debug_name = create_info_.depth_debug_name;
    target_set_create_info.scale_mode = RenderTargetScaleMode::swapchain_relative;
    target_set_create_info.enable_depth = true;
    target_set_create_info.color_format = create_info_.hdr_color_format;
    target_set_create_info.depth_format = create_info_.depth_format;
    target_set_create_info.samples = create_info_.samples;
    target_set_create_info.additional_color_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    target_set_create_info.additional_depth_usage = 0U;
    target_set_create_info.color_lifetime = create_info_.color_lifetime;
    target_set_create_info.depth_lifetime = create_info_.depth_lifetime;
    target_set_create_info.color_encoding = RenderTargetColorEncoding::linear;
    target_set_create_info.color_final_state = create_info_.color_final_state;
    target_set_create_info.depth_final_state = create_info_.depth_final_state;
    target_set_create_info.clear_color = create_info_.clear_color;
    target_set_create_info.clear_depth_value = create_info_.clear_depth_value;
    target_set_create_info.clear_stencil_value = create_info_.clear_stencil_value;
    return target_set_create_info;
}

void FrameComposerHost::RefreshFrameTargets(std::uint32_t frame_index_) noexcept {
    if (frame_index_ >= frame_targets.size()) {
        frame_targets.resize(frame_index_ + 1U);
    }

    FrameComposerTargets& targets = frame_targets[frame_index_];
    targets.hdr_color_target = scene_targets.ColorTarget();
    targets.depth_target = scene_targets.DepthTarget();
    targets.ready = scene_targets.IsReady();
    if (targets.ready) {
        targets.scene_color_output = scene_targets.BuildColorOutputConfig(true, true);
        targets.scene_depth_output = scene_targets.BuildDepthOutputConfig(true);
    } else {
        targets.scene_color_output = {};
        targets.scene_depth_output = {};
    }
}

void FrameComposerHost::ClearFrameTargets() noexcept {
    for (auto& target_slot : frame_targets) {
        target_slot = {};
    }
    frame_targets.clear();
}

void FrameComposerHost::DestroyOwnedTargets(VulkanContext& context_) noexcept {
    if (render_target_host == nullptr || !scene_targets.IsReady()) {
        return;
    }

    const std::uint64_t completed_submit_value = std::numeric_limits<std::uint64_t>::max();
    if (create_info_cache.color_lifetime == RenderTargetLifetime::persistent &&
        IsValidRenderTargetHandle(scene_targets.ColorTarget())) {
        (void)render_target_host->DestroyTarget(context_,
                                                scene_targets.ColorTarget(),
                                                completed_submit_value,
                                                completed_submit_value);
    }
    if (create_info_cache.depth_lifetime == RenderTargetLifetime::persistent &&
        IsValidRenderTargetHandle(scene_targets.DepthTarget())) {
        (void)render_target_host->DestroyTarget(context_,
                                                scene_targets.DepthTarget(),
                                                completed_submit_value,
                                                completed_submit_value);
    }
}

} // namespace vr::render
