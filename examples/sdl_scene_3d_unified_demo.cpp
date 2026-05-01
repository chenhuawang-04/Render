#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/geometry/geometry_material_host.hpp"
#include "vr/geometry/geometry_renderer_3d.hpp"
#include "vr/geometry/geometry_resource_host.hpp"
#include "vr/geometry/geometry_upload_host.hpp"
#include "vr/render/render_target_bloom_renderer.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/scene_render_target_set.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/surface/surface_renderer_3d.hpp"
#include "vr/surface/surface_upload_host.hpp"
#include "vr/text/text_renderer_3d.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
using Text3D = vr::ecs::Text<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;

using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

constexpr float k_pi = 3.14159265358979323846F;
constexpr std::uint32_t k_geometry_material_id = 1101U;
constexpr std::uint32_t k_geometry_image_id = 2101U;
constexpr std::uint32_t k_surface_image_id = 3101U;
constexpr std::uint32_t k_text_font_id = 1U;
constexpr std::uint32_t k_text_material_id = 4101U;

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
            const std::uint8_t r = static_cast<std::uint8_t>(55.0F + 170.0F * fx);
            const std::uint8_t g = static_cast<std::uint8_t>(65.0F + 120.0F * (1.0F - fy));
            const std::uint8_t b = static_cast<std::uint8_t>(110.0F + 120.0F * fy);
            pixels_[index] = PackRgba8(r, g, b, 255U);
        }
    }
}

[[nodiscard]] std::string PickDemoFontPath() {
    namespace fs = std::filesystem;

    constexpr std::array<const char*, 8U> candidates{
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/verdana.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf"
    };

    for (const char* candidate : candidates) {
        const fs::path font_path(candidate);
        if (fs::exists(font_path) && fs::is_regular_file(font_path)) {
            return font_path.string();
        }
    }
    return {};
}

void InitializeGeometryComponent(Geometry3D& component_) {
    GeometrySystem3D::Initialize(component_);
    GeometrySystem3D::SetRuntimeRoute(component_, 1U, k_geometry_material_id, 0U);
    GeometrySystem3D::SetBounds(component_,
                                vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F});
    component_.style.depth_test = 1U;
    component_.style.depth_write = 1U;
    component_.style.shading_model = vr::ecs::Geometry3DShadingModel::lit;
    component_.style.albedo_color = vr::ecs::Rgba8{236U, 214U, 178U, 255U};
    component_.mesh.submesh_index = 0U;
    component_.mesh.lod_index = 0U;
    component_.mesh.flags = 0U;
    component_.runtime.route.depth_bin = 24U;
    GeometrySystem3D::RebuildSortKey(component_);
}

void InitializeSurfaceComponent(Surface3D& component_,
                                std::uint32_t sampler_id_) {
    SurfaceSystem3D::Initialize(component_);
    SurfaceSystem3D::SetTextureRoute(component_, k_surface_image_id, sampler_id_, 0U, 0U);
    SurfaceSystem3D::SetDepthBin(component_, 40U);
    SurfaceSystem3D::SetDepthTest(component_, true);
    SurfaceSystem3D::SetDepthWrite(component_, false);
    SurfaceSystem3D::SetDoubleSided(component_, true);
    SurfaceSystem3D::SetTintColor(component_, vr::ecs::Rgba8{225U, 238U, 255U, 232U});
    SurfaceSystem3D::SetOpacity(component_, 0.94F);
    SurfaceSystem3D::SetUvTransform(component_, 1.05F, 1.05F, -0.03F, -0.02F);
}

