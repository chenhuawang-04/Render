#include "support/test_framework.hpp"
#include "support/render_graph_test_utils.hpp"
#include "vr/ecs/system/appearance_runtime_system.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/light_system.hpp"
#include "vr/ecs/system/shadow_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/geometry/geometry_appearance_host.hpp"
#include "vr/geometry/geometry_renderer_3d.hpp"
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/render_target_format_utils.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render/scene_render_target_set.hpp"
#include "vr/shadow/shadow_renderer_3d.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
using Light3D = vr::ecs::Light<vr::ecs::Dim3>;
using LightSystem3D = vr::ecs::LightSystem<vr::ecs::Dim3>;
using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

void ApplyGeometryAppearanceBridge(Geometry3D& component_,
                                   bool depth_test_,
                                   bool depth_write_,
                                   bool double_sided_,
                                   bool cast_shadow_,
                                   bool receive_shadow_,
                                   vr::ecs::AppearanceShadingModel3D shading_model_,
                                   vr::ecs::Rgba8 base_color_) {
    Appearance3D appearance{};
    AppearanceSystem3D::Initialize(appearance);
    AppearanceSystem3D::SetBaseColor(appearance, base_color_);
    AppearanceSystem3D::SetOpacity(appearance, static_cast<float>(base_color_.a) / 255.0F);
    AppearanceSystem3D::SetShadingModel(appearance, shading_model_);
    AppearanceSystem3D::SetDepthTest(appearance, depth_test_);
    AppearanceSystem3D::SetDepthWrite(appearance, depth_write_);
    AppearanceSystem3D::SetDoubleSided(appearance, double_sided_);
    AppearanceSystem3D::SetCastShadow(appearance, cast_shadow_);
    AppearanceSystem3D::SetReceiveShadow(appearance, receive_shadow_);
    if (base_color_.a < 255U || !depth_write_) {
        AppearanceSystem3D::SetBlendMode(appearance, vr::ecs::AppearanceBlendMode::alpha);
        AppearanceSystem3D::SetAlphaMode(appearance, vr::ecs::AppearanceAlphaMode::blend);
    }
    (void)GeometrySystem3D::ApplyAppearanceRuntimeState(component_, appearance.style);
}

[[nodiscard]] VkFormat ResolveDepthTargetFormat(vr::VulkanContext& context_) {
    constexpr std::array<VkFormat, 3U> candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT
    };
    return vr::render::ResolveFirstSupportedDepthStencilFormat(context_, candidates);
}

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
                                 std::uint32_t appearance_id_,
                                 vr::ecs::Float3 bounds_min_,
                                 vr::ecs::Float3 bounds_max_,
                                 bool depth_test_,
                                 bool depth_write_,
                                 bool double_sided_,
                                 bool cast_shadow_,
                                 bool receive_shadow_,
                                 vr::ecs::AppearanceShadingModel3D shading_model_,
                                 vr::ecs::Rgba8 base_color_) {
    GeometrySystem3D::Initialize(component_);
    GeometrySystem3D::SetRuntimeRoute(component_, geometry_id_, appearance_id_, 0U);
    GeometrySystem3D::SetBounds(component_, bounds_min_, bounds_max_);
    component_.mesh.submesh_index = 0U;
    component_.mesh.lod_index = 0U;
    component_.mesh.flags = 0U;
    ApplyGeometryAppearanceBridge(component_,
                                  depth_test_,
                                  depth_write_,
                                  double_sided_,
                                  cast_shadow_,
                                  receive_shadow_,
                                  shading_model_,
                                  base_color_);
}

void InitializeLightComponent(Light3D& component_) {
    LightSystem3D::Initialize(component_);
    LightSystem3D::SetLightKind(component_, vr::ecs::LightKind::spot);
    LightSystem3D::SetColor(component_, vr::ecs::Rgba8{255U, 244U, 224U, 255U});
    LightSystem3D::SetIntensity(component_, 1800.0F);
    LightSystem3D::SetRange(component_, 18.0F);
    LightSystem3D::SetConeAngles(component_, 0.30F, 0.70F);
    LightSystem3D::SetCastShadow(component_, true);
}

void InitializeShadowComponent(Shadow3D& component_,
                               std::uint32_t light_component_index_) {
    ShadowSystem3D::Initialize(component_);
    ShadowSystem3D::SetProjectionKind(component_, vr::ecs::ShadowProjectionKind::spot);
    ShadowSystem3D::SetCascadeConfig(component_, 1U, 0.5F);
    ShadowSystem3D::SetMapResolution(component_, 1024U, 1024U);
    ShadowSystem3D::SetFilterKernel(component_, vr::ecs::ShadowFilterKernel::pcf5x5);
    ShadowSystem3D::SetBias(component_, 0.0012F, 0.00035F);
    ShadowSystem3D::SetDepthSlopeBias(component_, 1.75F);
    ShadowSystem3D::SetLightComponentIndex(component_, light_component_index_);
    ShadowSystem3D::SetTransformComponentIndex(component_, 0U);
    ShadowSystem3D::SetCameraComponentIndex(component_, 0U);
    ShadowSystem3D::SetAtlasNamespace(component_, 7U);
    ShadowSystem3D::SetFaceCount(component_, 1U);
}

[[nodiscard]] vr::render::SceneRecorder3DCreateInfo BuildGeometryRecorderCreateInfo() noexcept {
    vr::render::SceneRecorder3DCreateInfo create_info{};
    create_info.scene_target.color_debug_name = "RuntimeGeometry3DSceneColor";
    create_info.scene_target.depth_debug_name = "RuntimeGeometry3DSceneDepth";
    create_info.scene_target.enable_depth = true;
    create_info.scene_target.color_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.depth_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.clear_color = VkClearColorValue{{0.05F, 0.07F, 0.10F, 1.0F}};
    create_info.bloom.clear_swapchain = true;
    create_info.bloom.enable_reinhard_tonemap = true;
    create_info.bloom.exposure = 1.0F;
    create_info.bloom.apply_manual_gamma = false;
    create_info.bloom.bloom_threshold = 0.70F;
    create_info.bloom.bloom_knee = 0.42F;
    create_info.bloom.bloom_intensity = 0.88F;
    create_info.bloom.blur_filter_scale = 1.02F;
    create_info.bloom.downsample_scale = 0.5F;
    create_info.reserve_scene_renderer_count = 1U;
    create_info.reserve_overlay_renderer_count = 0U;
    return create_info;
}

