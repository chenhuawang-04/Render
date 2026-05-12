#include "vr/asset/texture_host.hpp"
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
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/runtime/crash_tracer_support.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/scene/background/sky_environment.hpp"
#include "vr/shadow/shadow_renderer_3d.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/surface/surface_renderer_3d.hpp"
#include "vr/surface/surface_upload_host.hpp"
#include "vr/text/text_renderer_3d.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

constexpr float k_pi = 3.14159265358979323846F;
constexpr std::uint32_t k_geometry_material_id = 1101U;
constexpr std::uint32_t k_geometry_image_id = 2101U;
constexpr std::uint32_t k_surface_image_id = 3101U;
constexpr std::uint32_t k_text_font_id = 1U;
constexpr std::uint32_t k_text_material_id = 4101U;
constexpr vr::asset::TextureId k_sky_environment_texture_id{5101U};
constexpr float k_sky_environment_intensity = 1.15F;
constexpr float k_sky_environment_rotation_y = 0.42F;

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

[[nodiscard]] std::uint16_t FloatToHalfBits(float value_) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value_);
    const std::uint32_t sign = (bits >> 16U) & 0x8000U;
    std::int32_t exponent = static_cast<std::int32_t>((bits >> 23U) & 0xFFU) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7FFFFFU;

    if (((bits >> 23U) & 0xFFU) == 0xFFU) {
        if (mantissa != 0U) {
            return static_cast<std::uint16_t>(sign | 0x7E00U);
        }
        return static_cast<std::uint16_t>(sign | 0x7C00U);
    }

    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<std::uint16_t>(sign);
        }
        mantissa |= 0x800000U;
        const std::uint32_t shifted = mantissa >> static_cast<std::uint32_t>(1 - exponent);
        return static_cast<std::uint16_t>(sign | ((shifted + 0x00001000U) >> 13U));
    }

    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7C00U);
    }

    return static_cast<std::uint16_t>(sign |
                                      (static_cast<std::uint32_t>(exponent) << 10U) |
                                      ((mantissa + 0x00001000U) >> 13U));
}

