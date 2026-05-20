#include "vr/render/frame_composer_host.hpp"


#include <limits>
#include <string>
#include <stdexcept>

namespace vr::render {

namespace {

[[nodiscard]] constexpr render_graph::TextureFormat ResolveGraphTextureFormat(
    const VkFormat format_) noexcept {
    switch (format_) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        return render_graph::TextureFormat::r8g8b8a8_unorm;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return render_graph::TextureFormat::r16g16b16a16_sfloat;
    case VK_FORMAT_D32_SFLOAT:
        return render_graph::TextureFormat::d32_sfloat;
    default:
        break;
    }
    return render_graph::TextureFormat::unknown;
}

[[nodiscard]] constexpr render_graph::SampleCount ResolveGraphSampleCount(
    const VkSampleCountFlagBits samples_) noexcept {
    switch (samples_) {
    case VK_SAMPLE_COUNT_2_BIT:
        return render_graph::SampleCount::x2;
    case VK_SAMPLE_COUNT_4_BIT:
        return render_graph::SampleCount::x4;
    case VK_SAMPLE_COUNT_8_BIT:
        return render_graph::SampleCount::x8;
    case VK_SAMPLE_COUNT_1_BIT:
    default:
        break;
    }
    return render_graph::SampleCount::x1;
}

} // namespace

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
    tonemap_output_override = false;
    stats = {};
    initialized = false;
}

bool FrameComposerHost::PrepareFrame(const FrameComposerPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("FrameComposerHost::PrepareFrame called before Initialize");
    }

    context = &prepare_view_.device;
    render_target_host = &prepare_view_.render_target;
    const bool ready = PrepareGraphManagedSceneTargets(prepare_view_);
    if (tonemap_output_override) {
        tonemap_renderer.SetOutputTargetConfig(tonemap_output_target_config);
    } else {
        tonemap_renderer.ResetOutputTargetConfig();
    }
    tonemap_renderer.PrepareFrame(MakeRenderTargetCompositeRendererPrepareView(prepare_view_));

    if (prepare_view_.frame.frame_index >= frame_targets.size()) {
        frame_targets.resize(prepare_view_.frame.frame_index + 1U);
    }
    if (ready) {
        RefreshFrameTargets(prepare_view_.frame.frame_index);
        ++stats.ready_frame_count;
    } else {
        frame_targets[prepare_view_.frame.frame_index] = {};
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
    (void)render_target_pool_;
    const bool ready = RefreshGraphManagedSceneTargets(context_,
                                                       render_target_host_,
                                                       swapchain_extent_,
                                                       last_submitted_value_,
                                                       completed_submit_value_);
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

void FrameComposerHost::BuildRenderGraph(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::ResourceHandle present_target_,
    const render_graph::Extent3D& reference_extent_,
    render_graph::ResourceVersionHandle& present_ready_version_,
    const ImportedTextureRegisterFn& register_imported_texture_) {
    (void)reference_extent_;
    if (!initialized) {
        throw std::runtime_error("FrameComposerHost::BuildRenderGraph called before Initialize");
    }
    if (render_target_host == nullptr) {
        throw std::runtime_error(
            "FrameComposerHost::BuildRenderGraph requires RenderTargetHost from PrepareFrame");
    }
    if (!scene_targets.IsReady() ||
        !IsValidRenderTargetHandle(scene_targets.ColorTarget())) {
        throw std::runtime_error(
            "FrameComposerHost::BuildRenderGraph requires ready graph-managed scene targets from PrepareFrame");
    }
    if (!register_imported_texture_) {
        throw std::runtime_error(
            "FrameComposerHost::BuildRenderGraph requires imported texture registration callback");
    }
    if (tonemap_output_override &&
        IsValidRenderTargetHandle(tonemap_output_target_config.color_target)) {
        throw std::runtime_error(
            "FrameComposerHost::BuildRenderGraph graph-only mainline does not support explicit tonemap output targets");
    }

    const RenderTargetHandle hdr_color_target = scene_targets.ColorTarget();
    const auto hdr_color_view = render_target_host->ResolveView(hdr_color_target);
    if (hdr_color_view.image == VK_NULL_HANDLE ||
        hdr_color_view.image_view == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "FrameComposerHost::BuildRenderGraph resolved invalid HDR source target view");
    }
    if (hdr_color_view.extent.width == 0U || hdr_color_view.extent.height == 0U) {
        throw std::runtime_error(
            "FrameComposerHost::BuildRenderGraph resolved zero-sized HDR source target");
    }

    const auto hdr_color_resource = builder_.CreateTexture(
        "frame_composer_hdr_color",
        render_graph::TextureDesc{
            .dimension = render_graph::TextureDimension::image_2d,
            .format = ResolveGraphTextureFormat(hdr_color_view.format),
            .extent = {
                .width = hdr_color_view.extent.width,
                .height = hdr_color_view.extent.height,
                .depth = hdr_color_view.extent.depth,
            },
            .usage = render_graph::texture_usage_sampled_flag |
                     render_graph::texture_usage_color_attachment_flag,
            .mip_level_count = 1U,
            .array_layer_count = 1U,
            .sample_count = ResolveGraphSampleCount(hdr_color_view.samples),
        },
        render_graph::ResourceLifetime::imported);
    register_imported_texture_(hdr_color_resource, hdr_color_target);

    const auto tonemap_pass = builder_.AddPass("frame_composer_tonemap");
    (void)builder_.Read(tonemap_pass,
                        hdr_color_resource,
                        render_graph::AccessDesc{
                            .access = render_graph::AccessKind::shader_sample_read,
                        });
    present_ready_version_ = builder_.Write(
        tonemap_pass,
        present_target_,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::color_attachment_write,
        });
    builder_.SetRasterPassDesc(
        tonemap_pass,
        render_graph::RasterPassDesc{
            .color_attachments = {
                tonemap_renderer.BuildGraphColorAttachmentDesc(present_target_, false),
            },
        });
    tonemap_renderer.DescribeGraphDescriptorBindings(builder_, tonemap_pass);
    builder_.SetExecuteCallback(
        tonemap_pass,
        [this, hdr_color_resource, present_target_](render_graph::GraphCommandContext& context_) {
            const auto previous_draw_call_count = tonemap_renderer.Stats().draw_call_count;
            const auto previous_skipped_draw_count = tonemap_renderer.Stats().skipped_draw_count;
            tonemap_renderer.RecordGraphPass(context_,
                                             hdr_color_resource,
                                             present_target_);
            AccumulateTonemapStats(previous_draw_call_count,
                                   previous_skipped_draw_count);
        });
}

