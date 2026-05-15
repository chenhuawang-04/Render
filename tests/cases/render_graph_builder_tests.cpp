#include "support/test_framework.hpp"
#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <array>
#include <string_view>

namespace {

template<typename FnT>
[[nodiscard]] bool ThrowsAnyException(FnT&& function_) {
    try {
        function_();
    } catch (...) {
        return true;
    }
    return false;
}

VR_TEST_CASE(RenderGraphBuilder_tracks_linear_resource_versioning,
             "unit;core;render_graph") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 1920U, .height = 1080U, .depth = 1U},
        });

    const auto prepare = builder.AddPass("prepare_visibility");
    const auto shade = builder.AddPass("shade_scene");
    const auto present = builder.AddPass("present_scene", true);

    const auto version0 = builder.Read(prepare, scene_color);
    const auto version1 = builder.Write(shade, scene_color);
    const auto present_input = builder.Read(present, scene_color);

    const auto compiled = builder.Compile();

    VR_CHECK(version0.version == 0U);
    VR_CHECK(version1.version == 1U);
    VR_CHECK(present_input.version == 1U);
    VR_REQUIRE(compiled.ExecutionOrder().size() == 3U);
    VR_CHECK(compiled.ExecutionOrder()[0].index == prepare.index);
    VR_CHECK(compiled.ExecutionOrder()[1].index == shade.index);
    VR_CHECK(compiled.ExecutionOrder()[2].index == present.index);
    VR_REQUIRE(compiled.LivenessRanges().size() == 2U);
    VR_CHECK(compiled.LivenessRanges()[0].version.version == 0U);
    VR_CHECK(compiled.LivenessRanges()[0].first_pass_order == 0U);
    VR_CHECK(compiled.LivenessRanges()[0].last_pass_order == 1U);
    VR_CHECK(compiled.LivenessRanges()[1].version.version == 1U);
    VR_CHECK(compiled.LivenessRanges()[1].first_pass_order == 1U);
    VR_CHECK(compiled.LivenessRanges()[1].last_pass_order == 2U);
}

VR_TEST_CASE(RenderGraphBuilder_culls_unrooted_work,
             "unit;core;render_graph") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scratch = builder.CreateBuffer(
        "scratch_buffer",
        vr::render_graph::BufferDesc{.size_bytes = 4096U});
    const auto orphan = builder.AddPass("orphan_pass");

    (void)builder.Write(orphan, scratch);

    const auto compiled = builder.Compile();
    VR_CHECK(compiled.Empty());
    VR_CHECK(compiled.Passes().empty());
    VR_CHECK(compiled.LivenessRanges().empty());
}

VR_TEST_CASE(RenderGraphBuilder_rejects_non_latest_version_writes,
             "unit;core;render_graph") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto history = builder.CreateTexture(
        "history",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
        });
    const auto writer0 = builder.AddPass("writer0");
    const auto writer1 = builder.AddPass("writer1", true);

    const auto version1 = builder.Write(writer0, history);
    VR_CHECK(version1.version == 1U);
    VR_CHECK(ThrowsAnyException([&]() {
        (void)builder.Write(writer1,
                            vr::render_graph::ResourceVersionHandle{
                                .resource_index = history.index,
                                .version = 0U,
                            });
    }));
}

VR_TEST_CASE(RenderGraphRuntimeService_is_registered_in_runtime_profile,
             "unit;core;render_graph;runtime") {
    using Runtime = vr::runtime::Runtime<>;
    using RenderGraphRuntimeService = vr::runtime::services::RenderGraphRuntimeService;

    static_assert(Runtime::RuntimeServicesType::Contains<RenderGraphRuntimeService>());

    Runtime runtime{};
    auto& services = runtime.Services();
    VR_CHECK(services.TryGet<RenderGraphRuntimeService>() != nullptr);
}

