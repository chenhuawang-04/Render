#include "vr/render/scene_recorder_3d.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vr::render {

namespace {

constexpr float k_min_bloom_downsample_scale = 0.125F;
constexpr float k_max_bloom_downsample_scale = 1.0F;

[[nodiscard]] float ClampBloomDownsampleScale(const float value_) noexcept {
    if (!std::isfinite(value_)) {
        return k_min_bloom_downsample_scale;
    }
    return std::clamp(value_, k_min_bloom_downsample_scale, k_max_bloom_downsample_scale);
}

[[nodiscard]] render_graph::TextureFormat ResolveBloomIntermediateGraphFormat(
    const render::RenderTargetBloomRendererCreateInfo& bloom_create_info_) noexcept {
    switch (bloom_create_info_.intermediate_format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        return render_graph::TextureFormat::r8g8b8a8_unorm;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return render_graph::TextureFormat::r16g16b16a16_sfloat;
    default:
        break;
    }
    return render_graph::TextureFormat::r16g16b16a16_sfloat;
}

[[nodiscard]] std::string BuildIndexedBloomResourceName(std::string_view prefix_,
                                                        const std::uint32_t index_) {
    return std::string(prefix_) + "_" + std::to_string(index_);
}

[[nodiscard]] render_graph::TextureDesc BuildBloomIntermediateDesc(
    const render_graph::FrameSnapshot3D& snapshot_,
    const render::RenderTargetBloomRendererCreateInfo& bloom_create_info_) noexcept {
    const float scale = ClampBloomDownsampleScale(bloom_create_info_.downsample_scale);
    const std::uint32_t width = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(static_cast<float>(snapshot_.reference_extent.width) * scale));
    const std::uint32_t height = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(static_cast<float>(snapshot_.reference_extent.height) * scale));
    return render_graph::TextureDesc{
        .dimension = render_graph::TextureDimension::image_2d,
        .format = ResolveBloomIntermediateGraphFormat(bloom_create_info_),
        .extent = {.width = width, .height = height, .depth = 1U},
        .usage = render_graph::texture_usage_color_attachment_flag |
                 render_graph::texture_usage_sampled_flag,
        .mip_level_count = 1U,
        .array_layer_count = 1U,
        .sample_count = render_graph::SampleCount::x1,
        .allow_alias = true,
    };
}

[[nodiscard]] render_graph::TextureDesc BuildPostprocessOverlayIntermediateDesc(
    const render_graph::FrameSnapshot3D& snapshot_) noexcept {
    return render_graph::TextureDesc{
        .dimension = render_graph::TextureDimension::image_2d,
        .format = render_graph::TextureFormat::r16g16b16a16_sfloat,
        .extent = snapshot_.reference_extent,
        .usage = render_graph::texture_usage_color_attachment_flag |
                 render_graph::texture_usage_sampled_flag |
                 render_graph::texture_usage_transfer_src_flag,
        .mip_level_count = 1U,
        .array_layer_count = 1U,
        .sample_count = render_graph::SampleCount::x1,
        .allow_alias = true,
    };
}

} // namespace

