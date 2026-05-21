#include "vr/render/frame_composer_host.hpp"

#include <algorithm>
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

    RenderTargetCompositeRendererCreateInfo tonemap_create_info{};
    tonemap_create_info.clear_swapchain = create_info_cache.clear_swapchain;
    tonemap_create_info.clear_color = create_info_cache.clear_color;
    tonemap_create_info.enable_reinhard_tonemap = create_info_cache.enable_reinhard_tonemap;
    tonemap_create_info.exposure = create_info_cache.exposure;
    tonemap_create_info.output_gamma = create_info_cache.output_gamma;
    tonemap_create_info.apply_manual_gamma = create_info_cache.apply_manual_gamma;
    tonemap_renderer.Initialize(tonemap_create_info);

    prepared_frame_slot_count = 0U;
    initialized = true;
}

void FrameComposerHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    tonemap_renderer.Shutdown(context_);
    prepared_frame_slot_count = 0U;
    stats = {};
    initialized = false;
}

bool FrameComposerHost::PrepareFrame(const FrameComposerPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("FrameComposerHost::PrepareFrame called before Initialize");
    }

    tonemap_renderer.PrepareFrame(MakeRenderTargetCompositeRendererPrepareView(prepare_view_));

    if (prepare_view_.frame.frame_index + 1U > prepared_frame_slot_count) {
        prepared_frame_slot_count = prepare_view_.frame.frame_index + 1U;
    }

    const bool ready = HasNonZeroExtent(prepare_view_.frame.swapchain_extent);
    if (ready) {
        ++stats.ready_frame_count;
    }

    ++stats.prepared_frame_count;
    ++stats.revision;
    return ready;
}

bool FrameComposerHost::OnSwapchainRecreated(VulkanContext&,
                                             RenderTargetHost&,
                                             const VkExtent2D swapchain_extent_,
                                             std::uint64_t,
                                             std::uint64_t) {
    if (!initialized) {
        throw std::runtime_error("FrameComposerHost::OnSwapchainRecreated called before Initialize");
    }

    tonemap_renderer.OnSwapchainRecreated(prepared_frame_slot_count,
                                          swapchain_extent_,
                                          VK_FORMAT_UNDEFINED);

    ++stats.swapchain_recreate_count;
    ++stats.revision;
    return HasNonZeroExtent(swapchain_extent_);
}

void FrameComposerHost::BuildRenderGraph(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::ResourceHandle present_target_,
    const render_graph::Extent3D& reference_extent_,
    render_graph::ResourceVersionHandle& present_ready_version_,
    const ImportedTextureRegisterFn& register_imported_texture_) {
    (void)register_imported_texture_;
    if (!initialized) {
        throw std::runtime_error("FrameComposerHost::BuildRenderGraph called before Initialize");
    }

    const render_graph::Extent3D hdr_extent{
        .width = std::max<std::uint32_t>(1U, reference_extent_.width),
        .height = std::max<std::uint32_t>(1U, reference_extent_.height),
        .depth = std::max<std::uint32_t>(1U, reference_extent_.depth),
    };
    const auto hdr_color_resource = builder_.CreateTexture(
        "frame_composer_hdr_color",
        render_graph::TextureDesc{
            .dimension = render_graph::TextureDimension::image_2d,
            .format = ResolveGraphTextureFormat(create_info_cache.hdr_color_format),
            .extent = hdr_extent,
            .usage = render_graph::texture_usage_sampled_flag |
                     render_graph::texture_usage_color_attachment_flag,
            .mip_level_count = 1U,
            .array_layer_count = 1U,
            .sample_count = ResolveGraphSampleCount(create_info_cache.samples),
            .allow_alias = true,
        },
        render_graph::ResourceLifetime::transient);

    const auto hdr_clear_pass = builder_.AddPass("frame_composer_hdr_clear");
    const auto hdr_ready_version = builder_.Write(
        hdr_clear_pass,
        hdr_color_resource,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::color_attachment_write,
        });
    builder_.SetRasterPassDesc(
        hdr_clear_pass,
        render_graph::RasterPassDesc{
            .color_attachments = {
                render_graph::RasterColorAttachmentDesc{
                    .target = hdr_color_resource,
                    .load_op = render_graph::AttachmentLoadOp::clear,
                    .store_op = render_graph::AttachmentStoreOp::store,
                    .clear_value = {
                        .red = create_info_cache.clear_color.float32[0],
                        .green = create_info_cache.clear_color.float32[1],
                        .blue = create_info_cache.clear_color.float32[2],
                        .alpha = create_info_cache.clear_color.float32[3],
                    },
                },
            },
        });
    builder_.SetExecuteCallback(hdr_clear_pass, [](render_graph::GraphCommandContext&) {});

    const auto tonemap_pass = builder_.AddPass("frame_composer_tonemap");
    (void)builder_.Read(tonemap_pass,
                        hdr_ready_version,
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

bool FrameComposerHost::IsInitialized() const noexcept {
    return initialized;
}

const FrameComposerHostStats& FrameComposerHost::Stats() const noexcept {
    return stats;
}

const FrameComposerHostCreateInfo& FrameComposerHost::CreateInfo() const noexcept {
    return create_info_cache;
}

bool FrameComposerHost::HasNonZeroExtent(const VkExtent2D extent_) noexcept {
    return extent_.width > 0U && extent_.height > 0U;
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
