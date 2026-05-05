#include "support/test_framework.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/geometry_mesh_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/light_system.hpp"
#include "vr/ecs/system/shadow_system.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/geometry/geometry_material_host.hpp"
#include "vr/geometry/geometry_renderer_3d.hpp"
#include "vr/geometry/geometry_resource_host.hpp"
#include "vr/geometry/geometry_upload_host.hpp"
#include "vr/render/animation_frame_coordinator.hpp"
#include "vr/render/ibl_bake_coordinator.hpp"
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render/skybox_renderer.hpp"
#include "vr/shadow/shadow_renderer_3d.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/surface/surface_renderer_3d.hpp"
#include "vr/surface/surface_upload_host.hpp"
#include "vr/text/text_renderer_3d.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
using Light3D = vr::ecs::Light<vr::ecs::Dim3>;
using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
using Text3D = vr::ecs::Text<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;

using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
using LightSystem3D = vr::ecs::LightSystem<vr::ecs::Dim3>;
using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;
using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
using GeometryMeshSystem3D = vr::ecs::GeometryMeshSystem;

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
    constexpr std::array<std::string_view, 17U> patterns{
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
        "synchronization2",
        "ft_new_face",
        "freetypehost::registerface"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr std::uint32_t PackRgba8(std::uint8_t r_,
                                                std::uint8_t g_,
                                                std::uint8_t b_,
                                                std::uint8_t a_) noexcept {
    return static_cast<std::uint32_t>(r_) |
           (static_cast<std::uint32_t>(g_) << 8U) |
           (static_cast<std::uint32_t>(b_) << 16U) |
           (static_cast<std::uint32_t>(a_) << 24U);
}

void FillCheckerTexture(std::uint32_t* pixels_,
                        std::uint32_t width_,
                        std::uint32_t height_,
                        std::uint32_t color_a_,
                        std::uint32_t color_b_) {
    if (pixels_ == nullptr || width_ == 0U || height_ == 0U) {
        return;
    }
    for (std::uint32_t y = 0U; y < height_; ++y) {
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            const bool odd = (((x >> 3U) ^ (y >> 3U)) & 1U) != 0U;
            pixels_[index] = odd ? color_a_ : color_b_;
        }
    }
}

void FillGradientTexture(std::uint32_t* pixels_,
                         std::uint32_t width_,
                         std::uint32_t height_) {
    if (pixels_ == nullptr || width_ == 0U || height_ == 0U) {
        return;
    }
    for (std::uint32_t y = 0U; y < height_; ++y) {
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            const float fx = static_cast<float>(x) /
                             static_cast<float>(std::max(width_, 1U) - 1U);
            const float fy = static_cast<float>(y) /
                             static_cast<float>(std::max(height_, 1U) - 1U);
            const std::uint8_t r = static_cast<std::uint8_t>(45.0F + 185.0F * fx);
            const std::uint8_t g = static_cast<std::uint8_t>(55.0F + 150.0F * (1.0F - fy));
            const std::uint8_t b = static_cast<std::uint8_t>(110.0F + 120.0F * fy);
            pixels_[index] = PackRgba8(r, g, b, 255U);
        }
    }
}

void FillHdrEnvironmentEquirect(vr::ecs::Float4* pixels_,
                                std::uint32_t width_,
                                std::uint32_t height_) {
    if (pixels_ == nullptr || width_ == 0U || height_ == 0U) {
        return;
    }

    for (std::uint32_t y = 0U; y < height_; ++y) {
        const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(height_);
        const float upper_t = std::clamp(1.0F - v * 1.25F, 0.0F, 1.0F);
        const float horizon_t = std::exp(-32.0F * (v - 0.48F) * (v - 0.48F));
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(width_);
            const float wrapped_du = std::fabs(u - 0.16F);
            const float sun_du = std::min(wrapped_du, 1.0F - wrapped_du);
            const float sun_dv = v - 0.32F;
            const float sun_t = std::exp(-(sun_du * sun_du * 640.0F + sun_dv * sun_dv * 900.0F));
            auto& pixel = pixels_[static_cast<std::size_t>(y) * width_ + x];
            pixel.x = 0.02F + 0.18F * (1.0F - upper_t) + 0.45F * horizon_t + 11.0F * sun_t;
            pixel.y = 0.03F + 0.30F * horizon_t + 0.62F * upper_t + 8.5F * sun_t;
            pixel.z = 0.08F + 1.85F * upper_t + 4.5F * sun_t;
            pixel.w = 1.0F;
        }
    }
}

[[nodiscard]] std::string FindTestFontPath() {
    namespace fs = std::filesystem;

    constexpr std::array<const char*, 6U> candidate_paths{
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/msyh.ttc"
    };

    for (const char* path : candidate_paths) {
        const fs::path candidate(path);
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate.string();
        }
    }
    return {};
}

void InitializeGeometryComponent(Geometry3D& component_,
                                 std::uint32_t geometry_id_,
                                 std::uint32_t material_id_,
                                 vr::ecs::Rgba8 albedo_) {
    GeometrySystem3D::Initialize(component_);
    GeometrySystem3D::SetRuntimeRoute(component_, geometry_id_, material_id_, 0U);
    GeometrySystem3D::SetBounds(component_,
                                vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F});
    component_.style.depth_test = 1U;
    component_.style.depth_write = 1U;
    component_.style.cast_shadow = 1U;
    component_.style.shading_model = vr::ecs::Geometry3DShadingModel::lit;
    component_.style.albedo_color = albedo_;
    component_.mesh.submesh_index = 0U;
    component_.mesh.lod_index = 0U;
    component_.mesh.flags = 0U;
}

void InitializeLightComponent(Light3D& component_) {
    LightSystem3D::Initialize(component_);
    LightSystem3D::SetLightKind(component_, vr::ecs::LightKind::spot);
    LightSystem3D::SetColor(component_, vr::ecs::Rgba8{255U, 244U, 224U, 255U});
    LightSystem3D::SetIntensity(component_, 1850.0F);
    LightSystem3D::SetRange(component_, 18.0F);
    LightSystem3D::SetConeAngles(component_, 0.30F, 0.72F);
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
    ShadowSystem3D::SetAtlasNamespace(component_, 9U);
    ShadowSystem3D::SetFaceCount(component_, 1U);
}