void InitializeTextComponent(Text3D& component_,
                             std::string_view text_) {
    TextSystem3D::Initialize(component_);
    TextSystem3D::SetRuntimeRoute(component_, k_text_font_id, k_text_material_id, 0U, 0U);
    TextSystem3D::SetColor(component_, vr::ecs::Rgba8{248U, 250U, 255U, 255U});
    TextSystem3D::SetOutlineEnabled(component_, true);
    TextSystem3D::SetOutlineWidthPx(component_, 1U);
    TextSystem3D::SetOutlineColor(component_, vr::ecs::Rgba8{18U, 22U, 32U, 255U});
    TextSystem3D::SetBillboard(component_, false);
    TextSystem3D::SetDepthTest(component_, true);
    TextSystem3D::SetDepthWrite(component_, false);
    TextSystem3D::SetWorldSize(component_, 0.42F);
    TextSystem3D::SetDepthBin(component_, 52U);
    (void)TextSystem3D::SetText(component_, text_);
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

struct UnifiedScene3DRecorder final {
    Runtime* runtime = nullptr;
    vr::geometry::GeometryRenderer3D geometry_renderer{};
    vr::surface::SurfaceRenderer3D surface_renderer{};
    vr::text::TextRenderer3D text_renderer{};
    vr::render::RenderTargetBloomRenderer bloom_renderer{};
    vr::render::SceneRenderTargetSet scene_targets{};

    void InitializeSceneTargets() {
        vr::render::SceneRenderTargetSetCreateInfo create_info{};
        create_info.color_debug_name = "UnifiedScene3DColor";
        create_info.depth_debug_name = "UnifiedScene3DDepth";
        create_info.enable_depth = true;
        create_info.color_lifetime = vr::render::RenderTargetLifetime::transient;
        create_info.depth_lifetime = vr::render::RenderTargetLifetime::transient;
        create_info.clear_color = VkClearColorValue{{0.035F, 0.040F, 0.060F, 1.0F}};
        scene_targets.Initialize(create_info);
    }

    void PrepareFrame(const vr::render::RuntimePrepareContext& prepare_context_) {
        (void)scene_targets.PrepareFrameAndConfigure(
            prepare_context_,
            nullptr,
            vr::render::BindSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::first),
            vr::render::BindSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::middle),
            vr::render::BindSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::last));
        (void)scene_targets.ConfigureSceneConsumer(bloom_renderer);
        geometry_renderer.PrepareFrame(prepare_context_);
        surface_renderer.PrepareFrame(prepare_context_);
        text_renderer.PrepareFrame(prepare_context_);
        bloom_renderer.PrepareFrame(prepare_context_);
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        geometry_renderer.Record(record_context_);
        surface_renderer.Record(record_context_);
        text_renderer.Record(record_context_);
        bloom_renderer.Record(record_context_);
    }

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_) {
        geometry_renderer.OnSwapchainRecreated(image_count_,
                                               extent_,
                                               format_,
                                               last_submitted_value_,
                                               completed_submit_value_);
        surface_renderer.OnSwapchainRecreated(image_count_,
                                              extent_,
                                              format_,
                                              last_submitted_value_,
                                              completed_submit_value_);
        text_renderer.OnSwapchainRecreated(image_count_,
                                           extent_,
                                           format_,
                                           last_submitted_value_,
                                           completed_submit_value_);
        bloom_renderer.OnSwapchainRecreated(image_count_, extent_, format_);

        if (runtime == nullptr) {
            return;
        }
        (void)scene_targets.OnSwapchainRecreatedAndConfigure(
            runtime->Context(),
            runtime->RenderTarget(),
            runtime->HasRenderTargetPool() ? &runtime->TargetPool() : nullptr,
            extent_,
            last_submitted_value_,
            completed_submit_value_,
            nullptr,
            vr::render::BindSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::first),
            vr::render::BindSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::middle),
            vr::render::BindSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::last));
        (void)scene_targets.ConfigureSceneConsumer(bloom_renderer);
    }
};

} // namespace

