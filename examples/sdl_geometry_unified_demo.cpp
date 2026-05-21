#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/geometry_mesh_system.hpp"
#include "vr/ecs/system/geometry_path_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/geometry/geometry_appearance_host.hpp"
#include "vr/geometry/geometry_renderer_2d.hpp"
#include "vr/geometry/geometry_renderer_3d.hpp"
#include "vr/geometry/geometry_resource_host.hpp"
#include "vr/geometry/geometry_upload_host.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/runtime/crash_tracer_support.hpp"
#include "vr/render/scene_recorder_3d.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;
using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
using GeometrySystem2D = vr::ecs::GeometrySystem<vr::ecs::Dim2>;
using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
using GeometryPathSystem = vr::ecs::GeometryPathSystem;
using GeometryMeshSystem = vr::ecs::GeometryMeshSystem;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

[[nodiscard]] vr::render::SceneRecorder3DCreateInfo BuildGeometryUnifiedRecorderCreateInfo() noexcept {
    vr::render::SceneRecorder3DCreateInfo create_info{};
    create_info.scene_target.color_debug_name = "GeometryUnifiedSceneColor";
    create_info.scene_target.depth_debug_name = "GeometryUnifiedSceneDepth";
    create_info.scene_target.enable_depth = true;
    create_info.scene_target.color_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.depth_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.clear_color = VkClearColorValue{{0.06F, 0.07F, 0.11F, 1.0F}};
    create_info.bloom.clear_swapchain = true;
    create_info.bloom.clear_color = {{0.02F, 0.025F, 0.04F, 1.0F}};
    create_info.bloom.enable_reinhard_tonemap = true;
    create_info.bloom.exposure = 1.0F;
    create_info.bloom.apply_manual_gamma = false;
    create_info.bloom.bloom_threshold = 0.70F;
    create_info.bloom.bloom_knee = 0.42F;
    create_info.bloom.bloom_intensity = 0.88F;
    create_info.bloom.blur_filter_scale = 1.02F;
    create_info.bloom.downsample_scale = 0.5F;
    create_info.reserve_scene_renderer_count = 1U;
    create_info.reserve_overlay_renderer_count = 1U;
    return create_info;
}

[[nodiscard]] constexpr vr::ecs::AppearanceHandle MakeStaticAppearanceHandle(
    std::uint32_t index_) noexcept {
    return vr::ecs::AppearanceHandle{
        .index = index_,
        .generation = 1U
    };
}

void InitializeGeometry3DComponent(Geometry3D& component_,
                                   Appearance3D& appearance_,
                                   std::uint32_t appearance_index_,
                                   std::uint32_t geometry_id_,
                                   std::uint32_t appearance_id_,
                                   vr::ecs::Rgba8 base_color_,
                                   bool depth_write_,
                                   bool double_sided_) {
    GeometryMeshSystem::Initialize(component_);
    GeometryMeshSystem::SetMeshRoute(component_, geometry_id_, 0U, 0U);
    GeometrySystem3D::SetAuthoringVisualResourceId(component_, appearance_id_);
    GeometrySystem3D::SetDepthBin(component_, 32U);
    GeometryMeshSystem::SetTopology(component_, vr::ecs::Geometry3DTopology::triangles);
    GeometryMeshSystem::SetBounds(component_,
                                  vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                  vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F});

    AppearanceSystem3D::Initialize(appearance_);
    AppearanceSystem3D::SetBaseColor(appearance_, base_color_);
    AppearanceSystem3D::SetDepthTest(appearance_, true);
    AppearanceSystem3D::SetDepthWrite(appearance_, depth_write_);
    AppearanceSystem3D::SetDoubleSided(appearance_, double_sided_);
    AppearanceSystem3D::SetCastShadow(appearance_, true);
    AppearanceSystem3D::SetReceiveShadow(appearance_, true);
    if (base_color_.a < 255U || !depth_write_) {
        AppearanceSystem3D::SetBlendMode(appearance_, vr::ecs::AppearanceBlendMode::alpha);
        AppearanceSystem3D::SetAlphaMode(appearance_, vr::ecs::AppearanceAlphaMode::blend);
    }

    (void)GeometrySystem3D::SetAppearanceRuntimeLink(component_,
                                                     MakeStaticAppearanceHandle(appearance_index_),
                                                     0ULL,
                                                     0ULL,
                                                     appearance_id_,
                                                     &appearance_.style);
}