void InitializeSurface3DComponent(Surface3D& component_,
                                  std::uint32_t texture_id_,
                                  std::uint32_t sampler_id_,
                                  std::uint16_t depth_bin_,
                                  vr::ecs::Rgba8 tint_color_) {
    SurfaceSystem3D::Initialize(component_);
    SurfaceSystem3D::SetTextureRoute(component_, texture_id_, sampler_id_, 0U, 0U);
    SurfaceSystem3D::SetDepthBin(component_, depth_bin_);
    SurfaceSystem3D::SetRenderPassHint(component_, vr::ecs::SurfaceRenderPassHint::transparent);
    SurfaceSystem3D::SetDepthTest(component_, true);
    SurfaceSystem3D::SetDepthWrite(component_, false);
    SurfaceSystem3D::SetDoubleSided(component_, true);
    SurfaceSystem3D::SetTintColor(component_, tint_color_);
    SurfaceSystem3D::SetOpacity(component_, 0.92F);
}

void InitializeText3DComponent(Text3D& component_,
                               std::uint32_t font_id_,
                               std::uint32_t material_id_,
                               std::string_view text_) {
    TextSystem3D::Initialize(component_);
    TextSystem3D::SetRuntimeRoute(component_, font_id_, material_id_, 0U, 0U);
    TextSystem3D::SetColor(component_, vr::ecs::Rgba8{245U, 248U, 255U, 255U});
    TextSystem3D::SetOutlineEnabled(component_, true);
    TextSystem3D::SetOutlineWidthPx(component_, 1U);
    TextSystem3D::SetOutlineColor(component_, vr::ecs::Rgba8{15U, 18U, 26U, 255U});
    TextSystem3D::SetBillboard(component_, false);
    TextSystem3D::SetWorldSize(component_, 0.42F);
    TextSystem3D::SetDepthWrite(component_, false);
    (void)TextSystem3D::SetText(component_, text_);
}

[[nodiscard]] vr::render::SceneRecorder3DCreateInfo BuildUnifiedSceneRecorderCreateInfo() noexcept {
    vr::render::SceneRecorder3DCreateInfo create_info{};
    create_info.scene_target.color_debug_name = "RuntimeUnified3DSceneColor";
    create_info.scene_target.depth_debug_name = "RuntimeUnified3DSceneDepth";
    create_info.scene_target.enable_depth = true;
    create_info.scene_target.color_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.depth_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.clear_color = VkClearColorValue{{0.035F, 0.040F, 0.060F, 1.0F}};
    create_info.bloom.clear_swapchain = true;
    create_info.bloom.enable_reinhard_tonemap = true;
    create_info.bloom.exposure = 1.0F;
    create_info.bloom.apply_manual_gamma = false;
    create_info.bloom.bloom_threshold = 0.70F;
    create_info.bloom.bloom_knee = 0.45F;
    create_info.bloom.bloom_intensity = 0.95F;
    create_info.bloom.blur_filter_scale = 1.10F;
    create_info.bloom.downsample_scale = 0.5F;
    create_info.reserve_scene_renderer_count = 3U;
    create_info.reserve_overlay_renderer_count = 0U;
    return create_info;
}

