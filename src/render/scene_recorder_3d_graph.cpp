#include "vr/render/scene_recorder_3d.hpp"

#include "render_target_temporal_motion_renderer.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "render_target_temporal_resolve_renderer.hpp"
#include "vr/render_graph/frame_temporal_consumer.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"

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
constexpr float k_temporal_resolve_current_weight = 0.90F;
constexpr float k_temporal_resolve_previous_weight = 0.10F;
constexpr float k_temporal_resolve_rejection_begin_pixels = 0.75F;
constexpr float k_temporal_resolve_rejection_end_pixels = 2.5F;
constexpr float k_temporal_resolve_depth_rejection_begin = 0.0015F;
constexpr float k_temporal_resolve_depth_rejection_end = 0.02F;

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

[[nodiscard]] render_graph::TextureDesc BuildTemporalResolveIntermediateDesc(
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

[[nodiscard]] ecs::Matrix4x4 ApplyProjectionUvJitter(
    const ecs::Matrix4x4& projection_,
    const float jitter_uv_x_,
    const float jitter_uv_y_) noexcept {
    ecs::Matrix4x4 jittered = projection_;
    const float jitter_ndc_x = jitter_uv_x_ * 2.0F;
    const float jitter_ndc_y = jitter_uv_y_ * 2.0F;

    jittered.m[0U] += projection_.m[3U] * jitter_ndc_x;
    jittered.m[4U] += projection_.m[7U] * jitter_ndc_x;
    jittered.m[8U] += projection_.m[11U] * jitter_ndc_x;
    jittered.m[12U] += projection_.m[15U] * jitter_ndc_x;

    jittered.m[1U] += projection_.m[3U] * jitter_ndc_y;
    jittered.m[5U] += projection_.m[7U] * jitter_ndc_y;
    jittered.m[9U] += projection_.m[11U] * jitter_ndc_y;
    jittered.m[13U] += projection_.m[15U] * jitter_ndc_y;
    return jittered;
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
    const bool temporal_resolve_requested =
        temporal_resolve_renderer != nullptr &&
        temporal_resolve_renderer->HasPreparedRuntimeState() &&
        graph_runtime_service != nullptr &&
        IsPostProcessEnabledForSubmission();
    const bool record_sky_after_opaque = HasSkyEnvironmentPassForSubmission() &&
                                         SkyEnvironmentDrawOrderForSubmission() ==
                                             scene::SkyEnvironmentDrawOrder::after_opaque_depth_tested &&
                                         build_result_.has_depth &&
                                         IsValidResourceHandle(build_result_.scene_depth);
    const bool record_sky_before_opaque = HasSkyEnvironmentPassForSubmission() &&
                                          !record_sky_after_opaque;
    const auto* snapshot_scene_view = snapshot_.SceneView();
    ecs::Matrix4x4 temporal_view_projection_override{};
    const bool has_temporal_view_projection_override =
        snapshot_scene_view != nullptr &&
        snapshot_scene_view->has_camera != 0U &&
        snapshot_.temporal.jitter.current_available;
    if (has_temporal_view_projection_override) {
        const ecs::Matrix4x4 jittered_projection =
            ApplyProjectionUvJitter(
                snapshot_scene_view->camera.runtime.projection_matrix,
                snapshot_.temporal.jitter.current_uv_x,
                snapshot_.temporal.jitter.current_uv_y);
        temporal_view_projection_override =
            ecs::spatial_math::MultiplyMatrix4x4(
                jittered_projection,
                snapshot_scene_view->camera.runtime.view_matrix);
    }

    auto configure_temporal_view_projection_for_renderer =
        [&](const SceneRendererEntry& entry_) {
            if (!IsLayerVisibleForSubmission(entry_.submission_layer_mask) ||
                entry_.renderer == nullptr ||
                entry_.configure_temporal_view_projection_fn == nullptr ||
                !IsFirstSceneRendererEntryForRenderer(entry_)) {
                return;
            }
            entry_.configure_temporal_view_projection_fn(
                entry_.renderer,
                has_temporal_view_projection_override
                    ? &temporal_view_projection_override
                    : nullptr);
        };
    ForEachSceneRendererInStageOrder(
        configure_temporal_view_projection_for_renderer);

    auto clear_temporal_motion_producer_state_for_renderer =
        [&](const SceneRendererEntry& entry_) {
            if (entry_.renderer == nullptr ||
                entry_.configure_temporal_motion_producer_state_fn == nullptr ||
                !IsFirstSceneRendererEntryForRenderer(entry_)) {
                return;
            }
            entry_.configure_temporal_motion_producer_state_fn(entry_.renderer,
                                                               nullptr);
        };
    ForEachSceneRendererInStageOrder(
        clear_temporal_motion_producer_state_for_renderer);

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
        const scene::SkyEnvironmentRenderState captured_state = frame_packet->Payload().environment;
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
        const scene::SkyEnvironmentRenderState captured_state = frame_packet->Payload().environment;
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
        const auto postprocess_output_target =
            (graph_overlay_present || temporal_resolve_requested)
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

    const auto register_imported_texture =
        [this](const render_graph::ResourceHandle logical_,
               const render::RenderTargetHandle render_target_) {
            graph_runtime_service->RegisterImportedTexture(logical_,
                                                           render_target_);
        };
    render_graph::ImportedTemporalSurface current_motion{};
    render_graph::ResourceVersionHandle current_motion_version =
        render_graph::invalid_resource_version;
    const bool temporal_motion_enabled =
        temporal_motion_renderer != nullptr &&
        temporal_motion_renderer->HasPreparedRuntimeState() &&
        graph_runtime_service != nullptr &&
        build_result_.has_depth &&
        IsValidResourceHandle(build_result_.scene_depth) &&
        snapshot_.temporal.motion.current_writable;
    if (temporal_motion_enabled) {
        current_motion =
            render_graph::ImportCurrentTemporalSurface(
                builder_,
                "frame_motion_history_current",
                snapshot_.temporal.motion,
                register_imported_texture);
        if (current_motion.available) {
            const auto motion_pass = builder_.AddPass("temporal_motion_vector_pass");
            (void)builder_.Read(motion_pass,
                                build_result_.scene_depth,
                                render_graph::AccessDesc{
                                    .access =
                                        render_graph::AccessKind::shader_sample_read,
                                });
            current_motion_version = builder_.Write(
                motion_pass,
                current_motion.resource,
                render_graph::AccessDesc{
                    .access = render_graph::AccessKind::color_attachment_write,
                });
            builder_.SetRasterPassDesc(
                motion_pass,
                render_graph::RasterPassDesc{
                    .color_attachments = {
                        render_graph::RasterColorAttachmentDesc{
                            .target = current_motion.resource,
                            .load_op = render_graph::AttachmentLoadOp::dont_care,
                            .store_op = render_graph::AttachmentStoreOp::store,
                        },
                    },
                });
            temporal_motion_renderer->DescribeGraphDescriptorBindings(builder_,
                                                                      motion_pass);
            const auto current_clip_to_previous_clip =
                snapshot_.temporal.reprojection.current_clip_to_previous_clip;
            const bool has_previous_reprojection =
                snapshot_.temporal.reprojection.previous_available;
            const float current_jitter_uv_x =
                snapshot_.temporal.jitter.current_uv_x;
            const float current_jitter_uv_y =
                snapshot_.temporal.jitter.current_uv_y;
            const float previous_jitter_uv_x =
                snapshot_.temporal.jitter.previous_uv_x;
            const float previous_jitter_uv_y =
                snapshot_.temporal.jitter.previous_uv_y;
            builder_.SetExecuteCallback(
                motion_pass,
                [this,
                 current_clip_to_previous_clip,
                 current_jitter_uv_x,
                 current_jitter_uv_y,
                 previous_jitter_uv_x,
                 previous_jitter_uv_y,
                 has_previous_reprojection,
                 depth_source = build_result_.scene_depth,
                 output_target = current_motion.resource](render_graph::GraphCommandContext& context_) {
                    temporal_motion_renderer->RecordGraphPass(
                        context_,
                        depth_source,
                        output_target,
                        current_clip_to_previous_clip,
                        current_jitter_uv_x,
                        current_jitter_uv_y,
                        previous_jitter_uv_x,
                        previous_jitter_uv_y,
                        has_previous_reprojection);
                });
            graph_runtime_service->QueueFrameMotionHistoryPublish(
                snapshot_.frame_index,
                snapshot_.submission_id);
        }
    }

    std::vector<const SceneRendererEntry*> temporal_motion_overlay_entries{};
    render::SceneTemporalMotionProducerState temporal_motion_producer_state{};
    if (current_motion.available &&
        IsValidResourceVersionHandle(current_motion_version) &&
        IsValidResourceHandle(build_result_.scene_depth)) {
        temporal_motion_producer_state.current_clip_to_previous_clip =
            snapshot_.temporal.reprojection.current_clip_to_previous_clip;
        temporal_motion_producer_state.current_jitter_uv_x =
            snapshot_.temporal.jitter.current_available
                ? snapshot_.temporal.jitter.current_uv_x
                : 0.0F;
        temporal_motion_producer_state.current_jitter_uv_y =
            snapshot_.temporal.jitter.current_available
                ? snapshot_.temporal.jitter.current_uv_y
                : 0.0F;
        temporal_motion_producer_state.previous_jitter_uv_x =
            snapshot_.temporal.jitter.previous_available
                ? snapshot_.temporal.jitter.previous_uv_x
                : 0.0F;
        temporal_motion_producer_state.previous_jitter_uv_y =
            snapshot_.temporal.jitter.previous_available
                ? snapshot_.temporal.jitter.previous_uv_y
                : 0.0F;
        temporal_motion_producer_state.previous_submission_id =
            snapshot_.temporal.motion.previous_available
                ? snapshot_.temporal.motion.previous_submission_id
                : snapshot_.temporal.reprojection.previous_submission_id;
        temporal_motion_producer_state.previous_frame_index =
            snapshot_.temporal.motion.previous_available
                ? snapshot_.temporal.motion.previous_frame_index
                : snapshot_.temporal.reprojection.previous_frame_index;
        temporal_motion_producer_state.invalidation_reason =
            snapshot_.temporal.motion.invalidation_reason !=
                    render_graph::FrameHistoryInvalidationReason::none
                ? snapshot_.temporal.motion.invalidation_reason
                : (snapshot_.temporal.reprojection.invalidation_reason !=
                           render_graph::FrameHistoryInvalidationReason::none
                       ? snapshot_.temporal.reprojection.invalidation_reason
                       : snapshot_.temporal.jitter.invalidation_reason);
        temporal_motion_producer_state.previous_available =
            snapshot_.temporal.motion.previous_available &&
            snapshot_.temporal.reprojection.current_available &&
            snapshot_.temporal.reprojection.previous_available &&
            snapshot_.temporal.jitter.current_available &&
            snapshot_.temporal.jitter.previous_available &&
            temporal_motion_producer_state.invalidation_reason ==
                render_graph::FrameHistoryInvalidationReason::none;
        for (const SceneRendererEntry& entry_ : scene_renderer_entries) {
            if (!IsLayerVisibleForSubmission(entry_.submission_layer_mask) ||
                entry_.renderer == nullptr ||
                entry_.graph_temporal_motion_record_fn == nullptr ||
                !IsFirstSceneRendererEntryForRenderer(entry_)) {
                continue;
            }
            if (entry_.configure_temporal_motion_producer_state_fn != nullptr) {
                entry_.configure_temporal_motion_producer_state_fn(
                    entry_.renderer,
                    &temporal_motion_producer_state);
            }
            temporal_motion_overlay_entries.push_back(&entry_);
        }
    }
    if (!temporal_motion_overlay_entries.empty()) {
        const auto temporal_motion_overlay_pass =
            builder_.AddPass("temporal_object_motion_overlay_pass");
        (void)builder_.Read(
            temporal_motion_overlay_pass,
            current_motion_version,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::color_attachment_read,
            });
        (void)builder_.Read(
            temporal_motion_overlay_pass,
            build_result_.scene_depth,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::depth_stencil_read,
            });
        current_motion_version = builder_.Write(
            temporal_motion_overlay_pass,
            current_motion.resource,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::color_attachment_write,
            });
        builder_.SetRasterPassDesc(
            temporal_motion_overlay_pass,
            render_graph::RasterPassDesc{
                .color_attachments = {
                    render_graph::RasterColorAttachmentDesc{
                        .target = current_motion.resource,
                        .load_op = render_graph::AttachmentLoadOp::load,
                        .store_op = render_graph::AttachmentStoreOp::store,
                    },
                },
                .has_depth_attachment = true,
                .depth_attachment = render_graph::RasterDepthAttachmentDesc{
                    .target = build_result_.scene_depth,
                    .load_op = render_graph::AttachmentLoadOp::load,
                    .store_op = render_graph::AttachmentStoreOp::store,
                    .stencil_load_op =
                        render_graph::AttachmentLoadOp::dont_care,
                    .stencil_store_op =
                        render_graph::AttachmentStoreOp::dont_care,
                    .clear_value = {.depth = 1.0F, .stencil = 0U},
                    .read_only = true,
                },
            });
        for (const auto* entry_ : temporal_motion_overlay_entries) {
            if (entry_->describe_graph_temporal_motion_bindings_fn != nullptr) {
                entry_->describe_graph_temporal_motion_bindings_fn(
                    entry_->renderer,
                    builder_,
                    temporal_motion_overlay_pass);
            }
            if (graph_runtime_service != nullptr &&
                entry_->register_graph_imported_resources_fn != nullptr) {
                entry_->register_graph_imported_resources_fn(entry_->renderer,
                                                             *graph_runtime_service);
            }
        }
        builder_.SetExecuteCallback(
            temporal_motion_overlay_pass,
            [temporal_motion_overlay_entries,
             motion_target = current_motion.resource,
             depth_target = build_result_.scene_depth](
                render_graph::GraphCommandContext& context_) {
                for (const auto* entry_ : temporal_motion_overlay_entries) {
                    if (entry_->graph_temporal_motion_record_fn == nullptr) {
                        continue;
                    }
                    entry_->graph_temporal_motion_record_fn(entry_->renderer,
                                                            context_,
                                                            motion_target,
                                                            depth_target);
                }
            });
    }

    const bool temporal_resolve_enabled =
        temporal_resolve_requested &&
        IsValidResourceVersionHandle(color_chain_) &&
        IsValidResourceHandle(build_result_.present_target) &&
        IsValidResourceVersionHandle(current_motion_version) &&
        current_motion.available;
    if (temporal_resolve_enabled) {
        const auto temporal_readiness =
            render_graph::EvaluateTemporalConsumerAvailability(
                snapshot_.temporal,
                render_graph::TemporalConsumerRequirements{
                    .requires_previous_color = true,
                    .requires_previous_depth = true,
                    .requires_previous_motion = false,
                    .requires_current_motion = true,
                    .requires_temporal_jitter = true,
                });
        const auto previous_color =
            render_graph::ImportPreviousTemporalSurface(
                builder_,
                "temporal_previous_color",
                snapshot_.temporal.color,
                register_imported_texture);
        const auto previous_depth =
            render_graph::ImportPreviousTemporalSurface(
                builder_,
                "temporal_previous_depth",
                snapshot_.temporal.depth,
                register_imported_texture);
        const auto current_source =
            render_graph::detail::ResourceHandleFromVersion(color_chain_);
        const auto previous_source = previous_color.available
            ? previous_color.resource
            : current_source;
        const auto previous_depth_source = previous_depth.available
            ? previous_depth.resource
            : build_result_.scene_depth;
        const auto temporal_output_target = graph_overlay_present
            ? builder_.CreateTexture("temporal_history_resolve_color",
                                     BuildTemporalResolveIntermediateDesc(snapshot_),
                                     render_graph::ResourceLifetime::transient)
            : build_result_.present_target;
        const auto temporal_pass = builder_.AddPass("temporal_history_resolve_stub");
        (void)builder_.Read(temporal_pass,
                            color_chain_,
                            render_graph::AccessDesc{
                                .access =
                                    render_graph::AccessKind::shader_sample_read,
                            });
        if (previous_color.available) {
            (void)builder_.Read(temporal_pass,
                                previous_color.resource,
                                render_graph::AccessDesc{
                                    .access =
                                        render_graph::AccessKind::shader_sample_read,
                                });
        }
        if (previous_depth.available) {
            (void)builder_.Read(temporal_pass,
                                previous_depth.resource,
                                render_graph::AccessDesc{
                                    .access =
                                        render_graph::AccessKind::shader_sample_read,
                                });
        }
        const auto motion_source =
            render_graph::detail::ResourceHandleFromVersion(current_motion_version);
        (void)builder_.Read(temporal_pass,
                            current_motion_version,
                            render_graph::AccessDesc{
                                .access =
                                    render_graph::AccessKind::shader_sample_read,
                            });
        color_chain_ = builder_.Write(temporal_pass,
                                      temporal_output_target,
                                      render_graph::AccessDesc{
                                          .access =
                                              render_graph::AccessKind::color_attachment_write,
                                      });
        builder_.SetRasterPassDesc(temporal_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = temporal_output_target,
                                               .load_op =
                                                   render_graph::AttachmentLoadOp::dont_care,
                                               .store_op =
                                                   render_graph::AttachmentStoreOp::store,
                                           },
                                       },
                                   });
        temporal_resolve_renderer->DescribeGraphDescriptorBindings(builder_,
                                                                   temporal_pass);
        const float current_weight = temporal_readiness.ready
            ? k_temporal_resolve_current_weight
            : 1.0F;
        const float previous_weight = temporal_readiness.ready
            ? k_temporal_resolve_previous_weight
            : 0.0F;
        builder_.SetExecuteCallback(
            temporal_pass,
            [this,
             current_source,
             previous_source,
             previous_depth_source,
             motion_source,
             temporal_output_target,
             current_weight,
             previous_weight,
             motion_rejection_begin_pixels =
                 k_temporal_resolve_rejection_begin_pixels,
             motion_rejection_end_pixels =
                 k_temporal_resolve_rejection_end_pixels,
             depth_rejection_begin =
                 k_temporal_resolve_depth_rejection_begin,
             depth_rejection_end =
                 k_temporal_resolve_depth_rejection_end,
             reproject_history = temporal_readiness.ready](render_graph::GraphCommandContext& context_) {
                temporal_resolve_renderer->RecordGraphPass(
                    context_,
                    current_source,
                    previous_source,
                    previous_depth_source,
                    motion_source,
                    temporal_output_target,
                    current_weight,
                    previous_weight,
                    motion_rejection_begin_pixels,
                    motion_rejection_end_pixels,
                    depth_rejection_begin,
                    depth_rejection_end,
                    reproject_history);
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