void FrameComposerHost::RecordTonemapPass(const FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("FrameComposerHost::RecordTonemapPass called before Initialize");
    }
    const auto previous_draw_call_count = tonemap_renderer.Stats().draw_call_count;
    const auto previous_skipped_draw_count = tonemap_renderer.Stats().skipped_draw_count;
    tonemap_renderer.Record(record_context_);
    AccumulateTonemapStats(previous_draw_call_count,
                           previous_skipped_draw_count);
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

void FrameComposerHost::EnsureGraphManagedSceneTargetContract(const char* operation_) const {
    const bool uses_transient_color =
        create_info_cache.color_lifetime == RenderTargetLifetime::transient;
    const bool uses_transient_depth =
        create_info_cache.depth_lifetime == RenderTargetLifetime::transient;
    if (!uses_transient_color && !uses_transient_depth) {
        return;
    }

    throw std::runtime_error(std::string("FrameComposerHost::") +
                             operation_ +
                             " graph-only mainline does not support transient scene targets");
}

bool FrameComposerHost::PrepareGraphManagedSceneTargets(
    const FrameComposerPrepareView& prepare_view_) {
    EnsureGraphManagedSceneTargetContract("PrepareFrame");
    const bool ready = scene_targets.PrepareFrame(SceneRenderTargetSetPrepareView{
        .device = prepare_view_.device,
        .render_target = prepare_view_.render_target,
        .render_target_pool = nullptr,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    });
    if (ready) {
        (void)scene_targets.ConfigureCompositeRenderer(tonemap_renderer);
    } else {
        scene_targets.ResetCompositeRenderer(tonemap_renderer);
    }
    return ready;
}

bool FrameComposerHost::RefreshGraphManagedSceneTargets(
    VulkanContext& context_,
    RenderTargetHost& render_target_host_,
    VkExtent2D swapchain_extent_,
    std::uint64_t last_submitted_value_,
    std::uint64_t completed_submit_value_) {
    EnsureGraphManagedSceneTargetContract("OnSwapchainRecreated");
    const bool ready = scene_targets.OnSwapchainRecreated(context_,
                                                          render_target_host_,
                                                          nullptr,
                                                          swapchain_extent_,
                                                          last_submitted_value_,
                                                          completed_submit_value_);
    if (ready) {
        (void)scene_targets.ConfigureCompositeRenderer(tonemap_renderer);
    } else {
        scene_targets.ResetCompositeRenderer(tonemap_renderer);
    }
    return ready;
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

void FrameComposerHost::AccumulateTonemapStats(
    const std::uint32_t previous_draw_call_count_,
    const std::uint32_t previous_skipped_draw_count_) noexcept {
    const auto& tonemap_stats = tonemap_renderer.Stats();
    if (tonemap_stats.draw_call_count > previous_draw_call_count_) {
        ++stats.tonemap_record_count;
    }
    if (tonemap_stats.skipped_draw_count > previous_skipped_draw_count_) {
        ++stats.tonemap_skipped_count;
    }
    ++stats.revision;
}

} // namespace vr::render