VR_TEST_CASE(RuntimeIntegration_unified_scene_3d_bloom_post_stack_smoke,
             "integration;gpu;sdl;runtime;render_target;scene3d;postprocess") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for unified runtime 3D scene integration test.");
    }

    Runtime runtime{};
    vr::geometry::GeometryResourceHost geometry_resource_host{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryImageHost geometry_image_host{};
    vr::geometry::GeometryMaterialHost geometry_material_host{};
    vr::surface::SurfaceUploadHost surface_upload_host{};
    vr::surface::SurfaceImageHost surface_image_host{};
    vr::render::SceneRecorder3D recorder{};
    vr::render::SkyboxRenderer skybox_renderer{};
    vr::geometry::GeometryRenderer3D geometry_renderer{};
    vr::shadow::ShadowRenderer3D shadow_renderer{};
    vr::render::LightFrameCoordinator<vr::ecs::Dim3> light_frame_coordinator{};
    vr::surface::SurfaceRenderer3D surface_renderer{};
    vr::text::TextRenderer3D text_renderer{};

    bool runtime_initialized = false;
    bool geometry_resource_host_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_image_host_initialized = false;
    bool geometry_material_host_initialized = false;
    bool surface_upload_host_initialized = false;
    bool surface_image_host_initialized = false;
    bool skybox_renderer_initialized = false;
    bool geometry_renderer_initialized = false;
    bool shadow_renderer_initialized = false;
    bool surface_renderer_initialized = false;
    bool text_renderer_initialized = false;

    constexpr std::uint32_t texture_width = 64U;
    constexpr std::uint32_t texture_height = 64U;
    constexpr std::uint32_t ibl_equirect_width = 32U;
    constexpr std::uint32_t ibl_equirect_height = 16U;
    std::array<std::uint32_t, texture_width * texture_height> geometry_pixels{};
    std::array<std::uint32_t, texture_width * texture_height> surface_pixels{};
    std::array<vr::ecs::Float4, ibl_equirect_width * ibl_equirect_height> ibl_equirect_pixels{};
    FillCheckerTexture(geometry_pixels.data(),
                       texture_width,
                       texture_height,
                       PackRgba8(242U, 210U, 164U, 255U),
                       PackRgba8(108U, 74U, 54U, 255U));
    FillGradientTexture(surface_pixels.data(), texture_width, texture_height);
    FillHdrEnvironmentEquirect(ibl_equirect_pixels.data(),
                               ibl_equirect_width,
                               ibl_equirect_height);

    std::array<vr::geometry::GeometryMeshVertex, 4U> mesh_vertices{
        vr::geometry::GeometryMeshVertex{.position_x = -0.5F, .position_y = -0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 0.0F, .uv_v = 0.0F, .morph0_position_delta_z = -0.18F, .morph1_position_delta_x = -0.08F, .joint_index0 = 0U, .joint_weight0 = 1.0F},
        vr::geometry::GeometryMeshVertex{.position_x = 0.5F, .position_y = -0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 1.0F, .uv_v = 0.0F, .morph0_position_delta_z = 0.18F, .morph1_position_delta_x = 0.08F, .joint_index0 = 0U, .joint_weight0 = 1.0F},
        vr::geometry::GeometryMeshVertex{.position_x = 0.5F, .position_y = 0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 1.0F, .uv_v = 1.0F, .morph0_position_delta_z = 0.28F, .morph1_position_delta_x = 0.12F, .joint_index0 = 0U, .joint_weight0 = 1.0F},
        vr::geometry::GeometryMeshVertex{.position_x = -0.5F, .position_y = 0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 0.0F, .uv_v = 1.0F, .morph0_position_delta_z = -0.28F, .morph1_position_delta_x = -0.12F, .joint_index0 = 0U, .joint_weight0 = 1.0F}
    };
    std::array<std::uint32_t, 6U> mesh_indices{0U, 1U, 2U, 2U, 3U, 0U};
    std::array<vr::geometry::GeometrySubmeshRange, 1U> mesh_submeshes{
        vr::geometry::GeometrySubmeshRange{.first_index = 0U, .index_count = 6U, .vertex_offset = 0, .reserved0 = 0U}
    };

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_unified_scene_3d";
        create_info.platform.window.width = 800;
        create_info.platform.window.height = 450;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.modules.enable_ibl_bake_host = true;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        recorder.Initialize(BuildUnifiedSceneRecorderCreateInfo());
        recorder.BindRuntime(runtime);
        recorder.BindLightFrameCoordinator(&light_frame_coordinator);

        vr::render::SkyboxRendererCreateInfo skybox_renderer_create_info{};
        skybox_renderer_create_info.clear_swapchain = false;
        skybox_renderer.Initialize(skybox_renderer_create_info);
        skybox_renderer_initialized = true;

        vr::geometry::GeometryResourceHostCreateInfo geometry_resource_create_info{};
        geometry_resource_create_info.reserve_mesh_count = 16U;
        geometry_resource_create_info.reserve_submesh_count = 32U;
        geometry_resource_create_info.reserve_reusable_buffer_count = 8U;
        geometry_resource_create_info.max_reusable_vertex_buffer_count = 16U;
        geometry_resource_create_info.max_reusable_index_buffer_count = 16U;
        geometry_resource_host.Initialize(runtime.Context(), runtime.GpuMemory(), geometry_resource_create_info);
        geometry_resource_host_initialized = true;

        vr::geometry::GeometryUploadHostCreateInfo geometry_upload_create_info{};
        geometry_upload_create_info.frames_in_flight = 2U;
        geometry_upload_create_info.initial_3d_instance_buffer_bytes = 256U * 1024U;
        geometry_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), geometry_upload_create_info);
        geometry_upload_host_initialized = true;

        vr::geometry::GeometryImageHostCreateInfo geometry_image_create_info{};
        geometry_image_create_info.reserve_image_count = 8U;
        geometry_image_create_info.reserve_retired_image_count = 8U;
        geometry_image_host.Initialize(runtime.Context(), runtime.GpuMemory(), geometry_image_create_info);
        geometry_image_host_initialized = true;

        vr::geometry::GeometryMaterialHostCreateInfo geometry_material_create_info{};
        geometry_material_create_info.reserve_material_count = 16U;
        geometry_material_host.Initialize(geometry_material_create_info);
        geometry_material_host_initialized = true;

        vr::surface::SurfaceUploadHostCreateInfo surface_upload_create_info{};
        surface_upload_create_info.frames_in_flight = 2U;
        surface_upload_create_info.initial_3d_instance_buffer_bytes = 256U * 1024U;
        surface_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), surface_upload_create_info);
        surface_upload_host_initialized = true;

        vr::surface::SurfaceImageHostCreateInfo surface_image_create_info{};
        surface_image_create_info.reserve_image_count = 8U;
        surface_image_create_info.reserve_retired_image_count = 8U;
        surface_image_host.Initialize(runtime.Context(), runtime.GpuMemory(), surface_image_create_info);
        surface_image_host_initialized = true;

        runtime.Upload().BeginFrame(runtime.Context(), 0U);

        vr::geometry::GeometryMeshUploadInfo mesh_upload_info{};
        mesh_upload_info.geometry_id = 1U;
        mesh_upload_info.vertices = mesh_vertices.data();
        mesh_upload_info.vertex_count = static_cast<std::uint32_t>(mesh_vertices.size());
        mesh_upload_info.indices = mesh_indices.data();
        mesh_upload_info.index_count = static_cast<std::uint32_t>(mesh_indices.size());
        mesh_upload_info.submeshes = mesh_submeshes.data();
        mesh_upload_info.submesh_count = static_cast<std::uint32_t>(mesh_submeshes.size());
        mesh_upload_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        mesh_upload_info.bounds_min = vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F};
        mesh_upload_info.bounds_max = vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F};
        geometry_resource_host.UploadMesh(runtime.Context(),
                                          runtime.Upload(),
                                          0U,
                                          0U,
                                          0U,
                                          mesh_upload_info);

        vr::geometry::GeometryImageUploadInfo geometry_image_upload{};
        geometry_image_upload.image_id = 101U;
        geometry_image_upload.pixels = geometry_pixels.data();
        geometry_image_upload.width = texture_width;
        geometry_image_upload.height = texture_height;
        geometry_image_upload.format = VK_FORMAT_R8G8B8A8_UNORM;
        geometry_image_upload.bytes_per_pixel = 4U;
        geometry_image_upload.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        geometry_image_upload.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        geometry_image_upload.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        geometry_image_host.UploadImage(runtime.Context(),
                                        runtime.Upload(),
                                        0U,
                                        0U,
                                        0U,
                                        geometry_image_upload);

        vr::surface::SurfaceImageUploadInfo surface_image_upload{};
        surface_image_upload.image_id = 6101U;
        surface_image_upload.pixels = surface_pixels.data();
        surface_image_upload.width = texture_width;
        surface_image_upload.height = texture_height;
        surface_image_upload.format = VK_FORMAT_R8G8B8A8_UNORM;
        surface_image_upload.bytes_per_pixel = 4U;
        surface_image_upload.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        surface_image_upload.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        surface_image_upload.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        surface_image_host.UploadImage(runtime.Context(),
                                       runtime.Upload(),
                                       0U,
                                       0U,
                                       0U,
                                       surface_image_upload);

        const auto upload_end = runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        geometry_resource_host.BeginFrame(runtime.Context(), 0U);
        geometry_image_host.BeginFrame(runtime.Context(), 0U);
        surface_image_host.BeginFrame(runtime.Context(), 0U);

        vr::geometry::GeometryMaterialDesc geometry_material{};
        geometry_material.material_id = 11U;
        geometry_material.image_id = 101U;
        geometry_material.uv_scale_u = 1.0F;
        geometry_material.uv_scale_v = 1.0F;
        geometry_material_host.UpsertMaterial(geometry_material);

        vr::resource::SamplerId surface_sampler_id{};
        {
            vr::resource::SamplerDesc sampler_desc{};
            sampler_desc.mag_filter = VK_FILTER_LINEAR;
            sampler_desc.min_filter = VK_FILTER_LINEAR;
            sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_desc.min_lod = 0.0F;
            sampler_desc.max_lod = 0.0F;
            surface_sampler_id = runtime.Sampler().RegisterSampler(runtime.Context(), sampler_desc);
        }

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 32U;
        const vr::text::FontFaceId base_face_id = runtime.FreeType().RegisterFace(face_create_info);
        runtime.GlyphAtlas().MapFont(1U, base_face_id);

        Geometry3D geometry_component{};
        InitializeGeometryComponent(geometry_component, 1U, 11U, vr::ecs::Rgba8{235U, 214U, 170U, 255U});
        GeometryMeshSystem3D::EnableVertexDeformShader(geometry_component, true);
        GeometryMeshSystem3D::EnableMorphTargets(geometry_component, true);
        GeometryMeshSystem3D::EnableSkeletalRootMotion(geometry_component, true);
        GeometryMeshSystem3D::EnableSkeletalSkinning(geometry_component, true);
        geometry_component.runtime.route.depth_bin = 24U;
        GeometrySystem3D::RebuildSortKey(geometry_component);

        Surface3D surface_component{};
        InitializeSurface3DComponent(surface_component,
                                     6101U,
                                     surface_sampler_id.value,
                                     40U,
                                     vr::ecs::Rgba8{220U, 240U, 255U, 230U});

        Text3D text_component{};
        InitializeText3DComponent(text_component, 1U, 3U, "Unified Scene 3D");

        Transform3D geometry_transform{};
        Transform3D surface_transform{};
        Transform3D text_transform{};
        TransformSystem3D::Initialize(geometry_transform);
        TransformSystem3D::Initialize(surface_transform);
        TransformSystem3D::Initialize(text_transform);
        TransformSystem3D::SetLocalPosition(geometry_transform,
                                            vr::ecs::Float3{.x = -1.15F, .y = 0.05F, .z = 0.15F});
        TransformSystem3D::SetLocalScale(geometry_transform,
                                         vr::ecs::Float3{.x = 1.85F, .y = 1.85F, .z = 1.0F});
        TransformSystem3D::SetLocalPosition(surface_transform,
                                            vr::ecs::Float3{.x = 1.05F, .y = -0.10F, .z = -0.20F});
        TransformSystem3D::SetLocalScale(surface_transform,
                                         vr::ecs::Float3{.x = 1.65F, .y = 1.65F, .z = 1.0F});
        TransformSystem3D::SetLocalPosition(text_transform,
                                            vr::ecs::Float3{.x = -1.8F, .y = 1.0F, .z = -0.10F});
        TransformSystem3D::SetLocalScale(text_transform,
                                         vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

        std::array<Transform3D, 3U> shared_transforms{geometry_transform, surface_transform, text_transform};
        TransformSystem3D::UpdateHierarchy(shared_transforms.data(),
                                           static_cast<std::uint32_t>(shared_transforms.size()));
        geometry_transform = shared_transforms[0U];
        surface_transform = shared_transforms[1U];
        text_transform = shared_transforms[2U];

        Bounds3D geometry_bounds{};
        Bounds3D surface_bounds{};
        Bounds3D text_bounds{};
        BoundsSystem3D::Initialize(geometry_bounds);
        BoundsSystem3D::Initialize(surface_bounds);
        BoundsSystem3D::Initialize(text_bounds);
        BoundsSystem3D::SetLocalAabb(geometry_bounds,
                                     vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                     vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F});
        BoundsSystem3D::SetLocalCenterExtents(surface_bounds,
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 0.85F, .y = 0.85F, .z = 0.05F});
        BoundsSystem3D::SetLocalCenterExtents(text_bounds,
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 2.4F, .y = 0.55F, .z = 0.12F});
        std::array<Bounds3D, 3U> bounds_batch{geometry_bounds, surface_bounds, text_bounds};
        (void)BoundsSystem3D::UpdateAligned(bounds_batch.data(),
                                            shared_transforms.data(),
                                            static_cast<std::uint32_t>(bounds_batch.size()));
        geometry_bounds = bounds_batch[0U];
        surface_bounds = bounds_batch[1U];
        text_bounds = bounds_batch[2U];

        Camera3D camera{};
        CameraSystem3D::Initialize(camera);
        CameraSystem3D::SetProjectionMode(camera, vr::ecs::CameraProjectionMode::perspective);
        CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);
        CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);

        Transform3D camera_transform{};
        TransformSystem3D::Initialize(camera_transform);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.8F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);
        skybox_renderer.SetCameraData(&camera, &camera_transform);

        vr::render::IblBakeCoordinator ibl_bake_coordinator{};
        vr::render::IblBakeRequest ibl_bake_request{};
        ibl_bake_request.source.kind = vr::render::IblBakeSourceKind::equirectangular;
        ibl_bake_request.source.equirect.pixels = ibl_equirect_pixels.data();
        ibl_bake_request.source.equirect.width = ibl_equirect_width;
        ibl_bake_request.source.equirect.height = ibl_equirect_height;
        ibl_bake_request.skybox_cube_size = 16U;
        ibl_bake_request.specular_cube_size = 16U;
        ibl_bake_request.specular_sample_count = 96U;
        ibl_bake_request.sh_sample_count = 768U;
        ibl_bake_request.brdf_lut_size = 32U;
        ibl_bake_request.brdf_sample_count = 192U;
        ibl_bake_request.intensity = 1.15F;
        ibl_bake_request.rotation_y_radians = 0.42F;
        ibl_bake_request.tint_color = {1.0F, 0.98F, 1.04F};
        ibl_bake_request.set_active_environment = true;
        ibl_bake_coordinator.SetRequest(ibl_bake_request);

        std::array<vr::ecs::SkeletalJointPose<vr::ecs::Dim3>, 1U> skeletal_joint_storage{
            vr::ecs::SkeletalJointPose<vr::ecs::Dim3>{
                .position = vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                .rotation = vr::ecs::Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
                .scale = vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
            }
        };
        std::array<vr::ecs::SkeletalPoseOutputState<vr::ecs::Dim3>, 1U> skeletal_outputs{
            vr::ecs::SkeletalPoseOutputState<vr::ecs::Dim3>{
                .joints = skeletal_joint_storage.data(),
                .joint_count = static_cast<std::uint32_t>(skeletal_joint_storage.size()),
                .sampled_joint_count = static_cast<std::uint32_t>(skeletal_joint_storage.size()),
                .revision = 1U,
                .bind_pose_joints = skeletal_joint_storage.data(),
                .bind_pose_joint_count = static_cast<std::uint32_t>(skeletal_joint_storage.size()),
                .reserved0 = 0U,
                .reserved1 = 0U,
            }
        };
        std::array<float, 2U> morph_weight_storage{0.0F, 0.0F};
        std::array<vr::ecs::MorphWeightOutputState, 1U> morph_outputs{
            vr::ecs::MorphWeightOutputState{
                .weights = morph_weight_storage.data(),
                .weight_count = static_cast<std::uint32_t>(morph_weight_storage.size()),
                .sampled_weight_count = static_cast<std::uint32_t>(morph_weight_storage.size()),
                .revision = 1U,
            }
        };
        std::array<vr::ecs::Float4, 2U> vertex_deform_parameter_storage{
            vr::ecs::Float4{.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F},
            vr::ecs::Float4{.x = 0.0F, .y = 2.5F, .z = 0.0F, .w = 0.0F}
        };
        std::array<vr::ecs::VertexDeformOutputState, 1U> vertex_deform_outputs{
            vr::ecs::VertexDeformOutputState{
                .parameters = vertex_deform_parameter_storage.data(),
                .parameter_count = static_cast<std::uint32_t>(vertex_deform_parameter_storage.size()),
                .sampled_parameter_count = static_cast<std::uint32_t>(vertex_deform_parameter_storage.size()),
                .revision = 1U,
            }
        };
        std::array<vr::ecs::FrameSequenceOutputState, 1U> frame_sequence_outputs{
            vr::ecs::FrameSequenceOutputState{
                .frame_index_a = 0U,
                .frame_index_b = 0U,
                .frame_count = 0U,
                .revision = 1U,
                .blend_alpha = 0.0F,
                .normalized_time = 0.0F,
                .frame_position = 0.0F,
                .reserved0 = 0U,
            }
        };
        vr::ecs::AnimationEvaluationContext<vr::ecs::Dim3> animation_evaluation_context{};
        animation_evaluation_context.skeletal_outputs = {skeletal_outputs.data(),
                                                         static_cast<std::uint32_t>(skeletal_outputs.size())};
        animation_evaluation_context.morph_outputs = {morph_outputs.data(),
                                                      static_cast<std::uint32_t>(morph_outputs.size())};
        animation_evaluation_context.vertex_deform_outputs = {
            vertex_deform_outputs.data(),
            static_cast<std::uint32_t>(vertex_deform_outputs.size())
        };
        animation_evaluation_context.frame_sequence_outputs = {
            frame_sequence_outputs.data(),
            static_cast<std::uint32_t>(frame_sequence_outputs.size())
        };
        vr::render::AnimationFrameCoordinator<vr::ecs::Dim3> animation_frame_coordinator{};
        animation_frame_coordinator.SetEvaluationContext(animation_evaluation_context);

        std::array<Light3D, 1U> lights{};
        std::array<Transform3D, 1U> light_transforms{};
        InitializeLightComponent(lights[0U]);
        TransformSystem3D::Initialize(light_transforms[0U]);
        TransformSystem3D::SetLocalPosition(light_transforms[0U],
                                            vr::ecs::Float3{.x = 0.35F, .y = 2.75F, .z = 2.35F});
        TransformSystem3D::SetLocalRotationEulerXyz(light_transforms[0U], -0.72F, 0.18F, 0.0F);
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
                                            vr::ecs::Float3{.x = 0.35F, .y = 2.75F, .z = 2.35F});
        TransformSystem3D::SetLocalRotationEulerXyz(shadow_transforms[0U], -0.72F, 0.18F, 0.0F);
        TransformSystem3D::UpdateHierarchy(shadow_transforms.data(),
                                           static_cast<std::uint32_t>(shadow_transforms.size()));

        vr::geometry::GeometryRenderer3DCreateInfo geometry_renderer_create_info{};
        geometry_renderer_create_info.reserve_component_count = 1U;
        geometry_renderer_create_info.reserve_instance_count = 64U;
        geometry_renderer_create_info.enable_depth = true;
        geometry_renderer_create_info.clear_depth = true;
        geometry_renderer_create_info.clear_swapchain = false;
        geometry_renderer_create_info.clear_color = {{0.035F, 0.040F, 0.060F, 1.0F}};
        geometry_renderer_create_info.directional_light_x = 0.6F;
        geometry_renderer_create_info.directional_light_y = -0.8F;
        geometry_renderer_create_info.directional_light_z = 0.25F;
        geometry_renderer_create_info.directional_light_intensity = 1.0F;
        geometry_renderer.Initialize(geometry_renderer_create_info);
        geometry_renderer_initialized = true;
        geometry_renderer.SetHosts(&geometry_resource_host, &geometry_upload_host);
        geometry_renderer.SetMaterialHosts(&geometry_material_host, &geometry_image_host);
        geometry_renderer.SetSceneData(&geometry_component,
                                       &geometry_transform,
                                       1U,
                                       &camera,
                                       &camera_transform,
                                       &geometry_bounds);

        vr::surface::SurfaceRenderer3DCreateInfo surface_renderer_create_info{};
        surface_renderer_create_info.reserve_component_count = 1U;
        surface_renderer_create_info.reserve_instance_count = 64U;
        surface_renderer_create_info.enable_depth = true;
        surface_renderer_create_info.clear_depth = false;
        surface_renderer_create_info.clear_swapchain = false;
        surface_renderer.Initialize(surface_renderer_create_info);
        surface_renderer_initialized = true;
        surface_renderer.SetHosts(&surface_upload_host, &surface_image_host);
        surface_renderer.SetSceneData(&surface_component,
                                      &surface_transform,
                                      1U,
                                      &camera,
                                      &camera_transform,
                                      &surface_bounds);

        vr::text::TextRenderer3DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.runtime_build.pixel_size_quantization = 1.0F;
        text_renderer_create_info.runtime_build.enable_kerning = true;
        text_renderer_create_info.reserve_component_count = 1U;
        text_renderer_create_info.reserve_glyph_count = 4096U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 1024U * 1024U;
        text_renderer_create_info.enable_depth = true;
        text_renderer_create_info.clear_depth = false;
        text_renderer_create_info.clear_swapchain = false;
        text_renderer.Initialize(text_renderer_create_info);
        text_renderer_initialized = true;
        text_renderer.SetSceneData(&text_component,
                                   &text_transform,
                                   1U,
                                   &camera,
                                   &camera_transform,
                                   &text_bounds);
        vr::shadow::ShadowRenderer3DCreateInfo shadow_renderer_create_info{};
        shadow_renderer_create_info.reserve_shadow_count = static_cast<std::uint32_t>(shadows.size());
        shadow_renderer_create_info.reserve_caster_count = 1U;
        shadow_renderer.Initialize(shadow_renderer_create_info);
        shadow_renderer_initialized = true;
        shadow_renderer.SetHosts(&geometry_resource_host);
        shadow_renderer.SetSceneData(shadows.data(),
                                     shadow_transforms.data(),
                                     static_cast<std::uint32_t>(shadows.size()),
                                     &camera,
                                     &geometry_bounds);
        shadow_renderer.SetGeometryData(&geometry_component, &geometry_transform, 1U);
        recorder.BindAnimationFrameCoordinator(&animation_frame_coordinator);
        recorder.RegisterPreSceneRenderer(ibl_bake_coordinator);
        recorder.RegisterShadowRenderer(shadow_renderer);
        recorder.RegisterOpaqueSceneRenderer(skybox_renderer, vr::render::SceneRenderPassRole::first);
        recorder.RegisterOpaqueSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::middle);
        recorder.RegisterTransparentSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::middle);
        recorder.RegisterTransparentSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::last);

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
        std::uint32_t max_geometry_draw_calls = 0U;
        std::uint32_t max_skybox_draw_calls = 0U;
        std::uint32_t max_skybox_descriptor_binds = 0U;
        std::uint32_t max_surface_draw_calls = 0U;
        std::uint32_t max_surface_ibl_descriptor_binds = 0U;
        std::uint32_t max_text_draw_calls = 0U;
        std::uint32_t max_combine_draw_calls = 0U;
        std::uint32_t max_blur_draw_calls = 0U;
        std::uint32_t max_visible_light_count = 0U;
        std::uint32_t max_shadow_view_count = 0U;
        std::uint32_t max_linked_light_count = 0U;
        std::uint32_t max_light_descriptor_binds = 0U;
        std::uint32_t max_ibl_descriptor_binds = 0U;
        std::uint32_t max_shadow_draw_calls = 0U;
        std::uint32_t max_shadow_atlas_passes = 0U;
        std::uint32_t max_text_instances = 0U;
        std::uint32_t max_surface_instances = 0U;
        std::uint32_t max_geometry_instances = 0U;
        std::uint32_t max_geometry_skeletal_instances = 0U;
        std::uint32_t max_geometry_vertex_deform_instances = 0U;
        std::uint32_t max_geometry_morph_instances = 0U;
        std::uint32_t max_shadow_morph_draw_calls = 0U;

        constexpr std::uint32_t max_ticks = 16U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            const float t = static_cast<float>(tick_index);
            const float wave = std::sin(t * 0.35F);
            const float secondary_wave = std::cos(t * 0.25F);
            TransformSystem3D::SetLocalRotationEulerXyz(geometry_transform,
                                                        0.0F,
                                                        0.22F * t,
                                                        0.12F * std::sin(t * 0.35F));
            TransformSystem3D::SetLocalRotationEulerXyz(surface_transform,
                                                        0.0F,
                                                        -0.18F * t,
                                                        0.0F);
            TransformSystem3D::SetLocalRotationEulerXyz(text_transform,
                                                        0.0F,
                                                        0.0F,
                                                        0.10F * std::sin(t * 0.20F));
            TransformSystem3D::SetLocalPosition(light_transforms[0U],
                                                vr::ecs::Float3{.x = 0.45F * std::cos(t * 0.12F),
                                                                .y = 2.75F + 0.10F * std::sin(t * 0.17F),
                                                                .z = 2.35F});
            TransformSystem3D::SetLocalRotationEulerXyz(light_transforms[0U],
                                                        -0.72F,
                                                        0.20F + 0.18F * std::sin(t * 0.11F),
                                                        0.0F);
            TransformSystem3D::SetLocalPosition(shadow_transforms[0U],
                                                vr::ecs::Float3{.x = 0.45F * std::cos(t * 0.12F),
                                                                .y = 2.75F + 0.10F * std::sin(t * 0.17F),
                                                                .z = 2.35F});
            TransformSystem3D::SetLocalRotationEulerXyz(shadow_transforms[0U],
                                                        -0.72F,
                                                        0.20F + 0.18F * std::sin(t * 0.11F),
                                                        0.0F);

            char frame_text[64]{};
            std::snprintf(frame_text, sizeof(frame_text), "Unified Scene %u", tick_index);
            (void)TextSystem3D::SetText(text_component, frame_text);

            skeletal_joint_storage[0U].position = vr::ecs::Float3{
                .x = 0.0F,
                .y = 0.08F * wave,
                .z = 0.0F,
            };
            skeletal_joint_storage[0U].rotation =
                vr::ecs::spatial_math::QuaternionFromEulerXyz(0.0F, 0.0F, 0.14F * wave);
            skeletal_joint_storage[0U].scale = vr::ecs::Float3{
                .x = 1.0F,
                .y = 1.0F + 0.06F * secondary_wave,
                .z = 1.0F,
            };
            skeletal_outputs[0U].revision = tick_index + 2U;
            skeletal_outputs[0U].sampled_joint_count = 1U;

            morph_weight_storage[0U] = 0.5F + 0.5F * wave;
            morph_weight_storage[1U] = 0.25F + 0.25F * secondary_wave;
            morph_outputs[0U].revision = tick_index + 2U;
            morph_outputs[0U].sampled_weight_count = 2U;

            vertex_deform_parameter_storage[0U] = vr::ecs::Float4{
                .x = 0.08F,
                .y = 0.0F,
                .z = 1.0F,
                .w = 0.0F,
            };
            vertex_deform_parameter_storage[1U] = vr::ecs::Float4{
                .x = t * 0.42F,
                .y = 3.0F,
                .z = 0.0F,
                .w = 0.02F * wave,
            };
            vertex_deform_outputs[0U].revision = tick_index + 2U;
            vertex_deform_outputs[0U].sampled_parameter_count = 2U;

            shared_transforms = {geometry_transform, surface_transform, text_transform};
            TransformSystem3D::UpdateHierarchy(shared_transforms.data(),
                                               static_cast<std::uint32_t>(shared_transforms.size()));
            geometry_transform = shared_transforms[0U];
            surface_transform = shared_transforms[1U];
            text_transform = shared_transforms[2U];
            TransformSystem3D::UpdateHierarchy(light_transforms.data(),
                                               static_cast<std::uint32_t>(light_transforms.size()));
            TransformSystem3D::UpdateHierarchy(shadow_transforms.data(),
                                               static_cast<std::uint32_t>(shadow_transforms.size()));

            bounds_batch = {geometry_bounds, surface_bounds, text_bounds};
            (void)BoundsSystem3D::UpdateAligned(bounds_batch.data(),
                                                shared_transforms.data(),
                                                static_cast<std::uint32_t>(bounds_batch.size()));
            geometry_bounds = bounds_batch[0U];
            surface_bounds = bounds_batch[1U];
            text_bounds = bounds_batch[2U];
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

            const auto skybox_stats = skybox_renderer.Stats();
            const auto geometry_stats = geometry_renderer.Stats();
            const auto surface_stats = surface_renderer.Stats();
            const auto text_stats = text_renderer.Stats();
            const auto bloom_stats = recorder.PostStack().Stats();
            const auto shadow_stats = shadow_renderer.Stats();

            max_skybox_draw_calls = std::max(max_skybox_draw_calls, skybox_stats.draw_call_count);
            max_skybox_descriptor_binds =
                std::max(max_skybox_descriptor_binds, skybox_stats.descriptor_set_bind_count);
            max_geometry_draw_calls = std::max(max_geometry_draw_calls, geometry_stats.draw_call_count);
            max_surface_draw_calls = std::max(max_surface_draw_calls, surface_stats.draw_call_count);
            max_surface_ibl_descriptor_binds =
                std::max(max_surface_ibl_descriptor_binds, surface_stats.ibl_descriptor_set_bind_count);
            max_text_draw_calls = std::max(max_text_draw_calls, text_stats.draw_call_count);
            max_combine_draw_calls = std::max(max_combine_draw_calls, bloom_stats.combine_draw_call_count);
            max_blur_draw_calls = std::max(max_blur_draw_calls, bloom_stats.blur_draw_call_count);
            max_visible_light_count = std::max(max_visible_light_count, geometry_stats.visible_light_count);
            max_shadow_view_count = std::max(max_shadow_view_count, geometry_stats.shadow_view_count);
            max_linked_light_count =
                std::max(max_linked_light_count, geometry_stats.light_shadow_linked_count);
            max_light_descriptor_binds =
                std::max(max_light_descriptor_binds, geometry_stats.light_descriptor_set_bind_count);
            max_ibl_descriptor_binds =
                std::max(max_ibl_descriptor_binds, geometry_stats.ibl_descriptor_set_bind_count);
            max_shadow_draw_calls = std::max(max_shadow_draw_calls, shadow_stats.draw_call_count);
            max_shadow_atlas_passes =
                std::max(max_shadow_atlas_passes, shadow_stats.atlas_layer_draw_pass_count);
            max_geometry_instances = std::max(max_geometry_instances, geometry_stats.instance_count);
            max_surface_instances = std::max(max_surface_instances, surface_stats.instance_count);
            max_text_instances = std::max(max_text_instances, text_stats.instance_count);
            max_geometry_skeletal_instances =
                std::max(max_geometry_skeletal_instances, geometry_stats.skeletal_animated_instance_count);
            max_geometry_vertex_deform_instances =
                std::max(max_geometry_vertex_deform_instances, geometry_stats.vertex_deform_animated_instance_count);
            max_geometry_morph_instances =
                std::max(max_geometry_morph_instances, geometry_stats.morph_animated_instance_count);
            max_shadow_morph_draw_calls =
                std::max(max_shadow_morph_draw_calls, shadow_stats.morph_animated_draw_call_count);
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_skybox_draw_calls > 0U);
        VR_CHECK(max_skybox_descriptor_binds > 0U);
        VR_CHECK(max_geometry_draw_calls > 0U);
        VR_CHECK(max_surface_draw_calls > 0U);
        VR_CHECK(max_surface_ibl_descriptor_binds > 0U);
        VR_CHECK(max_text_draw_calls > 0U);
        VR_CHECK(max_combine_draw_calls > 0U);
        VR_CHECK(max_blur_draw_calls > 0U);
        VR_CHECK(max_visible_light_count > 0U);
        VR_CHECK(max_shadow_view_count > 0U);
        VR_CHECK(max_linked_light_count > 0U);
        VR_CHECK(max_light_descriptor_binds > 0U);
        VR_CHECK(max_ibl_descriptor_binds > 0U);
        VR_CHECK(max_shadow_draw_calls > 0U);
        VR_CHECK(max_shadow_atlas_passes > 0U);
        VR_CHECK(max_geometry_instances > 0U);
        VR_CHECK(max_surface_instances > 0U);
        VR_CHECK(max_text_instances > 0U);
        VR_CHECK(max_geometry_skeletal_instances > 0U);
        VR_CHECK(max_geometry_vertex_deform_instances > 0U);
        VR_CHECK(max_geometry_morph_instances > 0U);
        VR_CHECK(max_shadow_morph_draw_calls > 0U);
        VR_CHECK(recorder.Stats().pre_scene_renderer_count == 2U);
        VR_CHECK(recorder.Stats().animation_binding_refresh_count > 0U);
        VR_CHECK(recorder.Stats().frame_packet_bind_count >= 1U);
        VR_CHECK(recorder.Stats().frame_packet_prepare_count > 0U);
        VR_CHECK(recorder.Stats().frame_packet_record_count > 0U);
        VR_CHECK(recorder.FramePacket() == &main_scene_packet);
        VR_CHECK(recorder.ActiveView() == &main_view);
        VR_CHECK(recorder.ActiveView() != nullptr);
        VR_CHECK(recorder.ActiveView()->camera == &camera);
        VR_CHECK(runtime.TargetPool().Stats().acquire_count > 0U);
        VR_CHECK(runtime.TargetPool().Stats().reuse_hit_count > 0U);
        VR_CHECK(runtime.Ibl().Stats().prepared_frame_count > 0U);
        VR_CHECK(runtime.Ibl().Stats().environment_count == 1U);
        VR_CHECK(runtime.Ibl().Stats().descriptor_update_count <= submitted_frames + 2U);
        VR_CHECK(runtime.IblBake().Stats().baked_environment_count == 1U);
        VR_CHECK(runtime.IblBake().Stats().generated_texture_count >= 3U);
        VR_CHECK(ibl_bake_coordinator.HasBakedResult());
        VR_CHECK(ibl_bake_coordinator.Result().environment_id.IsValid());
        VR_CHECK(ibl_bake_coordinator.Result().skybox_cube.IsValid());
        VR_CHECK(ibl_bake_coordinator.Result().specular_cube.IsValid());
        VR_CHECK(ibl_bake_coordinator.Result().brdf_lut.IsValid());
        VR_CHECK(ibl_bake_coordinator.Stats().bake_count == 1U);
        VR_CHECK(runtime.RenderTarget().ResolveView(recorder.PostStack().Targets().ColorTarget()).state ==
                 vr::render::RenderTargetStateKind::shader_read);
        VR_CHECK(runtime.RenderTarget().ResolveView(recorder.PostStack().Targets().DepthTarget()).state ==
                 vr::render::RenderTargetStateKind::depth_attachment);
        VR_CHECK(runtime.GlyphUpload().Stats().uploaded_rect_count > 0U);
        VR_CHECK(animation_frame_coordinator.Stats().apply_scene_call_count > 0U);
        VR_CHECK(animation_frame_coordinator.Stats().apply_shadow_call_count > 0U);

        recorder.Shutdown(runtime.Context());
        skybox_renderer.Shutdown(runtime.Context());
        skybox_renderer_initialized = false;
        shadow_renderer.Shutdown(runtime.Context());
        shadow_renderer_initialized = false;
        text_renderer.Shutdown(runtime.Context());
        text_renderer_initialized = false;
        surface_renderer.Shutdown(runtime.Context());
        surface_renderer_initialized = false;
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
        surface_image_host.Shutdown(runtime.Context());
        surface_image_host_initialized = false;
        surface_upload_host.Shutdown(runtime.Context());
        surface_upload_host_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (runtime_initialized && runtime.IsInitialized() && recorder.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
        }
        if (skybox_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            skybox_renderer.Shutdown(runtime.Context());
            skybox_renderer_initialized = false;
        }
        if (shadow_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            shadow_renderer.Shutdown(runtime.Context());
            shadow_renderer_initialized = false;
        }
        if (text_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            text_renderer.Shutdown(runtime.Context());
            text_renderer_initialized = false;
        }
        if (surface_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_renderer.Shutdown(runtime.Context());
            surface_renderer_initialized = false;
        }
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
        if (surface_image_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_image_host.Shutdown(runtime.Context());
            surface_image_host_initialized = false;
        }
        if (surface_upload_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_upload_host.Shutdown(runtime.Context());
            surface_upload_host_initialized = false;
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