VR_TEST_CASE(RuntimeIntegration_geometry_renderer_3d_end_to_end_smoke, "integration;gpu;sdl;runtime;geometry") {
    Runtime runtime{};
    vr::geometry::GeometryResourceHost geometry_resource_host{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryImageHost geometry_image_host{};
    vr::geometry::GeometryAppearanceHost geometry_appearance_host{};
    vr::geometry::GeometryRenderer3D geometry_renderer{};

    bool runtime_initialized = false;
    bool geometry_resource_host_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_image_host_initialized = false;
    bool geometry_appearance_host_initialized = false;
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
    std::array<std::uint32_t, 16U> pixels_appearance_11{};
    std::array<std::uint32_t, 16U> pixels_appearance_22{};
    for (std::uint32_t y = 0U; y < 4U; ++y) {
        for (std::uint32_t x = 0U; x < 4U; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * 4U + x;
            const bool checker = ((x ^ y) & 1U) != 0U;
            pixels_appearance_11[index] = checker ? 0xFFBFA57BU : 0xFFF4E9D0U;
            pixels_appearance_22[index] = checker ? 0xFF6FC6FFU : 0xFF2D456BU;
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
                                false,
                                true,
                                true,
                                vr::ecs::AppearanceShadingModel3D::lit_pbr,
                                vr::ecs::Rgba8{235U, 208U, 160U, 255U});
    InitializeGeometryComponent(geometry_components[1U],
                                1U,
                                22U,
                                vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F},
                                true,
                                false,
                                true,
                                false,
                                true,
                                vr::ecs::AppearanceShadingModel3D::unlit,
                                vr::ecs::Rgba8{170U, 225U, 255U, 210U});
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

        vr::geometry::GeometryAppearanceHostCreateInfo appearance_create_info{};
        appearance_create_info.reserve_appearance_count = 64U;
        geometry_appearance_host.Initialize(appearance_create_info);
        geometry_appearance_host_initialized = true;

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
        upload_image_11.pixels = pixels_appearance_11.data();
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
        upload_image_22.pixels = pixels_appearance_22.data();
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

        vr::geometry::GeometryAppearanceDesc appearance_11{};
        appearance_11.appearance_id = 11U;
        appearance_11.sampled_surface_binding.base_color_surface.surface_id = 101U;
        appearance_11.uv_scale_u = 1.0F;
        appearance_11.uv_scale_v = 1.0F;
        appearance_11.metallic_factor = 0.15F;
        appearance_11.roughness_factor = 0.42F;
        appearance_11.normal_scale = 1.0F;
        appearance_11.occlusion_strength = 0.95F;
        geometry_appearance_host.UpsertAppearance(appearance_11);

        vr::geometry::GeometryAppearanceDesc appearance_22{};
        appearance_22.appearance_id = 22U;
        appearance_22.sampled_surface_binding.base_color_surface.surface_id = 202U;
        appearance_22.uv_scale_u = 1.25F;
        appearance_22.uv_scale_v = 1.25F;
        appearance_22.uv_bias_u = -0.12F;
        appearance_22.uv_bias_v = 0.08F;
        appearance_22.flags = vr::geometry::geometry_appearance_flag_alpha_test;
        appearance_22.alpha_cutoff = 0.2F;
        appearance_22.metallic_factor = 0.78F;
        appearance_22.roughness_factor = 0.64F;
        appearance_22.normal_scale = 1.35F;
        appearance_22.occlusion_strength = 0.60F;
        geometry_appearance_host.UpsertAppearance(appearance_22);

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
        geometry_renderer.SetAppearanceHosts(&geometry_appearance_host, &geometry_image_host);
        const VkFormat external_depth_format = ResolveDepthTargetFormat(runtime.Context());
        vr::render::RenderTargetDesc external_depth_desc{};
        external_depth_desc.debug_name = "RuntimeGeometry3DExternalDepth";
        external_depth_desc.dimension = vr::render::RenderTargetDimension::image_2d;
        external_depth_desc.lifetime = vr::render::RenderTargetLifetime::persistent;
        external_depth_desc.scale_mode = vr::render::RenderTargetScaleMode::absolute;
        external_depth_desc.width = runtime.Swapchain().Extent().width;
        external_depth_desc.height = runtime.Swapchain().Extent().height;
        external_depth_desc.depth = 1U;
        external_depth_desc.format = external_depth_format;
        external_depth_desc.samples = VK_SAMPLE_COUNT_1_BIT;
        external_depth_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        external_depth_desc.aspect = vr::render::DepthStencilAspectMask(external_depth_format);
        const vr::render::RenderTargetHandle external_depth_target =
            runtime.RenderTarget().CreatePersistentTarget(runtime.Context(),
                                                          external_depth_desc,
                                                          runtime.Swapchain().Extent());
        vr::render::RenderTargetDepthOutputConfig depth_output_config{};
        depth_output_config.depth_target = external_depth_target;
        depth_output_config.final_state = vr::render::RenderTargetStateKind::depth_attachment;
        depth_output_config.use_explicit_load_op = true;
        depth_output_config.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_output_config.store_op = VK_ATTACHMENT_STORE_OP_STORE;
        depth_output_config.clear_depth_stencil = VkClearDepthStencilValue{1.0F, 0U};
        geometry_renderer.SetDepthTargetConfig(depth_output_config);
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
        std::uint32_t max_appearance_push_constant_updates = 0U;
        std::uint32_t max_appearance_sets = 0U;
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
            max_appearance_push_constant_updates =
                std::max(max_appearance_push_constant_updates,
                         renderer_stats.appearance_push_constant_update_count);
            max_appearance_sets = std::max(max_appearance_sets, renderer_stats.appearance_set_count);
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
        VR_CHECK(max_appearance_push_constant_updates > 0U);
        VR_CHECK(max_appearance_sets == 0U);
        VR_CHECK(observed_bounds_culling);
        VR_CHECK(max_culling_input_count == static_cast<std::uint32_t>(geometry_components.size()));
        VR_CHECK(max_culling_visible_count > 0U);
        VR_CHECK(geometry_resource_host.Stats().mesh_count > 0U);
        VR_CHECK(geometry_resource_host.Stats().reused_vertex_buffer_count > 0U);
        VR_CHECK(geometry_resource_host.Stats().reused_index_buffer_count > 0U);
        VR_CHECK(geometry_upload_host.Stats().upload_count > 0U);
        VR_CHECK(geometry_image_host.Stats().image_count >= 2U);
        VR_CHECK(geometry_appearance_host.Stats().appearance_count >= 2U);
        VR_CHECK(runtime.RenderTarget().ResolveView(external_depth_target).format == external_depth_format);

        geometry_renderer.Shutdown(runtime.Context());
        geometry_renderer_initialized = false;
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
        if (geometry_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            geometry_renderer.Shutdown(runtime.Context());
            geometry_renderer_initialized = false;
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

        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(RuntimeIntegration_geometry_renderer_3d_appearance_updates_reuse_descriptor_set,
             "integration;gpu;sdl;runtime;geometry;appearance") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceRuntimeSystem3D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;

    Runtime runtime{};
    vr::geometry::GeometryResourceHost geometry_resource_host{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryRenderer3D geometry_renderer{};

    bool runtime_initialized = false;
    bool geometry_resource_host_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_renderer_initialized = false;

    std::array<vr::geometry::GeometryMeshVertex, 4U> vertices{
        vr::geometry::GeometryMeshVertex{.position_x = -0.5F, .position_y = -0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 0.0F, .uv_v = 0.0F, .tangent_x = 1.0F, .tangent_y = 0.0F, .tangent_z = 0.0F, .tangent_w = 1.0F},
        vr::geometry::GeometryMeshVertex{.position_x = 0.5F, .position_y = -0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 1.0F, .uv_v = 0.0F, .tangent_x = 1.0F, .tangent_y = 0.0F, .tangent_z = 0.0F, .tangent_w = 1.0F},
        vr::geometry::GeometryMeshVertex{.position_x = 0.5F, .position_y = 0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 1.0F, .uv_v = 1.0F, .tangent_x = 1.0F, .tangent_y = 0.0F, .tangent_z = 0.0F, .tangent_w = 1.0F},
        vr::geometry::GeometryMeshVertex{.position_x = -0.5F, .position_y = 0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 0.0F, .uv_v = 1.0F, .tangent_x = 1.0F, .tangent_y = 0.0F, .tangent_z = 0.0F, .tangent_w = 1.0F}
    };
    std::array<std::uint32_t, 6U> indices{0U, 1U, 2U, 2U, 3U, 0U};
    std::array<vr::geometry::GeometrySubmeshRange, 1U> submeshes{
        vr::geometry::GeometrySubmeshRange{.first_index = 0U, .index_count = 6U, .vertex_offset = 0, .reserved0 = 0U}
    };

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

    Geometry3D geometry_component{};
    InitializeGeometryComponent(geometry_component,
                                1U,
                                0U,
                                vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F},
                                true,
                                true,
                                false,
                                true,
                                true,
                                vr::ecs::AppearanceShadingModel3D::lit_pbr,
                                vr::ecs::Rgba8{255U, 255U, 255U, 255U});

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F});
    TransformSystem3D::SetLocalScale(transform, vr::ecs::Float3{.x = 2.0F, .y = 2.0F, .z = 1.0F});
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    Bounds3D bounds_component{};
    BoundsSystem3D::Initialize(bounds_component);
    BoundsSystem3D::SetLocalAabb(bounds_component,
                                 vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                 vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F});
    (void)BoundsSystem3D::UpdateAligned(&bounds_component, &transform, 1U);

    Appearance3D appearance_component{};
    AppearanceSystem3D::Initialize(appearance_component);
    AppearanceSystem3D::SetBaseColor(appearance_component, vr::ecs::Rgba8{210U, 200U, 180U, 255U});
    AppearanceSystem3D::SetMetallic(appearance_component, 0.20F);
    AppearanceSystem3D::SetRoughness(appearance_component, 0.55F);
    AppearanceSystem3D::SetOcclusionStrength(appearance_component, 0.85F);
    AppearanceSystem3D::SetEmissiveIntensity(appearance_component, 0.0F);
    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim3> appearance_scratch{};
    (void)AppearanceRuntimeSystem3D::Build(&appearance_component, 1U, appearance_scratch);
    GeometrySystem3D::SetAppearanceHandle(geometry_component,
                                          appearance_component.runtime.gpu_record_handle);

    Camera3D camera{};
    CameraSystem3D::Initialize(camera);
    CameraSystem3D::SetAspectRatio(camera, 1280.0F / 720.0F);
    CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
    CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);

    Transform3D camera_transform{};
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.0F});
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_geometry_3d_appearance_reuse";
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
        resource_create_info.reserve_mesh_count = 8U;
        resource_create_info.reserve_submesh_count = 8U;
        resource_create_info.reserve_reusable_buffer_count = 8U;
        resource_create_info.max_reusable_vertex_buffer_count = 8U;
        resource_create_info.max_reusable_index_buffer_count = 8U;
        geometry_resource_host.Initialize(runtime.Context(), runtime.GpuMemory(), resource_create_info);
        geometry_resource_host_initialized = true;

        vr::geometry::GeometryUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_3d_instance_buffer_bytes = 64U * 1024U;
        geometry_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        geometry_upload_host_initialized = true;

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        geometry_resource_host.UploadMesh(runtime.Context(),
                                          runtime.Upload(),
                                          0U,
                                          0U,
                                          0U,
                                          mesh_upload_info);
        const vr::render::UploadEndFrameResult upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        geometry_resource_host.BeginFrame(runtime.Context(), 0U);

        vr::geometry::GeometryRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = 1U;
        renderer_create_info.reserve_instance_count = 8U;
        renderer_create_info.enable_depth = true;
        renderer_create_info.clear_swapchain = true;
        renderer_create_info.clear_color = {{0.05F, 0.07F, 0.10F, 1.0F}};
        renderer_create_info.directional_light_x = 0.0F;
        renderer_create_info.directional_light_y = -1.0F;
        renderer_create_info.directional_light_z = 0.0F;
        renderer_create_info.directional_light_intensity = 1.0F;
        geometry_renderer.Initialize(renderer_create_info);
        geometry_renderer_initialized = true;
        geometry_renderer.SetHosts(&geometry_resource_host, &geometry_upload_host);
        geometry_renderer.SetSceneData(&geometry_component,
                                       &transform,
                                       1U,
                                       &camera,
                                       &camera_transform,
                                       &bounds_component);
        geometry_renderer.SetAppearanceData(&appearance_component, 1U);

        std::uint32_t warmup_submitted_frames = 0U;
        constexpr std::uint32_t warmup_tick_limit = 8U;
        for (std::uint32_t tick_index = 0U;
             tick_index < warmup_tick_limit && runtime.IsRunning() && warmup_submitted_frames < 2U;
             ++tick_index) {
            CameraSystem3D::Update(camera, camera_transform);
            const Runtime::RuntimeTickResult tick_result = runtime.Tick(geometry_renderer);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++warmup_submitted_frames;
            }
            SDL_Delay(1U);
        }
        VR_REQUIRE(warmup_submitted_frames >= 2U);

        AppearanceSystem3D::SetBaseColor(appearance_component, vr::ecs::Rgba8{32U, 180U, 255U, 255U});
        AppearanceSystem3D::SetMetallic(appearance_component, 0.82F);
        AppearanceSystem3D::SetRoughness(appearance_component, 0.18F);
        constexpr std::uint32_t dirty_index = 0U;
        geometry_renderer.SetAppearanceDirtyHint(&dirty_index, 1U);

        CameraSystem3D::Update(camera, camera_transform);
        const Runtime::RuntimeTickResult changed_tick_result = runtime.Tick(geometry_renderer);
        VR_REQUIRE(changed_tick_result.render.code == vr::render::TickCode::Submitted ||
                   changed_tick_result.render.code == vr::render::TickCode::RecreateRequested);

        const vr::geometry::GeometryRenderer3DStats renderer_stats = geometry_renderer.Stats();
        VR_CHECK(renderer_stats.appearance_updated_record_count >= 1U);
        VR_CHECK(renderer_stats.uploaded_instance_count == 0U);
        VR_CHECK(renderer_stats.uploaded_bytes >= sizeof(vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3>));

        geometry_renderer.Shutdown(runtime.Context());
        geometry_renderer_initialized = false;
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