void ApplyGeometry2DAppearance(Geometry2D& component_,
                               vr::ecs::Rgba8 fill_color_,
                               vr::ecs::Rgba8 stroke_color_,
                               float opacity_,
                               std::int16_t layer_) {
    Appearance2D appearance{};
    AppearanceSystem2D::Initialize(appearance);
    AppearanceSystem2D::SetFillColor(appearance, fill_color_);
    AppearanceSystem2D::SetStrokeColor(appearance, stroke_color_);
    AppearanceSystem2D::SetOpacity(appearance, opacity_);
    AppearanceSystem2D::SetLayer(appearance, layer_);
    (void)GeometrySystem2D::ApplyAppearanceRuntimeState(component_, appearance.style);
}

void InitializeRectPath(Geometry2D& component_,
                        std::uint32_t geometry_id_,
                        std::uint32_t appearance_id_,
                        std::int16_t layer_,
                        vr::ecs::Rgba8 fill_color_,
                        vr::ecs::Rgba8 stroke_color_,
                        float stroke_width_,
                        float min_x_,
                        float min_y_,
                        float max_x_,
                        float max_y_) {
    GeometryPathSystem::Initialize(component_);
    GeometrySystem2D::SetRuntimeRoute(component_, geometry_id_, appearance_id_, 0U);
    component_.style.topology = vr::ecs::Geometry2DTopology::fill_and_stroke;
    component_.style.stroke_width_px = stroke_width_;
    ApplyGeometry2DAppearance(component_, fill_color_, stroke_color_, 1.0F, layer_);

    (void)GeometryPathSystem::AppendMoveTo(component_, min_x_, min_y_);
    (void)GeometryPathSystem::AppendLineTo(component_, max_x_, min_y_);
    (void)GeometryPathSystem::AppendLineTo(component_, max_x_, max_y_);
    (void)GeometryPathSystem::AppendLineTo(component_, min_x_, max_y_);
    (void)GeometryPathSystem::AppendClose(component_);
}

void InitializeCurvePath(Geometry2D& component_,
                         std::uint32_t geometry_id_,
                         std::uint32_t appearance_id_,
                         std::int16_t layer_) {
    GeometryPathSystem::Initialize(component_);
    GeometrySystem2D::SetRuntimeRoute(component_, geometry_id_, appearance_id_, 0U);
    component_.style.topology = vr::ecs::Geometry2DTopology::stroke;
    component_.style.stroke_width_px = 4.0F;
    component_.style.line_cap = vr::ecs::Geometry2DLineCap::round;
    component_.style.line_join = vr::ecs::Geometry2DLineJoin::round;
    ApplyGeometry2DAppearance(component_,
                              vr::ecs::Rgba8{220U, 234U, 255U, 255U},
                              vr::ecs::Rgba8{220U, 234U, 255U, 255U},
                              1.0F,
                              layer_);

    (void)GeometryPathSystem::AppendMoveTo(component_, 60.0F, 640.0F);
    (void)GeometryPathSystem::AppendCubicTo(component_,
                                            300.0F,
                                            500.0F,
                                            720.0F,
                                            720.0F,
                                            1060.0F,
                                            560.0F);
}

[[nodiscard]] std::uint32_t ParseMaxFrames(int argc_,
                                           char** argv_) {
    if (argc_ <= 1 || argv_ == nullptr) {
        return 0U;
    }

    for (int index = 1; index + 1 < argc_; ++index) {
        if (std::string_view(argv_[index]) != "--frames") {
            continue;
        }
        return static_cast<std::uint32_t>(std::strtoul(argv_[index + 1], nullptr, 10));
    }
    return 0U;
}

} // namespace

