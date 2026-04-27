#include "support/test_framework.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/geometry/geometry_material_host.hpp"
#include "vr/geometry/geometry_renderer_3d.hpp"
#include "vr/render/render_runtime_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

[[nodiscard]] std::string ToLower(std::string_view value_) {
    std::string lowered{};
    lowered.reserve(value_.size());
    for (const unsigned char ch : value_) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view text_,
                                           std::string_view needle_) {
    const std::string lowered_text = ToLower(text_);
    const std::string lowered_needle = ToLower(needle_);
    return lowered_text.find(lowered_needle) != std::string::npos;
}

[[nodiscard]] bool IsEnvironmentSkipError(std::string_view message_) {
    constexpr std::array<std::string_view, 15U> patterns{
        "sdl_initsubsystem",
        "sdl_createwindow",
        "sdl_vulkan_getinstanceextensions",
        "sdl_vulkan_createsurface",
        "vkcreateinstance",
        "vkenumeratephysicaldevices",
        "no vulkan physical devices found",
        "no suitable vulkan physical device found",
        "missing required instance extension",
        "vkcreatedevice",
        "vkgetphysicaldevicesurfacesupportkhr",
        "vkgetphysicaldevicesurfaceformatskhr",
        "vkgetphysicaldevicesurfacepresentmodeskhr",
        "dynamicrendering",
        "synchronization2"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

void InitializeGeometryComponent(Geometry3D& component_,
                                 std::uint32_t geometry_id_,
                                 std::uint32_t material_id_,
                                 vr::ecs::Float3 bounds_min_,
                                 vr::ecs::Float3 bounds_max_,
                                 bool depth_test_,
                                 bool depth_write_,
                                 vr::ecs::Geometry3DShadingModel shading_model_,
                                 vr::ecs::Rgba8 albedo_) {
    GeometrySystem3D::Initialize(component_);
    GeometrySystem3D::SetRuntimeRoute(component_, geometry_id_, material_id_, 0U);
    GeometrySystem3D::SetBounds(component_, bounds_min_, bounds_max_);
    component_.style.depth_test = depth_test_ ? 1U : 0U;
    component_.style.depth_write = depth_write_ ? 1U : 0U;
    component_.style.shading_model = shading_model_;
    component_.style.albedo_color = albedo_;
    component_.mesh.submesh_index = 0U;
    component_.mesh.lod_index = 0U;
    component_.mesh.flags = 0U;
}

VR_TEST_CASE(RuntimeIntegration_geometry_renderer_3d_end_to_end_smoke, "integration;gpu;sdl;runtime;geometry") {
    Runtime runtime{};
    vr::geometry::GeometryResourceHost geometry_resource_host{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryImageHost geometry_image_host{};
    vr::geometry::GeometryMaterialHost geometry_material_host{};
    vr::geometry::GeometryRenderer3D geometry_renderer{};

    bool runtime_initialized = false;
    bool geometry_resource_host_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_image_host_initialized = false;
    bool geometry_material_host_initialized = false;
    bool geometry_renderer_initialized = false;

    std::array<vr::geometry::GeometryMeshVertex, 4U> vertices{
        vr::geometry::GeometryMeshVertex{.position_x = -0.5F, .position_y = -0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 0.0F, .uv_v = 0.0F},
        vr::geometry::GeometryMeshVertex{.position_x = 0.5F, .position_y = -0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 1.0F, .uv_v = 0.0F},
        vr::geometry::GeometryMeshVertex{.position_x = 0.5F, .position_y = 0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 1.0F, .uv_v = 1.0F},
        vr::geometry::GeometryMeshVertex{.position_x = -0.5F, .position_y = 0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 0.0F, .uv_v = 1.0F}
    };
    std::array<std::uint32_t, 6U> indices{0U, 1U, 2U, 2U, 3U, 0U};
    std::array<vr::geometry::GeometrySubmeshRange, 1U> submeshes{
        vr::geometry::GeometrySubmeshRange{.first_index = 0U, .index_count = 6U, .vertex_offset = 0, .reserved0 = 0U}
    };
    std::array<std::uint32_t, 16U> pixels_material_11{};
    std::array<std::uint32_t, 16U> pixels_material_22{};
    for (std::uint32_t y = 0U; y < 4U; ++y) {
        for (std::uint32_t x = 0U; x < 4U; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * 4U + x;
            const bool checker = ((x ^ y) & 1U) != 0U;
            pixels_material_11[index] = checker ? 0xFFBFA57BU : 0xFFF4E9D0U;
            pixels_material_22[index] = checker ? 0xFF6FC6FFU : 0xFF2D456BU;
        }
    }

    vr::geometry::GeometryMeshUploadInfo mesh_upload_info{};
    mesh_upload_info.geometry_id = 1U;
    mesh_upload_info.vertices = vertices.data();
    mesh_upload_info.vertex_count = static_cast<std::uint32_t>(vertices.size());
    mesh_upload_info.indices = indices.data();
    mesh_upload_info.index_count = static_cast<std::uint32_t>(indices.size());
    mesh_upload_info.submeshes = submeshes.data();
    mesh_upload_info.submesh_count = static_cast<std::uint32_t>(submeshes.size());
    mesh_upload_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    mesh_upload_info.bounds_min = vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F};
    mesh_upload_info.bounds_max = vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F};

    std::array<Geometry3D, 2U> geometry_components{};
    InitializeGeometryComponent(geometry_components[0U],
                                1U,
                                11U,
                                vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F},
                                true,
                                true,
                                vr::ecs::Geometry3DShadingModel::lit,
                                vr::ecs::Rgba8{235U, 208U, 160U, 255U});
    InitializeGeometryComponent(geometry_components[1U],
                                1U,
                                22U,
                                vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F},
                                true,
                                false,
                                vr::ecs::Geometry3DShadingModel::unlit,
                                vr::ecs::Rgba8{170U, 225U, 255U, 210U});
    geometry_components[1U].style.double_sided = 1U;
    geometry_components[1U].style.cast_shadow = 0U;
    geometry_components[1U].runtime.route.depth_bin = 4U;
    GeometrySystem3D::RebuildSortKey(geometry_components[1U]);

    std::array<Transform3D, 2U> transforms{};
    TransformSystem3D::Initialize(transforms[0U]);
    TransformSystem3D::Initialize(transforms[1U]);
    TransformSystem3D::SetLocalPosition(transforms[0U], vr::ecs::Float3{.x = -0.70F, .y = 0.0F, .z = 0.0F});
    TransformSystem3D::SetLocalScale(transforms[0U], vr::ecs::Float3{.x = 1.8F, .y = 1.8F, .z = 1.0F});
    TransformSystem3D::SetLocalPosition(transforms[1U], vr::ecs::Float3{.x = 0.80F, .y = -0.20F, .z = -0.25F});
    TransformSystem3D::SetLocalScale(transforms[1U], vr::ecs::Float3{.x = 1.5F, .y = 1.5F, .z = 1.0F});
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    std::array<Bounds3D, 2U> bounds_components{};
    for (std::uint32_t i = 0U; i < bounds_components.size(); ++i) {
        BoundsSystem3D::Initialize(bounds_components[i]);
        BoundsSystem3D::SetLocalAabb(bounds_components[i],
                                     vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                     vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F});
    }
    (void)BoundsSystem3D::UpdateAligned(bounds_components.data(),
                                        transforms.data(),
                                        static_cast<std::uint32_t>(bounds_components.size()));

    Camera3D camera{};
    CameraSystem3D::Initialize(camera);
    CameraSystem3D::SetAspectRatio(camera, 1280.0F / 720.0F);
    CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
    CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);

    Transform3D camera_transform{};
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.2F});
    TransformSystem3D::SetLocalRotationEulerXyz(camera_transform, 0.0F, 0.0F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_geometry_3d_smoke";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::geometry::GeometryResourceHostCreateInfo resource_create_info{};
        resource_create_info.reserve_mesh_count = 32U;
        resource_create_info.reserve_submesh_count = 64U;
        resource_create_info.reserve_reusable_buffer_count = 16U;
        resource_create_info.max_reusable_vertex_buffer_count = 32U;
        resource_create_info.max_reusable_index_buffer_count = 32U;
        geometry_resource_host.Initialize(runtime.Context(), runtime.GpuMemory(), resource_create_info);
        geometry_resource_host_initialized = true;

        vr::geometry::GeometryUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_3d_instance_buffer_bytes = 256U * 1024U;
        geometry_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        geometry_upload_host_initialized = true;

        vr::geometry::GeometryImageHostCreateInfo image_create_info{};
        image_create_info.reserve_image_count = 32U;
        image_create_info.reserve_retired_image_count = 32U;
        geometry_image_host.Initialize(runtime.Context(), runtime.GpuMemory(), image_create_info);
        geometry_image_host_initialized = true;

        vr::geometry::GeometryMaterialHostCreateInfo material_create_info{};
        material_create_info.reserve_material_count = 64U;
        geometry_material_host.Initialize(material_create_info);
        geometry_material_host_initialized = true;

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        geometry_resource_host.UploadMesh(runtime.Context(),
                                          runtime.Upload(),
                                          0U,
                                          0U,
                                          0U,
                                          mesh_upload_info);
        geometry_resource_host.UploadMesh(runtime.Context(),
                                          runtime.Upload(),
                                          0U,
                                          0U,
                                          0U,
                                          mesh_upload_info);
        geometry_resource_host.BeginFrame(runtime.Context(), 0U);
        geometry_resource_host.UploadMesh(runtime.Context(),
                                          runtime.Upload(),
                                          0U,
                                          0U,
                                          0U,
                                          mesh_upload_info);

        vr::geometry::GeometryImageUploadInfo upload_image_11{};
        upload_image_11.image_id = 101U;
        upload_image_11.pixels = pixels_material_11.data();
        upload_image_11.width = 4U;
        upload_image_11.height = 4U;
        upload_image_11.format = VK_FORMAT_R8G8B8A8_UNORM;
        upload_image_11.bytes_per_pixel = 4U;
        upload_image_11.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        upload_image_11.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        upload_image_11.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        geometry_image_host.UploadImage(runtime.Context(),
                                        runtime.Upload(),
                                        0U,
                                        0U,
                                        0U,
                                        upload_image_11);

        vr::geometry::GeometryImageUploadInfo upload_image_22 = upload_image_11;
        upload_image_22.image_id = 202U;
        upload_image_22.pixels = pixels_material_22.data();
        geometry_image_host.UploadImage(runtime.Context(),
                                        runtime.Upload(),
                                        0U,
                                        0U,
                                        0U,
                                        upload_image_22);
        const vr::render::UploadEndFrameResult upload_end = runtime.Upload().EndFrameAndSubmit(runtime.Context(),
                                                                                                0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        geometry_resource_host.BeginFrame(runtime.Context(), 0U);
        geometry_image_host.BeginFrame(runtime.Context(), 0U);

        vr::geometry::GeometryMaterialDesc material_11{};
        material_11.material_id = 11U;
        material_11.image_id = 101U;
        material_11.uv_scale_u = 1.0F;
        material_11.uv_scale_v = 1.0F;
        geometry_material_host.UpsertMaterial(material_11);

        vr::geometry::GeometryMaterialDesc material_22{};
        material_22.material_id = 22U;
        material_22.image_id = 202U;
        material_22.uv_scale_u = 1.25F;
        material_22.uv_scale_v = 1.25F;
        material_22.uv_bias_u = -0.12F;
        material_22.uv_bias_v = 0.08F;
        material_22.flags = vr::geometry::geometry_material_flag_alpha_test;
        material_22.alpha_cutoff = 0.2F;
        geometry_material_host.UpsertMaterial(material_22);

        vr::geometry::GeometryRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(geometry_components.size());
        renderer_create_info.reserve_instance_count = 128U;
        renderer_create_info.enable_depth = true;
        renderer_create_info.clear_swapchain = true;
        renderer_create_info.clear_color = {{0.05F, 0.07F, 0.10F, 1.0F}};
        renderer_create_info.directional_light_x = 0.6F;
        renderer_create_info.directional_light_y = -0.8F;
        renderer_create_info.directional_light_z = 0.3F;
        renderer_create_info.directional_light_intensity = 1.0F;
        geometry_renderer.Initialize(renderer_create_info);
        geometry_renderer_initialized = true;
        geometry_renderer.SetHosts(&geometry_resource_host, &geometry_upload_host);
        geometry_renderer.SetMaterialHosts(&geometry_material_host, &geometry_image_host);
        geometry_renderer.SetSceneData(geometry_components.data(),
                                       transforms.data(),
                                       static_cast<std::uint32_t>(geometry_components.size()),
                                       &camera,
                                       &camera_transform,
                                       bounds_components.data());

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_draw_batches = 0U;
        std::uint32_t max_instances = 0U;
        std::uint32_t max_depth_test_batches = 0U;
        std::uint32_t max_depth_write_batches = 0U;
        std::uint32_t max_descriptor_updates = 0U;
        std::uint32_t max_material_push_constant_updates = 0U;
        std::uint32_t max_material_sets = 0U;
        std::uint32_t max_culling_input_count = 0U;
        std::uint32_t max_culling_visible_count = 0U;
        bool observed_bounds_culling = false;

        constexpr std::uint32_t max_ticks = 16U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            TransformSystem3D::SetLocalRotationEulerXyz(transforms[0U],
                                                         0.0F,
                                                         0.0F,
                                                         0.15F * static_cast<float>(tick_index));
            TransformSystem3D::SetLocalRotationEulerXyz(transforms[1U],
                                                         0.0F,
                                                         0.2F * static_cast<float>(tick_index),
                                                         0.0F);
            TransformSystem3D::UpdateHierarchy(transforms.data(),
                                               static_cast<std::uint32_t>(transforms.size()));
            (void)BoundsSystem3D::UpdateAligned(bounds_components.data(),
                                                transforms.data(),
                                                static_cast<std::uint32_t>(bounds_components.size()));
            CameraSystem3D::Update(camera, camera_transform);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(geometry_renderer);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            const vr::geometry::GeometryRenderer3DStats renderer_stats = geometry_renderer.Stats();
            max_draw_calls = std::max(max_draw_calls, renderer_stats.draw_call_count);
            max_draw_batches = std::max(max_draw_batches, renderer_stats.draw_batch_count);
            max_instances = std::max(max_instances, renderer_stats.instance_count);
            max_depth_test_batches = std::max(max_depth_test_batches, renderer_stats.depth_test_batch_count);
            max_depth_write_batches = std::max(max_depth_write_batches, renderer_stats.depth_write_batch_count);
            max_descriptor_updates = std::max(max_descriptor_updates, renderer_stats.descriptor_set_update_count);
            max_material_push_constant_updates = std::max(max_material_push_constant_updates,
                                                          renderer_stats.material_push_constant_update_count);
            max_material_sets = std::max(max_material_sets, renderer_stats.material_set_count);
            max_culling_input_count = std::max(max_culling_input_count, renderer_stats.culling_input_count);
            max_culling_visible_count = std::max(max_culling_visible_count, renderer_stats.culling_visible_count);
            observed_bounds_culling = observed_bounds_culling || renderer_stats.used_bounds_culling;
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_draw_calls > 0U);
        VR_CHECK(max_draw_batches > 0U);
        VR_CHECK(max_instances > 0U);
        VR_CHECK(max_depth_test_batches > 0U);
        VR_CHECK(max_depth_write_batches > 0U);
        VR_CHECK(max_descriptor_updates > 0U);
        VR_CHECK(max_material_push_constant_updates > 0U);
        VR_CHECK(max_material_sets > 0U);
        VR_CHECK(observed_bounds_culling);
        VR_CHECK(max_culling_input_count == static_cast<std::uint32_t>(geometry_components.size()));
        VR_CHECK(max_culling_visible_count > 0U);
        VR_CHECK(geometry_resource_host.Stats().mesh_count > 0U);
        VR_CHECK(geometry_resource_host.Stats().reused_vertex_buffer_count > 0U);
        VR_CHECK(geometry_resource_host.Stats().reused_index_buffer_count > 0U);
        VR_CHECK(geometry_upload_host.Stats().upload_count > 0U);
        VR_CHECK(geometry_image_host.Stats().image_count >= 2U);
        VR_CHECK(geometry_material_host.Stats().material_count >= 2U);

        geometry_renderer.Shutdown(runtime.Context());
        geometry_renderer_initialized = false;
        geometry_material_host.Shutdown();
        geometry_material_host_initialized = false;
        geometry_image_host.Shutdown(runtime.Context());
        geometry_image_host_initialized = false;
        geometry_upload_host.Shutdown(runtime.Context());
        geometry_upload_host_initialized = false;
        geometry_resource_host.Shutdown(runtime.Context());
        geometry_resource_host_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (geometry_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            geometry_renderer.Shutdown(runtime.Context());
            geometry_renderer_initialized = false;
        }
        if (geometry_material_host_initialized) {
            geometry_material_host.Shutdown();
            geometry_material_host_initialized = false;
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

        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

} // namespace