VR_TEST_CASE(RuntimeIntegration_geometry_renderer_3d_graph_only_record_path_smoke,
             "integration;gpu;sdl;runtime;geometry;render_graph") {
    Runtime runtime{};
    vr::geometry::GeometryResourceHost geometry_resource_host{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryImageHost geometry_image_host{};
    vr::geometry::GeometryAppearanceHost geometry_appearance_host{};
    vr::render::SceneRecorder3D recorder{};
    vr::geometry::GeometryRenderer3D geometry_renderer{};

    bool runtime_initialized = false;
    bool geometry_resource_host_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_image_host_initialized = false;
    bool geometry_appearance_host_initialized = false;
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
    std::array<std::uint32_t, 16U> pixels{};
    for (std::uint32_t y = 0U; y < 4U; ++y) {
        for (std::uint32_t x = 0U; x < 4U; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * 4U + x;
            const bool checker = ((x ^ y) & 1U) != 0U;
            pixels[index] = checker ? 0xFFBFA57BU : 0xFFF4E9D0U;
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

    Geometry3D geometry_component{};
    InitializeGeometryComponent(geometry_component,
                                1U,
                                11U,
                                vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F},
                                true,
                                true,
                                false,
                                false,
                                false,
                                vr::ecs::AppearanceShadingModel3D::lit_pbr,
                                vr::ecs::Rgba8{235U, 208U, 160U, 255U});
    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::UpdateHierarchy(&transform, 1U);
    Bounds3D bounds{};
    BoundsSystem3D::Initialize(bounds);
    BoundsSystem3D::SetLocalAabb(bounds,
                                 vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                 vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F});
    (void)BoundsSystem3D::UpdateAligned(&bounds, &transform, 1U);

    Camera3D camera{};
    CameraSystem3D::Initialize(camera);
    CameraSystem3D::SetAspectRatio(camera, 1280.0F / 720.0F);
    CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
    CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);
    Transform3D camera_transform{};
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.2F});
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    struct GraphAwareRecorder final {
        vr::render::SceneRecorder3D& inner;
        std::uint32_t legacy_record_count = 0U;

        void PrepareFrame(const vr::render::SceneRecorder3DPrepareView& prepare_view_) {
            inner.PrepareFrame(prepare_view_);
        }
        void BuildRenderGraph(vr::render_graph::RenderGraphBuilder& builder_,
                              const vr::render_graph::FrameSnapshot3D& snapshot_,
                              const vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_,
                              vr::render_graph::ResourceVersionHandle& color_chain_) {
            inner.BuildRenderGraph(builder_, snapshot_, build_result_, color_chain_);
        }
        void Record(const vr::render::FrameRecordContext& record_context_) {
            legacy_record_count += 1U;
            inner.Record(record_context_);
        }
        [[nodiscard]] const vr::render::RenderScenePacket3D* FramePacket() const noexcept {
            return inner.FramePacket();
        }
    } graph_recorder{.inner = recorder};

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_geometry_3d_graph_only";
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

        recorder.Initialize(BuildGeometryRecorderCreateInfo());
        recorder.BindRuntime(runtime);

        vr::geometry::GeometryResourceHostCreateInfo resource_create_info{};
        resource_create_info.reserve_mesh_count = 32U;
        resource_create_info.reserve_submesh_count = 64U;
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

        vr::geometry::GeometryAppearanceHostCreateInfo appearance_create_info{};
        appearance_create_info.reserve_appearance_count = 64U;
        geometry_appearance_host.Initialize(appearance_create_info);
        geometry_appearance_host_initialized = true;

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        geometry_resource_host.UploadMesh(runtime.Context(),
                                          runtime.Upload(),
                                          0U,
                                          0U,
                                          0U,
                                          mesh_upload_info);
        vr::geometry::GeometryImageUploadInfo upload_image{};
        upload_image.image_id = 101U;
        upload_image.pixels = pixels.data();
        upload_image.width = 4U;
        upload_image.height = 4U;
        upload_image.format = VK_FORMAT_R8G8B8A8_UNORM;
        upload_image.bytes_per_pixel = 4U;
        upload_image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        upload_image.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        upload_image.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        geometry_image_host.UploadImage(runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_image);
        const auto upload_end = runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        geometry_resource_host.BeginFrame(runtime.Context(), 0U);
        geometry_image_host.BeginFrame(runtime.Context(), 0U);

        vr::geometry::GeometryAppearanceDesc appearance{};
        appearance.appearance_id = 11U;
        appearance.sampled_surface_binding.base_color_surface.surface_id = 101U;
        appearance.uv_scale_u = 1.0F;
        appearance.uv_scale_v = 1.0F;
        geometry_appearance_host.UpsertAppearance(appearance);

        vr::geometry::GeometryRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = 1U;
        renderer_create_info.reserve_instance_count = 64U;
        renderer_create_info.enable_depth = true;
        geometry_renderer.Initialize(renderer_create_info);
        geometry_renderer_initialized = true;
        geometry_renderer.SetHosts(&geometry_resource_host, &geometry_upload_host);
        geometry_renderer.SetAppearanceHosts(&geometry_appearance_host, &geometry_image_host);
        geometry_renderer.SetSceneData(&geometry_component,
                                       &transform,
                                       1U,
                                       &camera,
                                       &camera_transform,
                                       &bounds);
        recorder.RegisterOpaqueSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::single);

        vr::render::RenderView3D main_view{};
        vr::render::RenderScenePacket3D main_scene_packet{};
        vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                           main_scene_packet,
                                                           camera,
                                                           camera_transform,
                                                           runtime.Swapchain().Extent(),
                                                           0U);
        recorder.SetFramePacket(&main_scene_packet);

        auto& service = runtime.Services().Get<vr::runtime::services::RenderGraphRuntimeService>();

        std::uint32_t submitted_frames = 0U;
        std::uint32_t executed_graph_frames = 0U;
        std::uint32_t max_descriptor_updates = 0U;
        std::uint64_t max_transient_descriptor_updates = 0U;
        bool saw_executable_scene_pass = false;
        constexpr std::uint32_t max_ticks = 6U;
        for (std::uint32_t tick_index = 0U; tick_index < max_ticks && runtime.IsRunning(); ++tick_index) {
            vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                               main_scene_packet,
                                                               camera,
                                                               camera_transform,
                                                               runtime.Swapchain().Extent(),
                                                               tick_index);
            recorder.SetFramePacket(&main_scene_packet);
            const Runtime::RuntimeTickResult tick_result = runtime.Tick(graph_recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }
            if (service.LastRecordStats().pass_count > 0U) {
                ++executed_graph_frames;
            }
            const vr::geometry::GeometryRenderer3DStats renderer_stats = geometry_renderer.Stats();
            max_descriptor_updates =
                std::max(max_descriptor_updates, renderer_stats.descriptor_set_update_count);
            max_transient_descriptor_updates = std::max(
                max_transient_descriptor_updates,
                runtime.Descriptor().Stats().transient_update_call_count);
            if (const auto* compiled_graph = service.TryGetCompiledGraph();
                compiled_graph != nullptr &&
                !compiled_graph->Passes().empty()) {
                saw_executable_scene_pass = saw_executable_scene_pass ||
                    (compiled_graph->Passes()[0].debug_name == "main_scene_pass" &&
                     compiled_graph->Passes()[0].executable);
            }
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(executed_graph_frames > 0U);
        VR_CHECK(graph_recorder.legacy_record_count == 0U);
        VR_CHECK(saw_executable_scene_pass);
        VR_CHECK(service.RecordExecutionEnabled());
        VR_CHECK(service.GraphOnlyRecordPathEnabled());
        VR_CHECK(max_descriptor_updates == 0U);
        VR_CHECK(max_transient_descriptor_updates > 0U);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    if (geometry_renderer_initialized) {
        geometry_renderer.Shutdown(runtime.Context());
    }
    if (geometry_appearance_host_initialized) {
        geometry_appearance_host.Shutdown();
    }
    if (geometry_image_host_initialized) {
        geometry_image_host.Shutdown(runtime.Context());
    }
    if (geometry_upload_host_initialized) {
        geometry_upload_host.Shutdown(runtime.Context());
    }
    if (geometry_resource_host_initialized) {
        geometry_resource_host.Shutdown(runtime.Context());
    }
    if (runtime_initialized) {
        runtime.Shutdown();
    }
}