VR_TEST_CASE(FrameSnapshot3D_copies_packet_metadata_and_stable_view_state,
             "unit;core;render_graph;snapshot") {
    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 8.0F, .origin_y = 4.0F, .width = 640.0F, .height = 360.0F};
    camera.runtime.culling_mask = 0x1234U;
    camera.runtime.revision = 7U;

    vr::ecs::Transform<vr::ecs::Dim3> camera_transform{};
    camera_transform.runtime.world_revision = 11U;

    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        &camera_transform,
        vr::render::RenderViewKind::world,
        3U);
    view.layer_mask = 0x00FF00FFU;
    view.debug_flags = vr::render::render_view_debug_wireframe_flag;
    view.targets.color_final_state = vr::render::RenderTargetStateKind::present_src;
    view.background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    view.background_override.state.revision = 42U;
    view.background_override.gpu.index = 9U;
    view.background_override.gpu.generation = 2U;
    vr::render::RefreshRenderViewSignature(view);

    auto packet = vr::render::MakeSingleViewScenePacket(view, 99U);
    packet.debug_flags = view.debug_flags;
    packet.extra.environment = view.background_override.state;
    packet.extra.environment_gpu = view.background_override.gpu;
    packet.extra.ibl_environment_id = 5U;
    vr::render::RefreshRenderScenePacketSignature(packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(
        packet,
        17U,
        vr::render_graph::Extent3D{.width = 1920U, .height = 1080U, .depth = 1U});
    const auto* active_view = snapshot.ActiveView();

    VR_REQUIRE(active_view != nullptr);
    VR_CHECK(snapshot.frame_index == 17U);
    VR_CHECK(snapshot.reference_extent.width == 1920U);
    VR_CHECK(snapshot.reference_extent.height == 1080U);
    VR_CHECK(snapshot.kind == vr::render::RenderScenePacketKind::world);
    VR_CHECK(snapshot.selection.active_view_index == 0U);
    VR_CHECK(snapshot.selection.scene_view_index == 0U);
    VR_CHECK(snapshot.selection.overlay_view_index == vr::render::invalid_scene_view_index);
    VR_CHECK(snapshot.submission_id == 99U);
    VR_CHECK(snapshot.ViewCount() == 1U);
    VR_CHECK(snapshot.debug_flags == vr::render::render_view_debug_wireframe_flag);
    VR_CHECK(snapshot.extra.environment.revision == 42U);
    VR_CHECK(snapshot.extra.environment_gpu.index == 9U);
    VR_CHECK(snapshot.extra.ibl_environment_id == 5U);
    VR_CHECK(active_view->view_index == 3U);
    VR_CHECK(active_view->has_camera == 1U);
    VR_CHECK(active_view->has_camera_transform == 1U);
    VR_CHECK(active_view->camera.runtime.culling_mask == 0x1234U);
    VR_CHECK(active_view->camera.runtime.revision == 7U);
    VR_CHECK(active_view->camera_transform_world_revision == 11U);
    VR_CHECK(active_view->targets.color_final_state == vr::render::RenderTargetStateKind::present_src);
    VR_CHECK(active_view->background_override.gpu.index == 9U);
}

VR_TEST_CASE(FrameSnapshot2D_resolves_scene_and_overlay_view_selection,
             "unit;core;render_graph;snapshot") {
    vr::ecs::Camera<vr::ecs::Dim2> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 800.0F, .height = 600.0F};
    world_camera.runtime.culling_mask = 0x1U;
    world_camera.runtime.revision = 5U;

    vr::ecs::Camera<vr::ecs::Dim2> ui_camera = world_camera;
    ui_camera.runtime.revision = 6U;

    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto ui_view = vr::render::MakeRenderViewFromCamera(
        ui_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);

    const std::array views{world_view, ui_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        1U,
        123U,
        vr::render::RenderScenePacketKind::mixed);
    const auto snapshot = vr::render_graph::MakeFrameSnapshot(
        packet,
        88U,
        vr::render_graph::Extent3D{.width = 800U, .height = 600U, .depth = 1U});

    VR_CHECK(snapshot.selection.active_view_index == 1U);
    VR_CHECK(snapshot.selection.scene_view_index == 0U);
    VR_CHECK(snapshot.selection.overlay_view_index == 1U);
    VR_REQUIRE(snapshot.SceneView() != nullptr);
    VR_REQUIRE(snapshot.OverlayView() != nullptr);
    VR_CHECK(snapshot.SceneView()->kind == vr::render::RenderViewKind::world);
    VR_CHECK(snapshot.OverlayView()->kind == vr::render::RenderViewKind::ui);
    VR_CHECK(snapshot.reference_extent.width == 800U);
    VR_CHECK(snapshot.reference_extent.height == 600U);
}

