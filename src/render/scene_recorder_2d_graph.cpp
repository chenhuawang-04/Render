#include "vr/render/scene_recorder_2d.hpp"

#include <stdexcept>
#include <vector>

namespace vr::render {
namespace {

[[nodiscard]] render_graph::TextureDesc BuildSceneConsumerOverlayIntermediateDesc(
    const render_graph::FrameSnapshot2D& snapshot_) noexcept {
    return render_graph::TextureDesc{
        .dimension = render_graph::TextureDimension::image_2d,
        .format = render_graph::TextureFormat::r8g8b8a8_unorm,
        .extent = snapshot_.reference_extent,
        .usage = render_graph::texture_usage_color_attachment_flag |
                 render_graph::texture_usage_transfer_src_flag,
        .mip_level_count = 1U,
        .array_layer_count = 1U,
        .sample_count = render_graph::SampleCount::x1,
        .allow_alias = true,
    };
}

} // namespace

void SceneRecorder2D::BuildRenderGraph(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::FrameSnapshot2D& snapshot_,
    const render_graph::MinimalFrameGraphBuildResult<ecs::Dim2>& build_result_,
    render_graph::ResourceVersionHandle& color_chain_) {
    if (!initialized) {
        return;
    }

    const bool shadow_enabled = IsShadowEnabledForSubmission();
    std::vector<const PreSceneRendererEntry*> visible_pre_scene_entries{};
    visible_pre_scene_entries.reserve(pre_scene_renderer_entries.size());
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
        visible_pre_scene_entries.push_back(&entry);
    }

    const bool scene_consumer_enabled =
        scene_consumer_entry.renderer != nullptr &&
        IsLayerVisibleForSubmission(scene_consumer_entry.submission_layer_mask) &&
        IsPostProcessEnabledForSubmission();
    if (scene_consumer_enabled &&
        (scene_consumer_entry.graph_record_fn == nullptr ||
         scene_consumer_entry.build_graph_color_attachment_fn == nullptr)) {
        throw std::runtime_error(
            "SceneRecorder2D::BuildRenderGraph requires graph-capable scene consumer registration");
    }