[[nodiscard]] VkFormat SelectEnvironmentTextureFormat(vr::VulkanContext& context_) {
    if (vr::asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R16G16B16A16_SFLOAT)) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    if (vr::asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R8G8B8A8_UNORM)) {
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

void UploadHdrEnvironmentTexture(Runtime& runtime_,
                                 vr::asset::TextureId texture_id_,
                                 const vr::ecs::Float4* pixels_,
                                 std::uint32_t width_,
                                 std::uint32_t height_) {
    if (!runtime_.HasTextureHost() || !runtime_.HasUploadHost()) {
        throw std::runtime_error("Unified 3D demo requires TextureHost and UploadHost for HDRI upload.");
    }

    const VkFormat texture_format = SelectEnvironmentTextureFormat(runtime_.Context());
    if (texture_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("Unified 3D demo could not resolve a sampled HDR environment format.");
    }

    vr::asset::TextureSubresourceUploadInfo subresource{};
    subresource.mip_level = 0U;
    subresource.base_array_layer = 0U;
    subresource.layer_count = 1U;
    subresource.image_extent = VkExtent3D{width_, height_, 1U};

    vr::asset::TextureUploadInfo upload{};
    upload.create.texture_id = texture_id_;
    upload.create.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    upload.create.format = texture_format;
    upload.create.extent = VkExtent3D{width_, height_, 1U};
    upload.create.mip_levels = 1U;
    upload.create.array_layers = 1U;
    upload.create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    upload.create.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    upload.create.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    upload.create.retain_cpu_upload_data = true;

    std::vector<std::uint16_t> rgba16_pixels{};
    std::vector<std::uint8_t> rgba8_pixels{};
    if (texture_format == VK_FORMAT_R16G16B16A16_SFLOAT) {
        rgba16_pixels.resize(static_cast<std::size_t>(width_) * height_ * 4U);
        for (std::size_t texel_index = 0U; texel_index < static_cast<std::size_t>(width_) * height_; ++texel_index) {
            rgba16_pixels[texel_index * 4U + 0U] = FloatToHalfBits(pixels_[texel_index].x);
            rgba16_pixels[texel_index * 4U + 1U] = FloatToHalfBits(pixels_[texel_index].y);
            rgba16_pixels[texel_index * 4U + 2U] = FloatToHalfBits(pixels_[texel_index].z);
            rgba16_pixels[texel_index * 4U + 3U] = FloatToHalfBits(pixels_[texel_index].w);
        }
        subresource.pixels = rgba16_pixels.data();
        subresource.size_bytes = static_cast<VkDeviceSize>(rgba16_pixels.size() * sizeof(std::uint16_t));
    } else {
        rgba8_pixels.resize(static_cast<std::size_t>(width_) * height_ * 4U);
        for (std::size_t texel_index = 0U; texel_index < static_cast<std::size_t>(width_) * height_; ++texel_index) {
            rgba8_pixels[texel_index * 4U + 0U] =
                static_cast<std::uint8_t>(std::clamp(pixels_[texel_index].x, 0.0F, 1.0F) * 255.0F);
            rgba8_pixels[texel_index * 4U + 1U] =
                static_cast<std::uint8_t>(std::clamp(pixels_[texel_index].y, 0.0F, 1.0F) * 255.0F);
            rgba8_pixels[texel_index * 4U + 2U] =
                static_cast<std::uint8_t>(std::clamp(pixels_[texel_index].z, 0.0F, 1.0F) * 255.0F);
            rgba8_pixels[texel_index * 4U + 3U] =
                static_cast<std::uint8_t>(std::clamp(pixels_[texel_index].w, 0.0F, 1.0F) * 255.0F);
        }
        subresource.pixels = rgba8_pixels.data();
        subresource.size_bytes = static_cast<VkDeviceSize>(rgba8_pixels.size());
    }

    upload.subresources = &subresource;
    upload.subresource_count = 1U;

    runtime_.Upload().BeginFrame(runtime_.Context(), 0U);
    runtime_.Texture().UploadTexture(runtime_.Context(),
                                     runtime_.Upload(),
                                     0U,
                                     0U,
                                     0U,
                                     upload);
    const auto upload_end = runtime_.Upload().EndFrameAndSubmit(runtime_.Context(), 0U);
    if (upload_end.submitted) {
        runtime_.Upload().WaitFrame(runtime_.Context(), 0U);
    }
    runtime_.Texture().BeginFrame(runtime_.Context(), 0U);
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
    component_.style.cast_shadow = 1U;
    component_.style.shading_model = vr::ecs::Geometry3DShadingModel::lit;
    component_.style.albedo_color = vr::ecs::Rgba8{236U, 214U, 178U, 255U};
    component_.mesh.submesh_index = 0U;
    component_.mesh.lod_index = 0U;
    component_.mesh.flags = 0U;
    component_.runtime.route.depth_bin = 24U;
    GeometrySystem3D::RebuildSortKey(component_);
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
    ShadowSystem3D::SetLightComponentIndex(component_, light_component_index_);
    ShadowSystem3D::SetTransformComponentIndex(component_, 0U);
    ShadowSystem3D::SetCameraComponentIndex(component_, 0U);
    ShadowSystem3D::SetAtlasNamespace(component_, 9U);
    ShadowSystem3D::SetFaceCount(component_, 1U);
}

void InitializeSurfaceComponent(Surface3D& component_,
                                std::uint32_t sampler_id_) {
    SurfaceSystem3D::Initialize(component_);
    SurfaceSystem3D::SetTextureRoute(component_, k_surface_image_id, sampler_id_, 0U, 0U);
    SurfaceSystem3D::SetDepthBin(component_, 40U);
    SurfaceSystem3D::SetRenderPassHint(component_, vr::ecs::SurfaceRenderPassHint::transparent);
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

[[nodiscard]] vr::render::SceneRecorder3DCreateInfo BuildUnifiedSceneRecorderCreateInfo() noexcept {
    vr::render::SceneRecorder3DCreateInfo create_info{};
    create_info.scene_target.color_debug_name = "UnifiedScene3DColor";
    create_info.scene_target.depth_debug_name = "UnifiedScene3DDepth";
    create_info.scene_target.enable_depth = true;
    create_info.scene_target.color_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.depth_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.clear_color = VkClearColorValue{{0.035F, 0.040F, 0.060F, 1.0F}};
    create_info.bloom.clear_swapchain = true;
    create_info.bloom.clear_color = {{0.02F, 0.025F, 0.04F, 1.0F}};
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

void ApplySceneSkyEnvironment(vr::render::RenderScenePacket3D& packet_,
                              vr::asset::TextureId sky_texture_id_) {
    packet_.extra.environment_gpu = {};
    packet_.extra.ibl_environment_id = 0U;
    auto& environment = packet_.extra.environment;
    environment = {};
    environment.mode = vr::scene::SkyEnvironmentMode::equirectangular_hdr;
    environment.sky_texture_id = sky_texture_id_.value;
    environment.zenith_color = vr::ecs::Float4{.x = 0.06F, .y = 0.10F, .z = 0.18F, .w = 1.0F};
    environment.horizon_color = vr::ecs::Float4{.x = 0.24F, .y = 0.20F, .z = 0.18F, .w = 1.0F};
    environment.ground_color = vr::ecs::Float4{.x = 0.03F, .y = 0.03F, .z = 0.04F, .w = 1.0F};
    environment.tint = vr::ecs::Float4{.x = 1.0F, .y = 0.98F, .z = 1.04F, .w = 1.0F};
    environment.exposure = 1.0F;
    environment.sky_intensity = k_sky_environment_intensity;
    environment.diffuse_ibl_intensity = k_sky_environment_intensity;
    environment.specular_ibl_intensity = k_sky_environment_intensity;
    environment.rotation_y = k_sky_environment_rotation_y;
    environment.max_specular_lod = -1.0F;
    environment.revision = 1U;
    vr::render::RefreshRenderScenePacketSignature(packet_);
}

} // namespace

int main(int argc_,
         char** argv_) {
    vr::runtime::InstallProcessCrashTracer(argc_, argv_);
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
    vr::render::SceneRecorder3D recorder{};
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
        create_info.platform.window.title = "Vulkan SDL3 Unified 3D Scene Demo";
        create_info.platform.window.width = 1280;
        create_info.platform.window.height = 720;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = true;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.modules.enable_ibl_bake_host = true;
        create_info.render_loop.swapchain.enable_vsync = true;
        create_info.render_loop.swapchain.preferred_image_count = 3U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        recorder.Initialize(BuildUnifiedSceneRecorderCreateInfo());
        recorder.BindRuntime(runtime);
        recorder.BindLightFrameCoordinator(&light_frame_coordinator);

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
        UploadHdrEnvironmentTexture(runtime,
                                    k_sky_environment_texture_id,
                                    ibl_equirect_pixels.data(),
                                    ibl_equirect_width,
                                    ibl_equirect_height);
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
        GeometryMeshSystem3D::EnableVertexDeformShader(geometry_component, true);
        GeometryMeshSystem3D::EnableMorphTargets(geometry_component, true);
        GeometryMeshSystem3D::EnableSkeletalRootMotion(geometry_component, true);
        GeometryMeshSystem3D::EnableSkeletalSkinning(geometry_component, true);

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

        Transform3D camera_transform{};
        TransformSystem3D::Initialize(camera_transform);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.8F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

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
        recorder.RegisterShadowRenderer(shadow_renderer);
        recorder.RegisterOpaqueSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::middle);
        recorder.RegisterTransparentSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::middle);
        recorder.RegisterTransparentSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::last);

        std::uint64_t frame_index = 0U;
        vr::render::RenderView3D main_view{};
        vr::render::RenderScenePacket3D main_scene_packet{};
        vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                           main_scene_packet,
                                                           camera,
                                                           camera_transform,
                                                           runtime.Swapchain().Extent(),
                                                           frame_index);
        ApplySceneSkyEnvironment(main_scene_packet, k_sky_environment_texture_id);
        recorder.SetFramePacket(&main_scene_packet);

        std::cout << "sdl_scene_3d_unified_demo running (Scene3D HDRI sky environment + auto lazy IBL bake + geometry + surface + text + bloom post stack). Close window to exit.\n";

        std::uint64_t fps_window_begin_ticks = SDL_GetTicks();
        std::uint32_t fps_window_frame_count = 0U;

        while (runtime.IsRunning()) {
            const std::uint64_t now_ticks = SDL_GetTicks();
            const float time_seconds = static_cast<float>(now_ticks) * 0.001F;
            const float wave = std::sin(time_seconds * 1.20F);
            const float secondary_wave = std::cos(time_seconds * 0.95F);

            const VkExtent2D extent = runtime.Swapchain().Extent();
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
            TransformSystem3D::SetLocalPosition(light_transforms[0U],
                                                vr::ecs::Float3{.x = 0.45F * std::cos(time_seconds * 0.42F),
                                                                .y = 2.75F + 0.12F * std::sin(time_seconds * 0.58F),
                                                                .z = 2.35F});
            TransformSystem3D::SetLocalRotationEulerXyz(light_transforms[0U],
                                                        -0.72F,
                                                        0.20F + 0.18F * std::sin(time_seconds * 0.37F),
                                                        0.0F);
            TransformSystem3D::SetLocalPosition(shadow_transforms[0U],
                                                vr::ecs::Float3{.x = 0.45F * std::cos(time_seconds * 0.42F),
                                                                .y = 2.75F + 0.12F * std::sin(time_seconds * 0.58F),
                                                                .z = 2.35F});
            TransformSystem3D::SetLocalRotationEulerXyz(shadow_transforms[0U],
                                                        -0.72F,
                                                        0.20F + 0.18F * std::sin(time_seconds * 0.37F),
                                                        0.0F);

            char runtime_text[96]{};
            std::snprintf(runtime_text,
                          sizeof(runtime_text),
                          "Unified Scene | Frame:%llu",
                          static_cast<unsigned long long>(frame_index));
            (void)TextSystem3D::SetText(text_component, runtime_text);

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
            skeletal_outputs[0U].revision = static_cast<std::uint32_t>(frame_index + 2U);
            skeletal_outputs[0U].sampled_joint_count = 1U;

            morph_weight_storage[0U] = 0.5F + 0.5F * wave;
            morph_weight_storage[1U] = 0.25F + 0.25F * secondary_wave;
            morph_outputs[0U].revision = static_cast<std::uint32_t>(frame_index + 2U);
            morph_outputs[0U].sampled_weight_count = 2U;

            vertex_deform_parameter_storage[0U] = vr::ecs::Float4{
                .x = 0.08F,
                .y = 0.0F,
                .z = 1.0F,
                .w = 0.0F,
            };
            vertex_deform_parameter_storage[1U] = vr::ecs::Float4{
                .x = time_seconds * 1.15F,
                .y = 3.0F,
                .z = 0.0F,
                .w = 0.02F * wave,
            };
            vertex_deform_outputs[0U].revision = static_cast<std::uint32_t>(frame_index + 2U);
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
                                                               extent,
                                                               frame_index);
            ApplySceneSkyEnvironment(main_scene_packet, k_sky_environment_texture_id);
            recorder.SetFramePacket(&main_scene_packet);

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
                const auto geometry_stats = geometry_renderer.Stats();
                const auto environment_stats = recorder.EnvironmentPass().Stats();
                const auto surface_stats = surface_renderer.Stats();
                const auto text_stats = text_renderer.Stats();
                const auto bloom_stats = recorder.PostStack().Stats();
                const auto shadow_stats = shadow_renderer.Stats();
                const auto pool_stats = runtime.TargetPool().Stats();
                std::cout << "FPS:" << fps
                          << " Frame:" << frame_index
                          << " | Env Draw:" << environment_stats.draw_call_count
                          << " | G Draw:" << geometry_stats.draw_call_count
                          << " Inst:" << geometry_stats.instance_count
                          << " Light:" << geometry_stats.visible_light_count
                          << " ShadowView:" << geometry_stats.shadow_view_count
                          << " | S Draw:" << surface_stats.draw_call_count
                          << " Inst:" << surface_stats.instance_count
                          << " | T Draw:" << text_stats.draw_call_count
                          << " Inst:" << text_stats.instance_count
                          << " | Shadow Draw:" << shadow_stats.draw_call_count
                          << " AtlasPass:" << shadow_stats.atlas_layer_draw_pass_count
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

        recorder.Shutdown(runtime.Context());
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
        std::cerr << "sdl_scene_3d_unified_demo failed: " << exception_.what() << '\n';

        if (runtime_initialized && runtime.IsInitialized() && recorder.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
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
        return 1;
    }

    return 0;
}