int main(int argc_,
         char** argv_) {
    const std::string font_path = PickDemoFontPath();
    if (font_path.empty()) {
        std::cerr << "sdl_scene_3d_unified_demo failed: no usable system font found.\n";
        return 1;
    }

    const std::uint32_t max_frames = ParseMaxFrames(argc_, argv_);

    Runtime runtime{};
    vr::geometry::GeometryResourceHost geometry_resource_host{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryImageHost geometry_image_host{};
    vr::geometry::GeometryMaterialHost geometry_material_host{};
    vr::surface::SurfaceUploadHost surface_upload_host{};
    vr::surface::SurfaceImageHost surface_image_host{};
    UnifiedScene3DRecorder recorder{};

    bool runtime_initialized = false;
    bool geometry_resource_host_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_image_host_initialized = false;
    bool geometry_material_host_initialized = false;
    bool surface_upload_host_initialized = false;
    bool surface_image_host_initialized = false;
    bool geometry_renderer_initialized = false;
    bool surface_renderer_initialized = false;
    bool text_renderer_initialized = false;
    bool bloom_renderer_initialized = false;

    constexpr std::uint32_t texture_width = 64U;
    constexpr std::uint32_t texture_height = 64U;
    std::array<std::uint32_t, texture_width * texture_height> geometry_pixels{};
    std::array<std::uint32_t, texture_width * texture_height> surface_pixels{};
    FillCheckerTexture(geometry_pixels.data(),
                       texture_width,
                       texture_height,
                       PackRgba8(242U, 210U, 164U, 255U),
                       PackRgba8(108U, 74U, 54U, 255U));
    FillGradientTexture(surface_pixels.data(), texture_width, texture_height);

    std::array<vr::geometry::GeometryMeshVertex, 4U> mesh_vertices{
        vr::geometry::GeometryMeshVertex{.position_x = -0.5F, .position_y = -0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 0.0F, .uv_v = 0.0F},
        vr::geometry::GeometryMeshVertex{.position_x = 0.5F, .position_y = -0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 1.0F, .uv_v = 0.0F},
        vr::geometry::GeometryMeshVertex{.position_x = 0.5F, .position_y = 0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 1.0F, .uv_v = 1.0F},
        vr::geometry::GeometryMeshVertex{.position_x = -0.5F, .position_y = 0.5F, .position_z = 0.0F, .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F, .uv_u = 0.0F, .uv_v = 1.0F}
    };
    std::array<std::uint32_t, 6U> mesh_indices{0U, 1U, 2U, 2U, 3U, 0U};
    std::array<vr::geometry::GeometrySubmeshRange, 1U> mesh_submeshes{
        vr::geometry::GeometrySubmeshRange{.first_index = 0U, .index_count = 6U, .vertex_offset = 0, .reserved0 = 0U}
    };

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "Vulkan SDL3 Unified 3D Scene Demo";
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

        recorder.runtime = &runtime;
        recorder.InitializeSceneTargets();

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
        geometry_image_upload.image_id = k_geometry_image_id;
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
        surface_image_upload.image_id = k_surface_image_id;
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

        const vr::render::UploadEndFrameResult upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        geometry_resource_host.BeginFrame(runtime.Context(), 0U);
        geometry_image_host.BeginFrame(runtime.Context(), 0U);
        surface_image_host.BeginFrame(runtime.Context(), 0U);

        vr::geometry::GeometryMaterialDesc geometry_material{};
        geometry_material.material_id = k_geometry_material_id;
        geometry_material.image_id = k_geometry_image_id;
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
        runtime.GlyphAtlas().MapFont(k_text_font_id, base_face_id);

        Geometry3D geometry_component{};
        InitializeGeometryComponent(geometry_component);

        Surface3D surface_component{};
        InitializeSurfaceComponent(surface_component, surface_sampler_id.value);

        Text3D text_component{};
        InitializeTextComponent(text_component, "Unified Scene 3D");

        Transform3D geometry_transform{};
        Transform3D surface_transform{};
        Transform3D text_transform{};
        TransformSystem3D::Initialize(geometry_transform);
        TransformSystem3D::Initialize(surface_transform);
        TransformSystem3D::Initialize(text_transform);
        TransformSystem3D::SetLocalPosition(geometry_transform,
                                            vr::ecs::Float3{.x = -1.15F, .y = 0.10F, .z = 0.12F});
        TransformSystem3D::SetLocalScale(geometry_transform,
                                         vr::ecs::Float3{.x = 1.90F, .y = 1.90F, .z = 1.0F});
        TransformSystem3D::SetLocalPosition(surface_transform,
                                            vr::ecs::Float3{.x = 1.05F, .y = -0.12F, .z = -0.20F});
        TransformSystem3D::SetLocalScale(surface_transform,
                                         vr::ecs::Float3{.x = 1.70F, .y = 1.70F, .z = 1.0F});
        TransformSystem3D::SetLocalPosition(text_transform,
                                            vr::ecs::Float3{.x = -1.90F, .y = 1.05F, .z = -0.08F});
        TransformSystem3D::SetLocalScale(text_transform,
                                         vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

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
                                              vr::ecs::Float3{.x = 2.8F, .y = 0.6F, .z = 0.12F});

        std::array<Transform3D, 3U> shared_transforms{geometry_transform, surface_transform, text_transform};
        TransformSystem3D::UpdateHierarchy(shared_transforms.data(),
                                           static_cast<std::uint32_t>(shared_transforms.size()));
        geometry_transform = shared_transforms[0U];
        surface_transform = shared_transforms[1U];
        text_transform = shared_transforms[2U];

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
        CameraSystem3D::SetAspectRatio(camera, 1280.0F / 720.0F);

        Transform3D camera_transform{};
        TransformSystem3D::Initialize(camera_transform);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.8F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

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
        recorder.geometry_renderer.Initialize(geometry_renderer_create_info);
        geometry_renderer_initialized = true;
        recorder.geometry_renderer.SetHosts(&geometry_resource_host, &geometry_upload_host);
        recorder.geometry_renderer.SetMaterialHosts(&geometry_material_host, &geometry_image_host);
        recorder.geometry_renderer.SetSceneData(&geometry_component,
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
        recorder.surface_renderer.Initialize(surface_renderer_create_info);
        surface_renderer_initialized = true;
        recorder.surface_renderer.SetHosts(&surface_upload_host, &surface_image_host);
        recorder.surface_renderer.SetSceneData(&surface_component,
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
        recorder.text_renderer.Initialize(text_renderer_create_info);
        text_renderer_initialized = true;
        recorder.text_renderer.SetSceneData(&text_component,
                                            &text_transform,
                                            1U,
                                            &camera,
                                            &camera_transform,
                                            &text_bounds);

        vr::render::RenderTargetBloomRendererCreateInfo bloom_create_info{};
        bloom_create_info.clear_swapchain = true;
        bloom_create_info.clear_color = {{0.02F, 0.025F, 0.04F, 1.0F}};
        bloom_create_info.enable_reinhard_tonemap = true;
        bloom_create_info.exposure = 1.0F;
        bloom_create_info.apply_manual_gamma = false;
        bloom_create_info.bloom_threshold = 0.70F;
        bloom_create_info.bloom_knee = 0.45F;
        bloom_create_info.bloom_intensity = 0.95F;
        bloom_create_info.blur_filter_scale = 1.10F;
        bloom_create_info.downsample_scale = 0.5F;
        recorder.bloom_renderer.Initialize(bloom_create_info);
        bloom_renderer_initialized = true;

        std::cout << "sdl_scene_3d_unified_demo running (geometry + surface + text share transient scene target + bloom post stack). Close window to exit.\n";

        std::uint64_t fps_window_begin_ticks = SDL_GetTicks();
        std::uint32_t fps_window_frame_count = 0U;
        std::uint64_t frame_index = 0U;

        while (runtime.IsRunning()) {
            const std::uint64_t now_ticks = SDL_GetTicks();
            const float time_seconds = static_cast<float>(now_ticks) * 0.001F;

            const VkExtent2D extent = runtime.Swapchain().Extent();
            if (extent.width > 0U && extent.height > 0U) {
                CameraSystem3D::SetAspectRatio(camera,
                                               static_cast<float>(extent.width) /
                                                   static_cast<float>(extent.height));
            }

            TransformSystem3D::SetLocalRotationEulerXyz(geometry_transform,
                                                        0.0F,
                                                        0.35F * time_seconds,
                                                        0.18F * std::sin(time_seconds * 0.70F));
            TransformSystem3D::SetLocalRotationEulerXyz(surface_transform,
                                                        0.10F * std::sin(time_seconds * 0.65F),
                                                        -0.42F * time_seconds,
                                                        0.0F);
            TransformSystem3D::SetLocalRotationEulerXyz(text_transform,
                                                        0.0F,
                                                        0.0F,
                                                        0.12F * std::sin(time_seconds * 1.30F));

            char runtime_text[96]{};
            std::snprintf(runtime_text,
                          sizeof(runtime_text),
                          "Unified Scene | Frame:%llu",
                          static_cast<unsigned long long>(frame_index));
            (void)TextSystem3D::SetText(text_component, runtime_text);

            shared_transforms = {geometry_transform, surface_transform, text_transform};
            TransformSystem3D::UpdateHierarchy(shared_transforms.data(),
                                               static_cast<std::uint32_t>(shared_transforms.size()));
            geometry_transform = shared_transforms[0U];
            surface_transform = shared_transforms[1U];
            text_transform = shared_transforms[2U];

            bounds_batch = {geometry_bounds, surface_bounds, text_bounds};
            (void)BoundsSystem3D::UpdateAligned(bounds_batch.data(),
                                                shared_transforms.data(),
                                                static_cast<std::uint32_t>(bounds_batch.size()));
            geometry_bounds = bounds_batch[0U];
            surface_bounds = bounds_batch[1U];
            text_bounds = bounds_batch[2U];
            CameraSystem3D::Update(camera, camera_transform);

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
                const auto geometry_stats = recorder.geometry_renderer.Stats();
                const auto surface_stats = recorder.surface_renderer.Stats();
                const auto text_stats = recorder.text_renderer.Stats();
                const auto bloom_stats = recorder.bloom_renderer.Stats();
                const auto pool_stats = runtime.TargetPool().Stats();
                std::cout << "FPS:" << fps
                          << " Frame:" << frame_index
                          << " | G Draw:" << geometry_stats.draw_call_count
                          << " Inst:" << geometry_stats.instance_count
                          << " | S Draw:" << surface_stats.draw_call_count
                          << " Inst:" << surface_stats.instance_count
                          << " | T Draw:" << text_stats.draw_call_count
                          << " Inst:" << text_stats.instance_count
                          << " | Bloom P:" << bloom_stats.prefilter_draw_call_count
                          << " B:" << bloom_stats.blur_draw_call_count
                          << " C:" << bloom_stats.combine_draw_call_count
                          << " DSU:" << bloom_stats.descriptor_set_update_count
                          << " | Pool Acquire:" << pool_stats.acquire_count
                          << " Reuse:" << pool_stats.reuse_hit_count
                          << '\n';
                fps_window_begin_ticks = now_ticks;
                fps_window_frame_count = 0U;
            }
        }

        recorder.bloom_renderer.Shutdown(runtime.Context());
        bloom_renderer_initialized = false;
        recorder.text_renderer.Shutdown(runtime.Context());
        text_renderer_initialized = false;
        recorder.surface_renderer.Shutdown(runtime.Context());
        surface_renderer_initialized = false;
        recorder.geometry_renderer.Shutdown(runtime.Context());
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
        std::cerr << "sdl_scene_3d_unified_demo failed: " << exception_.what() << '\n';

        if (bloom_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.bloom_renderer.Shutdown(runtime.Context());
            bloom_renderer_initialized = false;
        }
        if (text_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.text_renderer.Shutdown(runtime.Context());
            text_renderer_initialized = false;
        }
        if (surface_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.surface_renderer.Shutdown(runtime.Context());
            surface_renderer_initialized = false;
        }
        if (geometry_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.geometry_renderer.Shutdown(runtime.Context());
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
        return 1;
    }

    return 0;
}