VR_TEST_CASE(RuntimeIntegration_geometry_renderer_3d_bloom_post_stack_smoke,
             "integration;gpu;sdl;runtime;geometry;render_target;postprocess") {
    Runtime runtime{};
    vr::geometry::GeometryResourceHost geometry_resource_host{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryImageHost geometry_image_host{};
    vr::geometry::GeometryAppearanceHost geometry_appearance_host{};
    vr::render::SceneRecorder3D recorder{};
    vr::geometry::GeometryRenderer3D geometry_renderer{};
    vr::shadow::ShadowRenderer3D shadow_renderer{};
    vr::render::LightFrameCoordinator<vr::ecs::Dim3> light_frame_coordinator{};

    bool runtime_initialized = false;
    bool geometry_resource_host_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_image_host_initialized = false;
    bool geometry_appearance_host_initialized = false;
    bool geometry_renderer_initialized = false;
    bool shadow_renderer_initialized = false;

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
    std::array<std::uint32_t, 16U> pixels_appearance_11{};
    std::array<std::uint32_t, 16U> pixels_appearance_22{};
    for (std::uint32_t y = 0U; y < 4U; ++y) {
        for (std::uint32_t x = 0U; x < 4U; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * 4U + x;
            const bool checker = ((x ^ y) & 1U) != 0U;
            pixels_appearance_11[index] = checker ? 0xFFBFA57BU : 0xFFF4E9D0U;
            pixels_appearance_22[index] = checker ? 0xFF6FC6FFU : 0xFF2D456BU;
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
                                false,
                                true,
                                true,
                                vr::ecs::AppearanceShadingModel3D::lit_pbr,
                                vr::ecs::Rgba8{235U, 208U, 160U, 255U});
    InitializeGeometryComponent(geometry_components[1U],
                                1U,
                                22U,
                                vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F},
                                true,
                                false,
                                true,
                                false,
                                true,
                                vr::ecs::AppearanceShadingModel3D::unlit,
                                vr::ecs::Rgba8{170U, 225U, 255U, 210U});
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
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    std::array<Light3D, 1U> lights{};
    std::array<Transform3D, 1U> light_transforms{};
    InitializeLightComponent(lights[0U]);
    TransformSystem3D::Initialize(light_transforms[0U]);
    TransformSystem3D::SetLocalPosition(light_transforms[0U],
                                        vr::ecs::Float3{.x = 0.15F, .y = 1.45F, .z = 2.65F});
    TransformSystem3D::SetLocalRotationEulerXyz(light_transforms[0U], -0.52F, 0.02F, 0.0F);
    TransformSystem3D::UpdateHierarchy(light_transforms.data(),
                                       static_cast<std::uint32_t>(light_transforms.size()));
    light_frame_coordinator.SetLightData(lights.data(),
                                         light_transforms.data(),
                                         static_cast<std::uint32_t>(lights.size()));

    std::array<Shadow3D, 1U> shadows{};
    std::array<Transform3D, 1U> shadow_transforms{};
    InitializeShadowComponent(shadows[0U], 0U);
    TransformSystem3D::Initialize(shadow_transforms[0U]);
    TransformSystem3D::SetLocalPosition(shadow_transforms[0U],
                                        vr::ecs::Float3{.x = 0.15F, .y = 1.45F, .z = 2.65F});
    TransformSystem3D::SetLocalRotationEulerXyz(shadow_transforms[0U], -0.52F, 0.02F, 0.0F);
    TransformSystem3D::UpdateHierarchy(shadow_transforms.data(),
                                       static_cast<std::uint32_t>(shadow_transforms.size()));

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_geometry_3d_offscreen";
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

        recorder.Initialize(BuildGeometryRecorderCreateInfo());
        recorder.BindRuntime(runtime);
        recorder.BindLightFrameCoordinator(&light_frame_coordinator);

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

        vr::geometry::GeometryAppearanceHostCreateInfo appearance_create_info{};
        appearance_create_info.reserve_appearance_count = 64U;
        geometry_appearance_host.Initialize(appearance_create_info);
        geometry_appearance_host_initialized = true;

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        geometry_resource_host.UploadMesh(runtime.Context(),
                                          runtime.Upload(),
                                          0U,
                                          0U,
                                          0U,
                                          mesh_upload_info);

        vr::geometry::GeometryImageUploadInfo upload_image_11{};
        upload_image_11.image_id = 101U;
        upload_image_11.pixels = pixels_appearance_11.data();
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
        upload_image_22.pixels = pixels_appearance_22.data();
        geometry_image_host.UploadImage(runtime.Context(),
                                        runtime.Upload(),
                                        0U,
                                        0U,
                                        0U,
                                        upload_image_22);
        const vr::render::UploadEndFrameResult upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        geometry_resource_host.BeginFrame(runtime.Context(), 0U);
        geometry_image_host.BeginFrame(runtime.Context(), 0U);

        vr::geometry::GeometryAppearanceDesc appearance_11{};
        appearance_11.appearance_id = 11U;
        appearance_11.sampled_surface_binding.base_color_surface.surface_id = 101U;
        appearance_11.uv_scale_u = 1.0F;
        appearance_11.uv_scale_v = 1.0F;
        appearance_11.metallic_factor = 0.15F;
        appearance_11.roughness_factor = 0.42F;
        appearance_11.normal_scale = 1.0F;
        appearance_11.occlusion_strength = 0.95F;
        geometry_appearance_host.UpsertAppearance(appearance_11);

        vr::geometry::GeometryAppearanceDesc appearance_22{};
        appearance_22.appearance_id = 22U;
        appearance_22.sampled_surface_binding.base_color_surface.surface_id = 202U;
        appearance_22.uv_scale_u = 1.25F;
        appearance_22.uv_scale_v = 1.25F;
        appearance_22.uv_bias_u = -0.12F;
        appearance_22.uv_bias_v = 0.08F;
        appearance_22.flags = vr::geometry::geometry_appearance_flag_alpha_test;
        appearance_22.alpha_cutoff = 0.2F;
        appearance_22.metallic_factor = 0.78F;
        appearance_22.roughness_factor = 0.64F;
        appearance_22.normal_scale = 1.35F;
        appearance_22.occlusion_strength = 0.60F;
        geometry_appearance_host.UpsertAppearance(appearance_22);

        vr::geometry::GeometryRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(geometry_components.size());
        renderer_create_info.reserve_instance_count = 128U;
        renderer_create_info.enable_depth = true;
        renderer_create_info.clear_swapchain = false;
        renderer_create_info.clear_color = {{0.05F, 0.07F, 0.10F, 1.0F}};
        renderer_create_info.directional_light_x = 0.6F;
        renderer_create_info.directional_light_y = -0.8F;
        renderer_create_info.directional_light_z = 0.3F;
        renderer_create_info.directional_light_intensity = 1.0F;
        geometry_renderer.Initialize(renderer_create_info);
        geometry_renderer_initialized = true;
        geometry_renderer.SetHosts(&geometry_resource_host, &geometry_upload_host);
        geometry_renderer.SetAppearanceHosts(&geometry_appearance_host, &geometry_image_host);
        geometry_renderer.SetSceneData(geometry_components.data(),
                                       transforms.data(),
                                       static_cast<std::uint32_t>(geometry_components.size()),
                                       &camera,
                                       &camera_transform,
                                       bounds_components.data());
        vr::shadow::ShadowRenderer3DCreateInfo shadow_renderer_create_info{};
        shadow_renderer_create_info.reserve_shadow_count =
            static_cast<std::uint32_t>(shadows.size());
        shadow_renderer_create_info.reserve_caster_count =
            static_cast<std::uint32_t>(geometry_components.size());
        shadow_renderer.Initialize(shadow_renderer_create_info);
        shadow_renderer_initialized = true;
        shadow_renderer.SetHosts(&geometry_resource_host);
        shadow_renderer.SetSceneData(shadows.data(),
                                     shadow_transforms.data(),
                                     static_cast<std::uint32_t>(shadows.size()),
                                     &camera,
                                     bounds_components.data());
        shadow_renderer.SetGeometryData(geometry_components.data(),
                                        transforms.data(),
                                        static_cast<std::uint32_t>(geometry_components.size()));
        recorder.RegisterShadowRenderer(shadow_renderer);
        recorder.RegisterOpaqueSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::single);

        vr::render::RenderView3D main_view{};
        vr::render::RenderScenePacket3D main_scene_packet{};
        vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                           main_scene_packet,
                                                           camera,
                                                           camera_transform,
                                                           runtime.Swapchain().Extent(),
                                                           0U);
        recorder.SetFramePacket(&main_scene_packet);

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_draw_batches = 0U;
        std::uint32_t max_instances = 0U;
        std::uint32_t max_depth_test_batches = 0U;
        std::uint32_t max_depth_write_batches = 0U;
        std::uint32_t max_descriptor_updates = 0U;
        std::uint32_t max_appearance_push_constant_updates = 0U;
        std::uint32_t max_appearance_sets = 0U;
        std::uint32_t max_culling_input_count = 0U;
        std::uint32_t max_culling_visible_count = 0U;
        std::uint32_t max_prefilter_draw_calls = 0U;
        std::uint32_t max_blur_draw_calls = 0U;
        std::uint32_t max_combine_draw_calls = 0U;
        std::uint32_t max_bloom_descriptor_updates = 0U;
        std::uint32_t max_visible_light_count = 0U;
        std::uint32_t max_shadow_view_count = 0U;
        std::uint32_t max_linked_light_count = 0U;
        std::uint32_t max_light_descriptor_binds = 0U;
        std::uint32_t max_ibl_descriptor_binds = 0U;
        std::uint32_t max_shadow_draw_calls = 0U;
        std::uint32_t max_shadow_atlas_passes = 0U;
        std::uint64_t max_transient_descriptor_updates = 0U;
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
            TransformSystem3D::SetLocalRotationEulerXyz(light_transforms[0U],
                                                        -0.52F,
                                                        0.06F * static_cast<float>(tick_index),
                                                        0.0F);
            TransformSystem3D::SetLocalRotationEulerXyz(shadow_transforms[0U],
                                                        -0.52F,
                                                        0.06F * static_cast<float>(tick_index),
                                                        0.0F);
            TransformSystem3D::UpdateHierarchy(light_transforms.data(),
                                               static_cast<std::uint32_t>(light_transforms.size()));
            TransformSystem3D::UpdateHierarchy(shadow_transforms.data(),
                                               static_cast<std::uint32_t>(shadow_transforms.size()));
            (void)BoundsSystem3D::UpdateAligned(bounds_components.data(),
                                                transforms.data(),
                                                static_cast<std::uint32_t>(bounds_components.size()));
            const std::uint32_t dirty_index = 0U;
            light_frame_coordinator.SetTransformDirtyHint(&dirty_index, 1U);
            shadow_renderer.SetTransformDirtyHint(&dirty_index, 1U);
            vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                               main_scene_packet,
                                                               camera,
                                                               camera_transform,
                                                               runtime.Swapchain().Extent(),
                                                               tick_index);
            recorder.SetFramePacket(&main_scene_packet);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            const vr::geometry::GeometryRenderer3DStats renderer_stats = geometry_renderer.Stats();
            const auto bloom_stats = recorder.PostStack().Stats();
            max_draw_calls = std::max(max_draw_calls, renderer_stats.draw_call_count);
            max_draw_batches = std::max(max_draw_batches, renderer_stats.draw_batch_count);
            max_instances = std::max(max_instances, renderer_stats.instance_count);
            max_depth_test_batches = std::max(max_depth_test_batches, renderer_stats.depth_test_batch_count);
            max_depth_write_batches = std::max(max_depth_write_batches, renderer_stats.depth_write_batch_count);
            max_descriptor_updates = std::max(max_descriptor_updates, renderer_stats.descriptor_set_update_count);
            max_appearance_push_constant_updates =
                std::max(max_appearance_push_constant_updates,
                         renderer_stats.appearance_push_constant_update_count);
            max_appearance_sets = std::max(max_appearance_sets, renderer_stats.appearance_set_count);
            max_culling_input_count = std::max(max_culling_input_count, renderer_stats.culling_input_count);
            max_culling_visible_count = std::max(max_culling_visible_count, renderer_stats.culling_visible_count);
            max_prefilter_draw_calls = std::max(max_prefilter_draw_calls,
                                                bloom_stats.prefilter_draw_call_count);
            max_blur_draw_calls = std::max(max_blur_draw_calls,
                                           bloom_stats.blur_draw_call_count);
            max_combine_draw_calls = std::max(max_combine_draw_calls,
                                              bloom_stats.combine_draw_call_count);
            max_bloom_descriptor_updates = std::max(max_bloom_descriptor_updates,
                                                    bloom_stats.descriptor_set_update_count);
            max_transient_descriptor_updates = std::max(
                max_transient_descriptor_updates,
                runtime.Descriptor().Stats().transient_update_call_count);
            max_visible_light_count = std::max(max_visible_light_count,
                                               renderer_stats.visible_light_count);
            max_shadow_view_count = std::max(max_shadow_view_count,
                                             renderer_stats.shadow_view_count);
            max_linked_light_count = std::max(max_linked_light_count,
                                              renderer_stats.light_shadow_linked_count);
            max_light_descriptor_binds = std::max(max_light_descriptor_binds,
                                                  renderer_stats.light_descriptor_set_bind_count);
            max_ibl_descriptor_binds = std::max(max_ibl_descriptor_binds,
                                                renderer_stats.ibl_descriptor_set_bind_count);
            max_shadow_draw_calls = std::max(max_shadow_draw_calls,
                                             shadow_renderer.Stats().draw_call_count);
            max_shadow_atlas_passes = std::max(max_shadow_atlas_passes,
                                               shadow_renderer.Stats().atlas_layer_draw_pass_count);
            observed_bounds_culling = observed_bounds_culling || renderer_stats.used_bounds_culling;
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_draw_calls > 0U);
        VR_CHECK(max_draw_batches > 0U);
        VR_CHECK(max_instances > 0U);
        VR_CHECK(max_depth_test_batches > 0U);
        VR_CHECK(max_depth_write_batches > 0U);
        VR_CHECK(max_appearance_push_constant_updates > 0U);
        VR_CHECK(max_appearance_sets == 0U);
        VR_CHECK(max_prefilter_draw_calls > 0U);
        VR_CHECK(max_blur_draw_calls > 0U);
        VR_CHECK(max_combine_draw_calls > 0U);
        VR_CHECK(max_bloom_descriptor_updates == 0U);
        VR_CHECK(max_visible_light_count > 0U);
        VR_CHECK(max_shadow_view_count > 0U);
        VR_CHECK(max_linked_light_count > 0U);
        VR_CHECK(max_light_descriptor_binds > 0U);
        VR_CHECK(max_ibl_descriptor_binds > 0U);
        VR_CHECK(max_shadow_draw_calls > 0U);
        VR_CHECK(max_shadow_atlas_passes > 0U);
        VR_CHECK(observed_bounds_culling);
        VR_CHECK(max_culling_input_count == static_cast<std::uint32_t>(geometry_components.size()));
        VR_CHECK(max_culling_visible_count > 0U);
        VR_CHECK(geometry_resource_host.Stats().mesh_count > 0U);
        VR_CHECK(geometry_upload_host.Stats().upload_count > 0U);
        VR_CHECK(geometry_image_host.Stats().image_count >= 2U);
        VR_CHECK(geometry_appearance_host.Stats().appearance_count >= 2U);
        const bool graph_only_record_active = vr::test::IsGraphOnlyScene3DRecordActive(runtime);
        if (graph_only_record_active) {
            VR_CHECK(max_descriptor_updates == 0U);
            VR_CHECK(max_transient_descriptor_updates > 0U);
        } else {
            VR_CHECK(max_descriptor_updates > 0U);
        }
        VR_CHECK(recorder.Stats().pre_scene_renderer_count == 1U);
        VR_CHECK(recorder.Stats().frame_packet_prepare_count > 0U);
        VR_CHECK(graph_only_record_active
                     ? (recorder.Stats().frame_packet_record_count == 0U)
                     : (recorder.Stats().frame_packet_record_count > 0U));
        VR_CHECK(recorder.ActiveView() == &main_view);
        VR_CHECK(recorder.ActiveView() != nullptr);
        VR_CHECK(recorder.ActiveView()->camera == &camera);
        VR_CHECK(runtime.TargetPool().Stats().acquire_count > 0U);
        VR_CHECK(runtime.TargetPool().Stats().reuse_hit_count > 0U);
        VR_CHECK(runtime.Ibl().Stats().prepared_frame_count > 0U);
        if (!graph_only_record_active) {
            VR_CHECK(runtime.RenderTarget().ResolveView(recorder.PostStack().Targets().ColorTarget()).state ==
                     vr::render::RenderTargetStateKind::shader_read);
            VR_CHECK(runtime.RenderTarget().ResolveView(recorder.PostStack().Targets().DepthTarget()).state ==
                     vr::render::RenderTargetStateKind::depth_attachment);
        }

        recorder.Shutdown(runtime.Context());
        shadow_renderer.Shutdown(runtime.Context());
        shadow_renderer_initialized = false;
        geometry_renderer.Shutdown(runtime.Context());
        geometry_renderer_initialized = false;
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
        if (runtime_initialized && runtime.IsInitialized() && recorder.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
        }
        if (shadow_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            shadow_renderer.Shutdown(runtime.Context());
            shadow_renderer_initialized = false;
        }
        if (geometry_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            geometry_renderer.Shutdown(runtime.Context());
            geometry_renderer_initialized = false;
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

        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

} // namespace