    std::vector<const SceneRendererEntry*> visible_scene_entries{};
    visible_scene_entries.reserve(scene_renderer_entries.size());
    for (const SceneRendererEntry& entry : scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask) ||
            entry.renderer == nullptr) {
            continue;
        }
        if (entry.graph_record_fn == nullptr) {
            throw std::runtime_error("SceneRecorder2D::BuildRenderGraph requires graph-capable scene renderer registration");
        }
        visible_scene_entries.push_back(&entry);
    }

    const bool has_background_pass = HasBackgroundPassForSubmission() &&
                                     scene_view != nullptr &&
                                     frame_packet != nullptr;
    if ((has_background_pass || !visible_scene_entries.empty()) &&
        !IsValidPassHandle(build_result_.scene_pass)) {
        throw std::runtime_error(
            "SceneRecorder2D::BuildRenderGraph requires a valid scene pass for visible scene content");
    }

    if (IsValidPassHandle(build_result_.scene_pass)) {
        builder_.SetRasterPassDesc(build_result_.scene_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = build_result_.scene_color,
                                               .load_op = render_graph::AttachmentLoadOp::clear,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                               .clear_value = {
                                                   .red = create_info_cache.scene_target.clear_color.float32[0],
                                                   .green = create_info_cache.scene_target.clear_color.float32[1],
                                                   .blue = create_info_cache.scene_target.clear_color.float32[2],
                                                   .alpha = create_info_cache.scene_target.clear_color.float32[3],
                                               },
                                           },
                                       },
                                   });

        for (const auto* entry : visible_scene_entries) {
            if (entry->describe_graph_bindings_fn != nullptr) {
                entry->describe_graph_bindings_fn(entry->renderer,
                                                  builder_,
                                                  build_result_.scene_pass);
            }
            if (graph_runtime_service != nullptr &&
                entry->register_graph_imported_resources_fn != nullptr) {
                entry->register_graph_imported_resources_fn(entry->renderer,
                                                            *graph_runtime_service);
            }
        }

        if (has_background_pass || !visible_scene_entries.empty()) {
            const RenderView2D captured_view = has_background_pass ? *scene_view : RenderView2D{};
            const scene::Background2DRenderState captured_background =
                has_background_pass ? frame_packet->Payload().background
                                    : scene::Background2DRenderState{};
            const auto scene_color = build_result_.scene_color;
            builder_.SetExecuteCallback(
                build_result_.scene_pass,
                [this,
                 has_background_pass,
                 captured_view,
                 captured_background,
                 visible_scene_entries,
                 scene_color](render_graph::GraphCommandContext& context_) {
                    if (has_background_pass) {
                        background_pass.RecordGraphPass(context_,
                                                        captured_view,
                                                        captured_background,
                                                        scene_color);
                    }
                    for (const auto* entry : visible_scene_entries) {
                        entry->graph_record_fn(entry->renderer, context_, scene_color);
                    }
                });
        }
    }

    for (const auto* entry : visible_pre_scene_entries) {
        const auto pre_scene_pass = builder_.AddPass(
            entry->kind == PreSceneRendererKind::shadow ? "shadow_prepass" : "pre_scene_pass",
            true);
        if (IsValidPassHandle(build_result_.scene_pass)) {
            builder_.AddDependency(build_result_.scene_pass, pre_scene_pass);
        }
        builder_.SetExecuteCallback(
            pre_scene_pass,
            [entry](render_graph::GraphCommandContext& context_) {
                FrameRecordContext record_context{};
                record_context.frame_index = context_.FrameIndex();
                record_context.command_buffer = context_.CommandBuffer();
                record_context.render_target_host = &context_.RenderTargets();
                entry->record_fn(entry->renderer, record_context);
            });
    }

    const bool graph_overlay_present = IsValidPassHandle(build_result_.overlay_pass) &&
                                       IsValidResourceHandle(build_result_.scene_color);
    if (scene_consumer_enabled) {
        if (!IsValidResourceHandle(build_result_.scene_color) ||
            !IsValidResourceHandle(build_result_.present_target) ||
            !IsValidResourceVersionHandle(color_chain_)) {
            throw std::runtime_error(
                "SceneRecorder2D::BuildRenderGraph requires a valid scene color chain and present target for scene consumers");
        }

        const auto scene_consumer_output = graph_overlay_present
            ? builder_.CreateTexture("scene_consumer_color",
                                     BuildSceneConsumerOverlayIntermediateDesc(snapshot_),
                                     render_graph::ResourceLifetime::transient)
            : build_result_.present_target;
        const auto scene_consumer_pass = builder_.AddPass("scene_consumer_pass");
        (void)builder_.Read(scene_consumer_pass,
                            color_chain_,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
        color_chain_ = builder_.Write(scene_consumer_pass,
                                      scene_consumer_output,
                                      render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
        builder_.SetRasterPassDesc(scene_consumer_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           scene_consumer_entry.build_graph_color_attachment_fn(
                                               scene_consumer_entry.renderer,
                                               scene_consumer_output,
                                               false),
                                       },
                                   });
        if (scene_consumer_entry.describe_graph_bindings_fn != nullptr) {
            scene_consumer_entry.describe_graph_bindings_fn(scene_consumer_entry.renderer,
                                                            builder_,
                                                            scene_consumer_pass);
        }
        if (graph_runtime_service != nullptr &&
            scene_consumer_entry.register_graph_imported_resources_fn != nullptr) {
            scene_consumer_entry.register_graph_imported_resources_fn(
                scene_consumer_entry.renderer,
                *graph_runtime_service);
        }
        const auto scene_source = build_result_.scene_color;
        builder_.SetExecuteCallback(scene_consumer_pass,
                                    [this, scene_source, scene_consumer_output](render_graph::GraphCommandContext& context_) {
                                        scene_consumer_entry.graph_record_fn(scene_consumer_entry.renderer,
                                                                             context_,
                                                                             scene_source,
                                                                             scene_consumer_output);
                                    });

        if (graph_overlay_present) {
            const auto scene_consumer_copyback_pass = builder_.AddPass("scene_consumer_copyback");
            (void)builder_.Read(scene_consumer_copyback_pass,
                                color_chain_,
                                render_graph::AccessDesc{.access = render_graph::AccessKind::transfer_read});
            color_chain_ = builder_.Write(scene_consumer_copyback_pass,
                                          build_result_.scene_color,
                                          render_graph::AccessDesc{.access = render_graph::AccessKind::transfer_write});
            builder_.SetExecuteCallback(
                scene_consumer_copyback_pass,
                [scene_consumer_output, scene_color = build_result_.scene_color](render_graph::GraphCommandContext& context_) {
                    render_graph::detail::RecordTextureCopyOrBlit(context_,
                                                                  scene_consumer_output,
                                                                  scene_color);
                });
        }
    }

    std::vector<const OverlayRendererEntry*> visible_overlay_entries{};
    visible_overlay_entries.reserve(overlay_renderer_entries.size());
    for (const OverlayRendererEntry& entry : overlay_renderer_entries) {
        if (!IsOverlayLayerVisibleForSubmission(entry.submission_layer_mask) ||
            entry.renderer == nullptr) {
            continue;
        }
        if (entry.graph_record_fn == nullptr) {
            throw std::runtime_error("SceneRecorder2D::BuildRenderGraph requires graph-capable overlay renderer registration");
        }
        visible_overlay_entries.push_back(&entry);
    }

    if (!visible_overlay_entries.empty() &&
        !IsValidPassHandle(build_result_.overlay_pass)) {
        throw std::runtime_error(
            "SceneRecorder2D::BuildRenderGraph requires a dedicated overlay pass for visible overlay renderers");
    }

    if (!visible_overlay_entries.empty() &&
        IsValidPassHandle(build_result_.overlay_pass)) {
        for (const auto* entry : visible_overlay_entries) {
            if (entry->describe_graph_bindings_fn != nullptr) {
                entry->describe_graph_bindings_fn(entry->renderer,
                                                  builder_,
                                                  build_result_.overlay_pass);
            }
            if (graph_runtime_service != nullptr &&
                entry->register_graph_imported_resources_fn != nullptr) {
                entry->register_graph_imported_resources_fn(entry->renderer,
                                                            *graph_runtime_service);
            }
        }
        const auto overlay_target = build_result_.scene_color;
        builder_.SetExecuteCallback(
            build_result_.overlay_pass,
            [visible_overlay_entries, overlay_target](render_graph::GraphCommandContext& context_) {
                for (const auto* entry : visible_overlay_entries) {
                    entry->graph_record_fn(entry->renderer, context_, overlay_target);
                }
            });
    }
}


} // namespace vr::render