VR_TEST_CASE(FrameSnapshot3D_signature_ignores_pointer_identity,
             "unit;core;render_graph;snapshot") {
    vr::ecs::Camera<vr::ecs::Dim3> camera_a{};
    camera_a.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 200.0F};
    camera_a.runtime.culling_mask = 0xAA55U;
    camera_a.runtime.revision = 21U;

    vr::ecs::Camera<vr::ecs::Dim3> camera_b = camera_a;

    vr::ecs::Transform<vr::ecs::Dim3> transform_a{};
    transform_a.runtime.world_revision = 44U;
    vr::ecs::Transform<vr::ecs::Dim3> transform_b = transform_a;

    auto view_a = vr::render::MakeRenderViewFromCamera(
        camera_a,
        &transform_a,
        vr::render::RenderViewKind::world,
        1U);
    auto view_b = vr::render::MakeRenderViewFromCamera(
        camera_b,
        &transform_b,
        vr::render::RenderViewKind::world,
        1U);

    view_a.background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    view_b.background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    view_a.background_override.state.revision = 8U;
    view_b.background_override.state.revision = 8U;
    view_a.background_override.gpu.index = 6U;
    view_b.background_override.gpu.index = 6U;
    view_a.background_override.gpu.generation = 1U;
    view_b.background_override.gpu.generation = 1U;
    vr::render::RefreshRenderViewSignature(view_a);
    vr::render::RefreshRenderViewSignature(view_b);

    VR_CHECK(view_a.signature != view_b.signature);

    const auto frame_view_a = vr::render_graph::MakeFrameViewSnapshot(view_a);
    const auto frame_view_b = vr::render_graph::MakeFrameViewSnapshot(view_b);
    VR_CHECK(frame_view_a.signature == frame_view_b.signature);

    auto packet_a = vr::render::MakeSingleViewScenePacket(view_a, 100U);
    auto packet_b = vr::render::MakeSingleViewScenePacket(view_b, 200U);
    packet_a.extra.environment = view_a.background_override.state;
    packet_b.extra.environment = view_b.background_override.state;
    packet_a.extra.environment_gpu = view_a.background_override.gpu;
    packet_b.extra.environment_gpu = view_b.background_override.gpu;
    packet_a.extra.ibl_environment_id = 3U;
    packet_b.extra.ibl_environment_id = 3U;

    const auto snapshot_a = vr::render_graph::MakeFrameSnapshot(packet_a, 1U);
    const auto snapshot_b = vr::render_graph::MakeFrameSnapshot(packet_b, 2U);
    VR_CHECK(snapshot_a.signature == snapshot_b.signature);
}

VR_TEST_CASE(RenderGraphRuntimeService_resets_frame_local_state_on_begin_frame,
             "unit;core;render_graph;runtime") {
    vr::runtime::services::RenderGraphRuntimeService service{};
    const auto scratch = service.Builder().CreateBuffer(
        "scratch",
        vr::render_graph::BufferDesc{.size_bytes = 64U});
    const auto writer = service.Builder().AddPass("writer", true);
    (void)service.Builder().Write(writer, scratch);
    service.SetCompiledGraph(service.Builder().Compile());

    vr::render_graph::FrameSnapshot3D snapshot{};
    snapshot.views.push_back(vr::render_graph::FrameViewSnapshot3D{});
    vr::render_graph::RefreshFrameSnapshotSignature(snapshot);
    service.SetFrameSnapshot<vr::ecs::Dim3>(snapshot);

    VR_REQUIRE(service.TryGetCompiledGraph() != nullptr);
    VR_REQUIRE(service.TryGetFrameSnapshot<vr::ecs::Dim3>() != nullptr);

    service.BeginFrame(7U);

    VR_CHECK(service.FrameIndex() == 7U);
    VR_CHECK(service.Builder().ResourceCount() == 0U);
    VR_CHECK(service.Builder().PassCount() == 0U);
    VR_CHECK(service.TryGetCompiledGraph() == nullptr);
    VR_CHECK(!service.HasFrameSnapshot());
}

VR_TEST_CASE(RenderGraphRuntimeService_tracks_dimensioned_snapshots,
             "unit;core;render_graph;runtime") {
    vr::runtime::services::RenderGraphRuntimeService service{};

    vr::render_graph::FrameSnapshot2D snapshot2d{};
    snapshot2d.views.push_back(vr::render_graph::FrameViewSnapshot2D{});
    vr::render_graph::RefreshFrameSnapshotSignature(snapshot2d);
    service.SetFrameSnapshot<vr::ecs::Dim2>(snapshot2d);

    VR_REQUIRE(service.TryGetFrameSnapshot<vr::ecs::Dim2>() != nullptr);
    VR_CHECK(service.TryGetFrameSnapshot<vr::ecs::Dim3>() == nullptr);

    vr::render_graph::FrameSnapshot3D snapshot3d{};
    snapshot3d.views.push_back(vr::render_graph::FrameViewSnapshot3D{});
    vr::render_graph::RefreshFrameSnapshotSignature(snapshot3d);
    service.SetFrameSnapshot<vr::ecs::Dim3>(snapshot3d);

    VR_REQUIRE(service.TryGetFrameSnapshot<vr::ecs::Dim3>() != nullptr);
    VR_CHECK(service.TryGetFrameSnapshot<vr::ecs::Dim2>() == nullptr);
}