void SceneRecorder3D::BuildRenderGraph(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::FrameSnapshot3D& snapshot_,
    const render_graph::MinimalFrameGraphBuildResult<ecs::Dim3>& build_result_,
    render_graph::ResourceVersionHandle& color_chain_) {
    if (!initialized) {
        return;
    }

    const bool shadow_enabled = IsShadowEnabledForSubmission();
    const bool graph_overlay_present = IsValidPassHandle(build_result_.overlay_pass);
    const bool graph_bloom_chain_enabled = HasSceneViewForSubmission() &&
                                           IsPostProcessEnabledForSubmission();
    const bool record_sky_after_opaque = HasSkyEnvironmentPassForSubmission() &&
                                         SkyEnvironmentDrawOrderForSubmission() ==
                                             scene::SkyEnvironmentDrawOrder::after_opaque_depth_tested &&
                                         build_result_.has_depth &&
                                         IsValidResourceHandle(build_result_.scene_depth);
    const bool record_sky_before_opaque = HasSkyEnvironmentPassForSubmission() &&
                                          !record_sky_after_opaque;

    if (record_sky_before_opaque && scene_view != nullptr && frame_packet != nullptr) {
        const auto sky_pass = builder_.AddPass("sky_environment_pre_opaque");
        if (IsValidPassHandle(build_result_.scene_pass)) {
            builder_.AddDependency(build_result_.scene_pass, sky_pass);
        }
        builder_.SetRasterPassDesc(sky_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = build_result_.scene_color,
                                               .load_op = render_graph::AttachmentLoadOp::clear,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                               .clear_value = {.red = 0.02F, .green = 0.02F, .blue = 0.03F, .alpha = 1.0F},
                                           },
                                       },
                                   });
        sky_environment_pass.DescribeGraphDescriptorBindings(builder_, sky_pass);
        const RenderView3D captured_view = *scene_view;
        const scene::SkyEnvironmentRenderState captured_state = frame_packet->extra.environment;
        const auto scene_color = build_result_.scene_color;
        builder_.SetExecuteCallback(sky_pass,
                                    [this, captured_view, captured_state, scene_color](render_graph::GraphCommandContext& context_) {
                                        const auto color_view = context_.ResolveTextureView(scene_color);
                                        sky_environment_pass.RecordGraphPass(context_,
                                                                             captured_view,
                                                                             captured_state,
                                                                             color_view.format,
                                                                             false,
                                                                             VK_FORMAT_UNDEFINED);
                                    });
        builder_.SetRasterPassDesc(build_result_.scene_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = build_result_.scene_color,
                                               .load_op = render_graph::AttachmentLoadOp::load,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                           },
                                       },
                                       .has_depth_attachment = build_result_.has_depth,
                                       .depth_attachment = render_graph::RasterDepthAttachmentDesc{
                                           .target = build_result_.scene_depth,
                                           .load_op = render_graph::AttachmentLoadOp::clear,
                                           .store_op = render_graph::AttachmentStoreOp::store,
                                           .stencil_load_op = render_graph::AttachmentLoadOp::dont_care,
                                           .stencil_store_op = render_graph::AttachmentStoreOp::dont_care,
                                           .clear_value = {.depth = 1.0F, .stencil = 0U},
                                       },
                                   });
    }

    if (record_sky_after_opaque &&
        scene_view != nullptr &&
        frame_packet != nullptr &&
        IsValidResourceHandle(build_result_.scene_color) &&
        IsValidResourceHandle(build_result_.scene_depth) &&
        IsValidResourceVersionHandle(color_chain_)) {
        const auto sky_pass = builder_.AddPass("sky_environment_post_opaque");
        (void)builder_.Read(sky_pass,
                            color_chain_,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_read});
        (void)builder_.Read(sky_pass,
                            build_result_.scene_depth,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::depth_stencil_read});
        color_chain_ = builder_.Write(sky_pass,
                                      build_result_.scene_color,
                                      render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
        builder_.SetRasterPassDesc(sky_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = build_result_.scene_color,
                                               .load_op = render_graph::AttachmentLoadOp::load,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                           },
                                       },
                                       .has_depth_attachment = true,
                                       .depth_attachment = render_graph::RasterDepthAttachmentDesc{
                                           .target = build_result_.scene_depth,
                                           .load_op = render_graph::AttachmentLoadOp::load,
                                           .store_op = render_graph::AttachmentStoreOp::store,
                                           .stencil_load_op = render_graph::AttachmentLoadOp::dont_care,
                                           .stencil_store_op = render_graph::AttachmentStoreOp::dont_care,
                                           .clear_value = {.depth = 1.0F, .stencil = 0U},
                                           .read_only = true,
                                       },
                                   });
        sky_environment_pass.DescribeGraphDescriptorBindings(builder_, sky_pass);
        const RenderView3D captured_view = *scene_view;
        const scene::SkyEnvironmentRenderState captured_state = frame_packet->extra.environment;
        const auto scene_color = build_result_.scene_color;
        const auto scene_depth = build_result_.scene_depth;
        builder_.SetExecuteCallback(sky_pass,
                                    [this, captured_view, captured_state, scene_color, scene_depth](render_graph::GraphCommandContext& context_) {
                                        const auto color_view = context_.ResolveTextureView(scene_color);
                                        const auto depth_view = context_.ResolveTextureView(scene_depth);
                                        sky_environment_pass.RecordGraphPass(context_,
                                                                             captured_view,
                                                                             captured_state,
                                                                             color_view.format,
                                                                             true,
                                                                             depth_view.format);
                                    });
    }

    std::vector<const SceneRendererEntry*> opaque_visible_entries{};
    for (const SceneRendererEntry& entry_ : scene_renderer_entries) {
        if (entry_.stage != SceneRecorder3DSceneStage::opaque ||
            !IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            continue;
        }
        if (entry_.renderer == nullptr) {
            continue;
        }
        if (entry_.graph_record_fn == nullptr) {
            throw std::runtime_error(
                "SceneRecorder3D::BuildRenderGraph requires graph-capable opaque scene renderer registration");
        }
        opaque_visible_entries.push_back(&entry_);
    }
    if (!opaque_visible_entries.empty() &&
        IsValidPassHandle(build_result_.scene_pass)) {
        for (const auto* entry_ : opaque_visible_entries) {
            if (entry_->describe_graph_bindings_fn != nullptr) {
                entry_->describe_graph_bindings_fn(entry_->renderer,
                                                   builder_,
                                                   build_result_.scene_pass);
            }
            if (graph_runtime_service != nullptr &&
                entry_->register_graph_imported_resources_fn != nullptr) {
                entry_->register_graph_imported_resources_fn(entry_->renderer,
                                                             *graph_runtime_service);
            }
        }
        const auto scene_color = build_result_.scene_color;
        const auto scene_depth = build_result_.scene_depth;
        builder_.SetExecuteCallback(build_result_.scene_pass,
                                    [opaque_visible_entries, scene_color, scene_depth](render_graph::GraphCommandContext& context_) {
                                        for (const auto* entry_ : opaque_visible_entries) {
                                            if (entry_->graph_record_fn == nullptr) {
                                                continue;
                                            }
                                            entry_->graph_record_fn(entry_->renderer,
                                                                    context_,
                                                                    SceneRenderStage::opaque,
                                                                    scene_color,
                                                                    scene_depth);
                                        }
                                    });
    }

    std::vector<const SceneRendererEntry*> transparent_visible_entries{};
    for (const SceneRendererEntry& entry_ : scene_renderer_entries) {
        if (entry_.stage != SceneRecorder3DSceneStage::transparent ||
            !IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            continue;
        }
        if (entry_.renderer == nullptr) {
            continue;
        }
        if (entry_.graph_record_fn == nullptr) {
            throw std::runtime_error(
                "SceneRecorder3D::BuildRenderGraph requires graph-capable transparent scene renderer registration");
        }
        transparent_visible_entries.push_back(&entry_);
    }
    if (!transparent_visible_entries.empty() &&
        IsValidResourceHandle(build_result_.scene_color) &&
        IsValidResourceVersionHandle(color_chain_)) {
        const auto transparent_pass = builder_.AddPass("transparent_scene_pass");
        (void)builder_.Read(transparent_pass,
                            color_chain_,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_read});
        if (IsValidResourceHandle(build_result_.scene_depth)) {
            (void)builder_.Read(transparent_pass,
                                build_result_.scene_depth,
                                render_graph::AccessDesc{.access = render_graph::AccessKind::depth_stencil_read});
        }
        color_chain_ = builder_.Write(transparent_pass,
                                      build_result_.scene_color,
                                      render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
        builder_.SetRasterPassDesc(transparent_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = build_result_.scene_color,
                                               .load_op = render_graph::AttachmentLoadOp::load,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                           },
                                       },
                                       .has_depth_attachment = build_result_.has_depth,
                                       .depth_attachment = render_graph::RasterDepthAttachmentDesc{
                                           .target = build_result_.scene_depth,
                                           .load_op = render_graph::AttachmentLoadOp::load,
                                           .store_op = render_graph::AttachmentStoreOp::store,
                                           .stencil_load_op = render_graph::AttachmentLoadOp::dont_care,
                                           .stencil_store_op = render_graph::AttachmentStoreOp::dont_care,
                                           .clear_value = {.depth = 1.0F, .stencil = 0U},
                                           .read_only = false,
                                       },
                                   });
        for (const auto* entry_ : transparent_visible_entries) {
            if (entry_->describe_graph_bindings_fn != nullptr) {
                entry_->describe_graph_bindings_fn(entry_->renderer,
                                                   builder_,
                                                   transparent_pass);
            }
            if (graph_runtime_service != nullptr &&
                entry_->register_graph_imported_resources_fn != nullptr) {
                entry_->register_graph_imported_resources_fn(entry_->renderer,
                                                             *graph_runtime_service);
            }
        }
        const auto scene_color = build_result_.scene_color;
        const auto scene_depth = build_result_.scene_depth;
        builder_.SetExecuteCallback(transparent_pass,
                                    [transparent_visible_entries, scene_color, scene_depth](render_graph::GraphCommandContext& context_) {
                                        for (const auto* entry_ : transparent_visible_entries) {
                                            if (entry_->graph_record_fn == nullptr) {
                                                continue;
                                            }
                                            entry_->graph_record_fn(entry_->renderer,
                                                                    context_,
                                                                    SceneRenderStage::transparent,
                                                                    scene_color,
                                                                    scene_depth);
                                        }
                                    });
    }

    if (graph_bloom_chain_enabled &&
        IsValidResourceHandle(build_result_.scene_color) &&
        IsValidResourceHandle(build_result_.present_target) &&
        IsValidResourceVersionHandle(color_chain_)) {
        const auto& bloom_create_info = bloom_renderer.CreateInfo();
        const auto bloom_desc = BuildBloomIntermediateDesc(snapshot_, bloom_create_info);
        const auto prefilter_target =
            builder_.CreateTexture("bloom_prefilter_target", bloom_desc);
        const auto postprocess_output_target = graph_overlay_present
            ? builder_.CreateTexture("postprocess_color",
                                     BuildPostprocessOverlayIntermediateDesc(snapshot_),
                                     render_graph::ResourceLifetime::transient)
            : build_result_.present_target;

        const auto prefilter_pass = builder_.AddPass("bloom_prefilter");
        (void)builder_.Read(prefilter_pass,
                            color_chain_,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
        auto bloom_chain = builder_.Write(prefilter_pass,
                                          prefilter_target,
                                          render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
        builder_.SetRasterPassDesc(prefilter_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = prefilter_target,
                                               .load_op = render_graph::AttachmentLoadOp::dont_care,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                           },
                                       },
                                   });
        bloom_renderer.DescribeGraphSingleSourceBindings(builder_, prefilter_pass);
        const auto scene_color = build_result_.scene_color;
        builder_.SetExecuteCallback(prefilter_pass,
                                    [this, scene_color, prefilter_target](render_graph::GraphCommandContext& context_) {
                                        bloom_renderer.RecordGraphPrefilterPass(context_, scene_color, prefilter_target);
                                    });

        auto bloom_chain_target = prefilter_target;
        const std::uint32_t blur_pair_count = std::max<std::uint32_t>(1U, bloom_create_info.blur_pass_pair_count);
        for (std::uint32_t blur_index = 0U; blur_index < blur_pair_count; ++blur_index) {
            const auto blur_h_target = builder_.CreateTexture(
                BuildIndexedBloomResourceName("bloom_blur_h_target", blur_index),
                bloom_desc);
            const auto blur_h_pass = builder_.AddPass("bloom_blur_h");
            (void)builder_.Read(blur_h_pass,
                                bloom_chain,
                                render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
            const auto blur_h_version = builder_.Write(blur_h_pass,
                                                       blur_h_target,
                                                       render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
            builder_.SetRasterPassDesc(blur_h_pass,
                                       render_graph::RasterPassDesc{
                                           .color_attachments = {
                                               render_graph::RasterColorAttachmentDesc{
                                                   .target = blur_h_target,
                                                   .load_op = render_graph::AttachmentLoadOp::dont_care,
                                                   .store_op = render_graph::AttachmentStoreOp::store,
                                               },
                                           },
                                       });
            bloom_renderer.DescribeGraphSingleSourceBindings(builder_, blur_h_pass);
            builder_.SetExecuteCallback(blur_h_pass,
                                        [this, bloom_chain_target, blur_h_target](render_graph::GraphCommandContext& context_) {
                                            bloom_renderer.RecordGraphBlurPass(context_, bloom_chain_target, blur_h_target);
                                        });

            const auto blur_v_target = builder_.CreateTexture(
                BuildIndexedBloomResourceName("bloom_blur_v_target", blur_index),
                bloom_desc);
            const auto blur_v_pass = builder_.AddPass("bloom_blur_v");
            (void)builder_.Read(blur_v_pass,
                                blur_h_version,
                                render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
            bloom_chain = builder_.Write(blur_v_pass,
                                         blur_v_target,
                                         render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
            builder_.SetRasterPassDesc(blur_v_pass,
                                       render_graph::RasterPassDesc{
                                           .color_attachments = {
                                               render_graph::RasterColorAttachmentDesc{
                                                   .target = blur_v_target,
                                                   .load_op = render_graph::AttachmentLoadOp::dont_care,
                                                   .store_op = render_graph::AttachmentStoreOp::store,
                                               },
                                           },
                                       });
            bloom_renderer.DescribeGraphSingleSourceBindings(builder_, blur_v_pass);
            builder_.SetExecuteCallback(blur_v_pass,
                                        [this, blur_h_target, blur_v_target](render_graph::GraphCommandContext& context_) {
                                            bloom_renderer.RecordGraphBlurPass(context_, blur_h_target, blur_v_target);
                                        });
            bloom_chain_target = blur_v_target;
        }

        const auto combine_pass = builder_.AddPass("bloom_combine");
        (void)builder_.Read(combine_pass,
                            color_chain_,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
        (void)builder_.Read(combine_pass,
                            bloom_chain,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
        color_chain_ = builder_.Write(combine_pass,
                                      postprocess_output_target,
                                      render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
        const auto combine_output_load_op = graph_overlay_present
            ? render_graph::AttachmentLoadOp::dont_care
            : (bloom_create_info.clear_swapchain
                ? render_graph::AttachmentLoadOp::clear
                : render_graph::AttachmentLoadOp::load);
        builder_.SetRasterPassDesc(combine_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = postprocess_output_target,
                                               .load_op = combine_output_load_op,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                               .clear_value = {
                                                   .red = bloom_create_info.clear_color.float32[0],
                                                   .green = bloom_create_info.clear_color.float32[1],
                                                   .blue = bloom_create_info.clear_color.float32[2],
                                                   .alpha = bloom_create_info.clear_color.float32[3],
                                               },
                                           },
                                       },
                                   });
        bloom_renderer.DescribeGraphDualSourceBindings(builder_, combine_pass);
        builder_.SetExecuteCallback(combine_pass,
                                    [this, scene_color, bloom_chain_target, postprocess_output_target](render_graph::GraphCommandContext& context_) {
                                        bloom_renderer.RecordGraphCombinePass(context_,
                                                                             scene_color,
                                                                             bloom_chain_target,
                                                                             postprocess_output_target);
                                    });
    }

    if (IsValidPassHandle(build_result_.overlay_pass) &&
        IsValidResourceHandle(build_result_.scene_color)) {
        std::vector<const OverlayRendererEntry*> visible_overlay_entries{};
        for (const OverlayRendererEntry& entry_ : overlay_renderer_entries) {
            if (!IsOverlayLayerVisibleForSubmission(entry_.submission_layer_mask)) {
                continue;
            }
            if (entry_.renderer == nullptr) {
                continue;
            }
            if (entry_.graph_record_fn == nullptr) {
                throw std::runtime_error(
                    "SceneRecorder3D::BuildRenderGraph requires graph-capable overlay renderer registration");
            }
            visible_overlay_entries.push_back(&entry_);
        }
        if (!visible_overlay_entries.empty()) {
            for (const auto* entry_ : visible_overlay_entries) {
                if (entry_->describe_graph_bindings_fn != nullptr) {
                    entry_->describe_graph_bindings_fn(entry_->renderer,
                                                       builder_,
                                                       build_result_.overlay_pass);
                }
                if (graph_runtime_service != nullptr &&
                    entry_->register_graph_imported_resources_fn != nullptr) {
                    entry_->register_graph_imported_resources_fn(entry_->renderer,
                                                                 *graph_runtime_service);
                }
            }
            const auto overlay_target = build_result_.scene_color;
            builder_.SetExecuteCallback(build_result_.overlay_pass,
                                        [visible_overlay_entries, overlay_target](render_graph::GraphCommandContext& context_) {
                                            for (const auto* entry_ : visible_overlay_entries) {
                                                if (entry_->graph_record_fn == nullptr) {
                                                    continue;
                                                }
                                                entry_->graph_record_fn(entry_->renderer, context_, overlay_target);
                                            }
                                        });
        }
    }

    for (const PreSceneRendererEntry& entry : pre_scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.kind == PreSceneRendererKind::shadow && !shadow_enabled) {
            continue;
        }
        if (entry.renderer == nullptr || entry.record_fn == nullptr) {
            continue;
        }

        const auto pre_scene_pass = builder_.AddPass(
            entry.kind == PreSceneRendererKind::shadow ? "shadow_prepass" : "pre_scene_pass",
            true);
        if (IsValidPassHandle(build_result_.scene_pass)) {
            builder_.AddDependency(build_result_.scene_pass, pre_scene_pass);
        }
        const auto* pre_scene_entry = &entry;
        builder_.SetExecuteCallback(
            pre_scene_pass,
            [pre_scene_entry](render_graph::GraphCommandContext& context_) {
                FrameRecordContext record_context{};
                record_context.frame_index = context_.FrameIndex();
                record_context.command_buffer = context_.CommandBuffer();
                record_context.render_target_host = &context_.RenderTargets();
                pre_scene_entry->record_fn(pre_scene_entry->renderer, record_context);
            });
    }
}

} // namespace vr::render