int main(int argc_,
         char** argv_) {
    vr::runtime::InstallProcessCrashTracer(argc_, argv_);
    Runtime runtime{};
    vr::geometry::GeometryResourceHost geometry_resource_host{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryImageHost geometry_image_host{};
    vr::geometry::GeometryAppearanceHost geometry_appearance_host{};
    vr::render::SceneRecorder3D recorder{};
    vr::geometry::GeometryRenderer3D renderer_3d{};
    vr::geometry::GeometryRenderer2D renderer_2d{};
    const std::uint32_t max_frames = ParseMaxFrames(argc_, argv_);

    bool runtime_initialized = false;
    bool geometry_resource_host_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_image_host_initialized = false;
    bool geometry_appearance_host_initialized = false;
    bool renderer_3d_initialized = false;
    bool renderer_2d_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "Vulkan SDL3 Geometry Unified Demo";
        create_info.platform.window.width = 1280;
        create_info.platform.window.height = 720;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = true;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = true;
        create_info.render_loop.swapchain.preferred_image_count = 3U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        recorder.Initialize(BuildGeometryUnifiedRecorderCreateInfo());
        recorder.BindRuntime(runtime);

        vr::geometry::GeometryResourceHostCreateInfo resource_create_info{};
        resource_create_info.reserve_mesh_count = 64U;
        resource_create_info.reserve_submesh_count = 128U;
        resource_create_info.reserve_reusable_buffer_count = 16U;
        geometry_resource_host.Initialize(runtime.Context(),
                                          runtime.GpuMemory(),
                                          resource_create_info);
        geometry_resource_host_initialized = true;

        vr::geometry::GeometryUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_2d_primitive_buffer_bytes = 512U * 1024U;
        upload_create_info.initial_3d_instance_buffer_bytes = 1024U * 1024U;
        geometry_upload_host.Initialize(runtime.Context(),
                                        runtime.GpuMemory(),
                                        upload_create_info);
        geometry_upload_host_initialized = true;

        vr::geometry::GeometryImageHostCreateInfo image_create_info{};
        image_create_info.reserve_image_count = 64U;
        image_create_info.reserve_retired_image_count = 64U;
        geometry_image_host.Initialize(runtime.Context(),
                                       runtime.GpuMemory(),
                                       image_create_info);
        geometry_image_host_initialized = true;

        vr::geometry::GeometryAppearanceHostCreateInfo appearance_create_info{};
        appearance_create_info.reserve_appearance_count = 128U;
        geometry_appearance_host.Initialize(appearance_create_info);
        geometry_appearance_host_initialized = true;

        std::array<vr::geometry::GeometryMeshVertex, 4U> quad_vertices{
            vr::geometry::GeometryMeshVertex{
                .position_x = -0.5F, .position_y = -0.5F, .position_z = 0.0F,
                .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
                .uv_u = 0.0F, .uv_v = 0.0F},
            vr::geometry::GeometryMeshVertex{
                .position_x = 0.5F, .position_y = -0.5F, .position_z = 0.0F,
                .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
                .uv_u = 1.0F, .uv_v = 0.0F},
            vr::geometry::GeometryMeshVertex{
                .position_x = 0.5F, .position_y = 0.5F, .position_z = 0.0F,
                .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
                .uv_u = 1.0F, .uv_v = 1.0F},
            vr::geometry::GeometryMeshVertex{
                .position_x = -0.5F, .position_y = 0.5F, .position_z = 0.0F,
                .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
                .uv_u = 0.0F, .uv_v = 1.0F}
        };
        std::array<std::uint32_t, 6U> quad_indices{0U, 1U, 2U, 2U, 3U, 0U};
        std::array<vr::geometry::GeometrySubmeshRange, 1U> quad_submeshes{
            vr::geometry::GeometrySubmeshRange{
                .first_index = 0U,
                .index_count = 6U,
                .vertex_offset = 0,
                .reserved0 = 0U}
        };

        vr::geometry::GeometryMeshUploadInfo mesh_upload_info{};
        mesh_upload_info.geometry_id = 1U;
        mesh_upload_info.vertices = quad_vertices.data();
        mesh_upload_info.vertex_count = static_cast<std::uint32_t>(quad_vertices.size());
        mesh_upload_info.indices = quad_indices.data();
        mesh_upload_info.index_count = static_cast<std::uint32_t>(quad_indices.size());
        mesh_upload_info.submeshes = quad_submeshes.data();
        mesh_upload_info.submesh_count = static_cast<std::uint32_t>(quad_submeshes.size());
        mesh_upload_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        mesh_upload_info.bounds_min = vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F};
        mesh_upload_info.bounds_max = vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F};

        std::array<std::uint32_t, 16U> pixels_a{};
        std::array<std::uint32_t, 16U> pixels_b{};
        for (std::uint32_t y = 0U; y < 4U; ++y) {
            for (std::uint32_t x = 0U; x < 4U; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * 4U + x;
                const bool checker = ((x ^ y) & 1U) != 0U;
                pixels_a[index] = checker ? 0xFFF7E4C2U : 0xFFAB8F60U;
                pixels_b[index] = checker ? 0xFF99D8FFU : 0xFF355A82U;
            }
        }

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        geometry_resource_host.UploadMesh(runtime.Context(),
                                          runtime.Upload(),
                                          0U,
                                          0U,
                                          0U,
                                          mesh_upload_info);

        vr::geometry::GeometryImageUploadInfo image_upload_a{};
        image_upload_a.image_id = 1001U;
        image_upload_a.pixels = pixels_a.data();
        image_upload_a.width = 4U;
        image_upload_a.height = 4U;
        image_upload_a.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_upload_a.bytes_per_pixel = 4U;
        image_upload_a.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_upload_a.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_upload_a.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        geometry_image_host.UploadImage(runtime.Context(),
                                        runtime.Upload(),
                                        0U,
                                        0U,
                                        0U,
                                        image_upload_a);

        vr::geometry::GeometryImageUploadInfo image_upload_b = image_upload_a;
        image_upload_b.image_id = 1002U;
        image_upload_b.pixels = pixels_b.data();
        geometry_image_host.UploadImage(runtime.Context(),
                                        runtime.Upload(),
                                        0U,
                                        0U,
                                        0U,
                                        image_upload_b);

        const vr::render::UploadEndFrameResult upload_end_result =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end_result.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }

        geometry_resource_host.BeginFrame(runtime.Context(), 0U);
        geometry_image_host.BeginFrame(runtime.Context(), 0U);

        vr::geometry::GeometryAppearanceDesc appearance_a{};
        appearance_a.appearance_id = 11U;
        appearance_a.sampled_surface_binding.base_color_surface.surface_id = 1001U;
        appearance_a.uv_scale_u = 1.0F;
        appearance_a.uv_scale_v = 1.0F;
        geometry_appearance_host.UpsertAppearance(appearance_a);

        vr::geometry::GeometryAppearanceDesc appearance_b{};
        appearance_b.appearance_id = 22U;
        appearance_b.sampled_surface_binding.base_color_surface.surface_id = 1002U;
        appearance_b.uv_scale_u = 1.35F;
        appearance_b.uv_scale_v = 1.35F;
        appearance_b.uv_bias_u = -0.15F;
        appearance_b.uv_bias_v = 0.10F;
        appearance_b.flags = vr::geometry::geometry_appearance_flag_alpha_test;
        appearance_b.alpha_cutoff = 0.2F;
        geometry_appearance_host.UpsertAppearance(appearance_b);

        std::array<Geometry3D, 2U> geometry_3d_components{};
        std::array<Appearance3D, 2U> geometry_3d_appearances{};
        InitializeGeometry3DComponent(geometry_3d_components[0U],
                                      geometry_3d_appearances[0U],
                                      0U,
                                      1U,
                                      11U,
                                      vr::ecs::Rgba8{255U, 255U, 255U, 255U},
                                      true,
                                      false);
        InitializeGeometry3DComponent(geometry_3d_components[1U],
                                      geometry_3d_appearances[1U],
                                      1U,
                                      1U,
                                      22U,
                                      vr::ecs::Rgba8{220U, 245U, 255U, 230U},
                                      false,
                                      true);
        geometry_3d_components[1U].runtime.route.depth_bin = 40U;
        GeometrySystem3D::RebuildSortKey(geometry_3d_components[1U]);

        std::array<Transform3D, 2U> geometry_3d_transforms{};
        TransformSystem3D::Initialize(geometry_3d_transforms[0U]);
        TransformSystem3D::Initialize(geometry_3d_transforms[1U]);
        TransformSystem3D::SetLocalPosition(geometry_3d_transforms[0U],
                                            vr::ecs::Float3{.x = -0.90F, .y = 0.0F, .z = 0.0F});
        TransformSystem3D::SetLocalScale(geometry_3d_transforms[0U],
                                         vr::ecs::Float3{.x = 2.0F, .y = 2.0F, .z = 1.0F});
        TransformSystem3D::SetLocalPosition(geometry_3d_transforms[1U],
                                            vr::ecs::Float3{.x = 1.05F, .y = -0.1F, .z = -0.25F});
        TransformSystem3D::SetLocalScale(geometry_3d_transforms[1U],
                                         vr::ecs::Float3{.x = 1.7F, .y = 1.7F, .z = 1.0F});

        Camera3D camera{};
        CameraSystem3D::Initialize(camera);
        CameraSystem3D::SetAspectRatio(camera, 1280.0F / 720.0F);
        CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
        CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);

        Transform3D camera_transform{};
        TransformSystem3D::Initialize(camera_transform);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.5F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

        std::array<Geometry2D, 2U> geometry_2d_components{};
        InitializeRectPath(geometry_2d_components[0U],
                           501U,
                           1U,
                           5,
                           vr::ecs::Rgba8{40U, 56U, 86U, 210U},
                           vr::ecs::Rgba8{186U, 206U, 244U, 255U},
                           2.0F,
                           24.0F,
                           24.0F,
                           430.0F,
                           168.0F);
        InitializeCurvePath(geometry_2d_components[1U], 502U, 1U, 7);

        vr::geometry::GeometryRenderer3DCreateInfo renderer_3d_create_info{};
        renderer_3d_create_info.reserve_component_count =
            static_cast<std::uint32_t>(geometry_3d_components.size());
        renderer_3d_create_info.reserve_instance_count = 256U;
        renderer_3d_create_info.reserve_appearance_set_count = 64U;
        renderer_3d_create_info.enable_depth = true;
        renderer_3d_create_info.clear_depth = true;
        renderer_3d_create_info.clear_swapchain = false;
        renderer_3d_create_info.clear_color = {{0.06F, 0.07F, 0.11F, 1.0F}};
        renderer_3d_create_info.directional_light_x = 0.45F;
        renderer_3d_create_info.directional_light_y = -0.95F;
        renderer_3d_create_info.directional_light_z = 0.25F;
        renderer_3d_create_info.directional_light_intensity = 1.08F;
        renderer_3d.Initialize(renderer_3d_create_info);
        renderer_3d_initialized = true;
        renderer_3d.SetHosts(&geometry_resource_host, &geometry_upload_host);
        renderer_3d.SetAppearanceHosts(&geometry_appearance_host, &geometry_image_host);
        renderer_3d.SetAppearanceData(geometry_3d_appearances.data(),
                                      static_cast<std::uint32_t>(geometry_3d_appearances.size()));
        renderer_3d.SetSceneData(geometry_3d_components.data(),
                                 geometry_3d_transforms.data(),
                                 static_cast<std::uint32_t>(geometry_3d_components.size()),
                                 &camera,
                                 &camera_transform);
        recorder.RegisterOpaqueSceneRenderer(renderer_3d, vr::render::SceneRenderPassRole::single);

        vr::geometry::GeometryRenderer2DCreateInfo renderer_2d_create_info{};
        renderer_2d_create_info.reserve_component_count =
            static_cast<std::uint32_t>(geometry_2d_components.size());
        renderer_2d_create_info.reserve_primitive_count = 2048U;
        renderer_2d_create_info.input_positions_pixel_space = true;
        renderer_2d_create_info.pixel_space_origin_top_left = true;
        renderer_2d_create_info.clear_swapchain = false;
        renderer_2d.Initialize(renderer_2d_create_info);
        renderer_2d_initialized = true;
        renderer_2d.SetHost(&geometry_upload_host);
        renderer_2d.SetSceneData(geometry_2d_components.data(),
                                 static_cast<std::uint32_t>(geometry_2d_components.size()));
        recorder.RegisterOverlayRenderer(renderer_2d);

        vr::render::RenderView3D main_view{};
        vr::render::RenderScenePacket3D main_scene_packet{};
        vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                           main_scene_packet,
                                                           camera,
                                                           camera_transform,
                                                           runtime.Swapchain().Extent(),
                                                           0U);
        recorder.SetFramePacket(&main_scene_packet);

        std::cout << "sdl_geometry_unified_demo running (3D geometry offscreen + bloom post stack + 2D overlay). Close window to exit.\n";

        std::uint64_t fps_window_begin_ticks = SDL_GetTicks();
        std::uint32_t fps_window_frame_count = 0U;
        std::uint64_t frame_index = 0U;
        while (runtime.IsRunning()) {
            const std::uint64_t now_ticks = SDL_GetTicks();
            const float time_seconds = static_cast<float>(now_ticks) * 0.001F;

            const VkExtent2D extent = runtime.Swapchain().Extent();
            TransformSystem3D::SetLocalRotationEulerXyz(geometry_3d_transforms[0U],
                                                        0.0F,
                                                        0.0F,
                                                        0.45F * time_seconds);
            TransformSystem3D::SetLocalRotationEulerXyz(geometry_3d_transforms[1U],
                                                        0.0F,
                                                        0.65F * time_seconds,
                                                        0.15F * std::sin(time_seconds));
            TransformSystem3D::UpdateHierarchy(geometry_3d_transforms.data(),
                                               static_cast<std::uint32_t>(geometry_3d_transforms.size()));
            vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                               main_scene_packet,
                                                               camera,
                                                               camera_transform,
                                                               extent,
                                                               frame_index);
            recorder.SetFramePacket(&main_scene_packet);

            const std::uint8_t pulse_alpha = static_cast<std::uint8_t>(
                120.0F + 100.0F * (0.5F + 0.5F * std::sin(time_seconds * 2.0F)));
            const auto appearance_bridge =
                vr::ecs::ReadAppearanceRuntimeBridge2D(geometry_2d_components[0U].runtime);
            ApplyGeometry2DAppearance(
                geometry_2d_components[0U],
                vr::ecs::Rgba8{40U, 56U, 86U, static_cast<std::uint8_t>(110U + pulse_alpha / 2U)},
                appearance_bridge.stroke_color,
                appearance_bridge.opacity,
                appearance_bridge.layer);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            (void)tick_result;
            ++fps_window_frame_count;
            ++frame_index;

            if (max_frames > 0U && frame_index >= max_frames) {
                break;
            }

            const std::uint64_t fps_window_elapsed = now_ticks - fps_window_begin_ticks;
            if (fps_window_elapsed >= 1000U) {
                const float fps = (fps_window_elapsed > 0U)
                    ? (1000.0F * static_cast<float>(fps_window_frame_count) /
                       static_cast<float>(fps_window_elapsed))
                    : 0.0F;
                const vr::geometry::GeometryRenderer3DStats stats_3d = renderer_3d.Stats();
                const auto bloom_stats = recorder.BloomStats();
                const vr::geometry::GeometryRenderer2DStats stats_2d = renderer_2d.Stats();
                std::cout << "FPS: " << fps
                          << " | Frame:" << frame_index
                          << " | 3D Draw:" << stats_3d.draw_call_count
                          << " Batch:" << stats_3d.draw_batch_count
                          << " AppSet:" << stats_3d.appearance_set_count
                          << " | Bloom P:" << bloom_stats.prefilter_draw_call_count
                          << " B:" << bloom_stats.blur_draw_call_count
                          << " C:" << bloom_stats.combine_draw_call_count
                          << " DSU:" << bloom_stats.descriptor_set_update_count
                          << " | 2D Draw:" << stats_2d.draw_call_count
                          << " Prim:" << stats_2d.primitive_count
                          << '\n';
                fps_window_begin_ticks = now_ticks;
                fps_window_frame_count = 0U;
            }
        }

        renderer_2d.Shutdown(runtime.Context());
        renderer_2d_initialized = false;
        recorder.Shutdown(runtime.Context());
        renderer_3d.Shutdown(runtime.Context());
        renderer_3d_initialized = false;
        geometry_appearance_host.Shutdown();
        geometry_appearance_host_initialized = false;
        geometry_image_host.Shutdown(runtime.Context());
        geometry_image_host_initialized = false;
        geometry_upload_host.Shutdown(runtime.Context());
        geometry_upload_host_initialized = false;
        geometry_resource_host.Shutdown(runtime.Context());
        geometry_resource_host_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        std::cerr << "sdl_geometry_unified_demo failed: " << exception_.what() << '\n';

        if (renderer_2d_initialized && runtime_initialized && runtime.IsInitialized()) {
            renderer_2d.Shutdown(runtime.Context());
            renderer_2d_initialized = false;
        }
        if (runtime_initialized && runtime.IsInitialized() && recorder.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
        }
        if (renderer_3d_initialized && runtime_initialized && runtime.IsInitialized()) {
            renderer_3d.Shutdown(runtime.Context());
            renderer_3d_initialized = false;
        }
        if (geometry_appearance_host_initialized) {
            geometry_appearance_host.Shutdown();
            geometry_appearance_host_initialized = false;
        }
        if (geometry_image_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            geometry_image_host.Shutdown(runtime.Context());
            geometry_image_host_initialized = false;
        }
        if (geometry_upload_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            geometry_upload_host.Shutdown(runtime.Context());
            geometry_upload_host_initialized = false;
        }
        if (geometry_resource_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            geometry_resource_host.Shutdown(runtime.Context());
            geometry_resource_host_initialized = false;
        }
        if (runtime_initialized && runtime.IsInitialized()) {
            runtime.Shutdown();
            runtime_initialized = false;
        }
        return 1;
    }

    return 0;
}

