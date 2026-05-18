#include "support/test_framework.hpp"
#include "vr/render/scene_recorder_2d.hpp"

#include <array>
#include <cstdint>

namespace {

[[nodiscard]] vr::render::RenderView2D MakeFullscreenView(const VkExtent2D extent_,
                                                          const vr::render::RenderViewKind kind_,
                                                          const std::uint32_t view_flags_ = vr::render::render_view_overlay_enabled_flag,
                                                          const std::uint32_t layer_mask_ = 0x1U) {
    vr::render::RenderView2D view{};
    view.kind = kind_;
    view.flags = view_flags_;
    view.layer_mask = layer_mask_;
    view.debug_flags = vr::render::render_view_debug_none_flag;
    view.viewport = vr::render::RenderViewViewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>((extent_.width > 0U) ? extent_.width : 1U),
        .height = static_cast<float>((extent_.height > 0U) ? extent_.height : 1U),
        .min_depth = 0.0F,
        .max_depth = 1.0F,
    };
    view.scissor = vr::render::RenderViewScissor{
        .x = 0,
        .y = 0,
        .width = (extent_.width > 0U) ? extent_.width : 1U,
        .height = (extent_.height > 0U) ? extent_.height : 1U,
    };
    vr::render::RefreshRenderViewSignature(view);
    return view;
}

VR_TEST_CASE(Background2DGraph_build_render_graph_routes_solid_background_to_scene_pass,
             "unit;core;render_graph;runtime;background2d") {
    vr::render::SceneRecorder2D recorder{};
    recorder.Initialize();

    const auto world_view = MakeFullscreenView(VkExtent2D{320U, 180U},
                                               vr::render::RenderViewKind::world);
    auto packet = vr::render::MakeSingleViewScenePacket(
        world_view,
        6201U,
        vr::render::RenderScenePacketKind::world);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.extra.background.mode = vr::scene::Background2DMode::solid_color;
    packet.extra.background.color0 = {.x = 0.15F, .y = 0.25F, .z = 0.35F, .w = 1.0F};
    packet.extra.background.opacity = 1.0F;
    vr::render::RefreshRenderScenePacketSignature(packet);
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 27U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    recorder.BuildRenderGraph(builder, snapshot, build_result, color_chain);
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 3U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_REQUIRE(compiled.Passes()[0].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[0].execute));

    recorder.ClearFramePacket();
}

VR_TEST_CASE(Background2DGraph_build_render_graph_routes_gradient_background_to_scene_pass,
             "unit;core;render_graph;runtime;background2d") {
    vr::render::SceneRecorder2D recorder{};
    recorder.Initialize();

    const auto world_view = MakeFullscreenView(VkExtent2D{320U, 180U},
                                               vr::render::RenderViewKind::world);
    auto packet = vr::render::MakeSingleViewScenePacket(
        world_view,
        6203U,
        vr::render::RenderScenePacketKind::world);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.extra.background.mode = vr::scene::Background2DMode::gradient;
    packet.extra.background.color0 = {.x = 0.08F, .y = 0.10F, .z = 0.18F, .w = 1.0F};
    packet.extra.background.color1 = {.x = 0.28F, .y = 0.34F, .z = 0.46F, .w = 1.0F};
    packet.extra.background.opacity = 1.0F;
    vr::render::RefreshRenderScenePacketSignature(packet);
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 29U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    recorder.BuildRenderGraph(builder, snapshot, build_result, color_chain);
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 3U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_REQUIRE(compiled.Passes()[0].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[0].execute));

    recorder.ClearFramePacket();
}

} // namespace