VR_TEST_CASE(RenderGraphRuntimeService_captures_snapshot_during_prepare_tick_frame,
             "unit;core;render_graph;runtime") {
    using Host = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;

    struct FakeRecorder final {
        const vr::render::RenderScenePacket3D* frame_packet = nullptr;

        void PrepareFrame(const vr::render::SceneRecorder3DPrepareView&) noexcept {}

        void Record(const vr::render::FrameRecordContext&) noexcept {}

        [[nodiscard]] const vr::render::RenderScenePacket3D* FramePacket() const noexcept {
            return frame_packet;
        }
    };

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 128.0F, .height = 72.0F};
    camera.runtime.culling_mask = 0x55AAU;
    camera.runtime.revision = 12U;

    vr::ecs::Transform<vr::ecs::Dim3> transform{};
    transform.runtime.world_revision = 34U;

    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        &transform,
        vr::render::RenderViewKind::world,
        2U);
    view.background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    view.background_override.state.revision = 9U;
    view.background_override.gpu.index = 4U;
    view.background_override.gpu.generation = 1U;

    auto packet = vr::render::MakeSingleViewScenePacket(view, 333U);
    packet.extra.environment = view.background_override.state;
    packet.extra.environment_gpu = view.background_override.gpu;
    packet.extra.ibl_environment_id = 7U;

    Host host{};
    FakeRecorder recorder{};
    recorder.frame_packet = &packet;

    host.PrepareTickFrame(recorder, 5U);

    auto& service = host.Services().Get<vr::runtime::services::RenderGraphRuntimeService>();
    const auto* snapshot = service.TryGetFrameSnapshot<vr::ecs::Dim3>();
    VR_REQUIRE(snapshot != nullptr);
    VR_CHECK(snapshot->frame_index == 5U);
    VR_CHECK(snapshot->reference_extent.width == 128U);
    VR_CHECK(snapshot->reference_extent.height == 72U);
    VR_CHECK(snapshot->submission_id == 333U);
    VR_CHECK(snapshot->ViewCount() == 1U);
    VR_CHECK(snapshot->extra.ibl_environment_id == 7U);
    VR_REQUIRE(snapshot->ActiveView() != nullptr);
    VR_CHECK(snapshot->ActiveView()->camera.runtime.revision == 12U);
    VR_CHECK(snapshot->ActiveView()->camera_transform_world_revision == 34U);
}

VR_TEST_CASE(RenderGraphBuilder_builds_minimal_scene_overlay_present_chain,
             "unit;core;render_graph;runtime") {
    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 640.0F, .height = 360.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x10U;

    vr::ecs::Camera<vr::ecs::Dim3> overlay_camera = world_camera;
    overlay_camera.runtime.revision = 2U;

    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto overlay_view = vr::render::MakeRenderViewFromCamera(
        overlay_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);

    const std::array views{world_view, overlay_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        0U,
        444U,
        vr::render::RenderScenePacketKind::mixed);
    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 9U);

    vr::render_graph::RenderGraphBuilder builder{};
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_CHECK(build_result.has_scene_pass);
    VR_CHECK(build_result.has_overlay_pass);
    VR_CHECK(build_result.has_depth);
    VR_REQUIRE(compiled.ExecutionOrder().size() == 3U);
    VR_REQUIRE(compiled.Passes().size() == 3U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[1].debug_name == "overlay_pass");
    VR_CHECK(compiled.Passes()[2].debug_name == "present_to_swapchain");
}

VR_TEST_CASE(RenderGraphBuilder_exports_dot_and_json,
             "unit;core;render_graph;debug") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 256U, .height = 256U, .depth = 1U},
        });
    const auto main_pass = builder.AddPass("main_scene_pass");
    const auto present_pass = builder.AddPass("present_to_swapchain", true);
    const auto color_version = builder.Write(main_pass, color);
    (void)builder.Read(present_pass, color_version);

    const auto compiled = builder.Compile();
    const std::string dot = compiled.BuildDotGraph();
    const std::string json = compiled.BuildJson();

    VR_CHECK(dot.find("digraph RenderGraph") != std::string::npos);
    VR_CHECK(dot.find("main_scene_pass") != std::string::npos);
    VR_CHECK(dot.find("present_to_swapchain") != std::string::npos);
    VR_CHECK(json.find("\"passes\"") != std::string::npos);
    VR_CHECK(json.find("\"livenessRanges\"") != std::string::npos);
    VR_CHECK(json.find("scene_color#v1") != std::string::npos);
}

} // namespace
