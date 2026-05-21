#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/geometry_mesh_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/geometry/geometry_renderer_3d.hpp"
#include "vr/geometry/geometry_resource_host.hpp"
#include "vr/geometry/geometry_upload_host.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/runtime/crash_tracer_support.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;
using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;

using GeometryMeshSystem3D = vr::ecs::GeometryMeshSystem;
using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

constexpr float k_pi = 3.14159265358979323846F;
constexpr std::uint32_t k_sphere_geometry_id = 9001U;
constexpr std::uint32_t k_grid_columns = 6U;
constexpr std::uint32_t k_grid_rows = 5U;
constexpr std::uint32_t k_grid_sphere_count = k_grid_columns * k_grid_rows;
constexpr std::uint32_t k_feature_sphere_count = 3U;
constexpr std::uint32_t k_total_sphere_count = k_grid_sphere_count + k_feature_sphere_count;
constexpr std::uint32_t k_texture_base_color_id = 9101U;
constexpr std::uint32_t k_texture_normal_id = 9102U;
constexpr std::uint32_t k_texture_orm_id = 9103U;
constexpr std::uint32_t k_texture_occlusion_id = 9104U;
constexpr std::uint32_t k_texture_emissive_id = 9105U;

struct Float3Value final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct SphereMeshData final {
    std::vector<vr::geometry::GeometryMeshVertex> vertices{};
    std::vector<std::uint32_t> indices{};
    std::array<vr::geometry::GeometrySubmeshRange, 1U> submeshes{};
};

[[nodiscard]] constexpr vr::ecs::AppearanceHandle MakeStaticAppearanceHandle(
    std::uint32_t index_) noexcept {
    return vr::ecs::AppearanceHandle{
        .index = index_,
        .generation = 1U,
    };
}

[[nodiscard]] constexpr float Lerp(float a_, float b_, float t_) noexcept {
    return a_ + (b_ - a_) * t_;
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

[[nodiscard]] std::uint8_t Float01ToByte(float value_) noexcept {
    const float clamped = std::clamp(value_, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
}

[[nodiscard]] constexpr Float3Value Subtract(Float3Value lhs_, Float3Value rhs_) noexcept {
    return Float3Value{
        .x = lhs_.x - rhs_.x,
        .y = lhs_.y - rhs_.y,
        .z = lhs_.z - rhs_.z,
    };
}

[[nodiscard]] constexpr Float3Value Cross(Float3Value lhs_, Float3Value rhs_) noexcept {
    return Float3Value{
        .x = lhs_.y * rhs_.z - lhs_.z * rhs_.y,
        .y = lhs_.z * rhs_.x - lhs_.x * rhs_.z,
        .z = lhs_.x * rhs_.y - lhs_.y * rhs_.x,
    };
}

[[nodiscard]] constexpr float Dot(Float3Value lhs_, Float3Value rhs_) noexcept {
    return lhs_.x * rhs_.x + lhs_.y * rhs_.y + lhs_.z * rhs_.z;
}

[[nodiscard]] float Length(Float3Value value_) noexcept {
    return std::sqrt(Dot(value_, value_));
}

[[nodiscard]] Float3Value Normalize(Float3Value value_) noexcept {
    const float length = Length(value_);
    if (length <= 1e-6F) {
        return Float3Value{.x = 0.0F, .y = 0.0F, .z = 1.0F};
    }
    return Float3Value{
        .x = value_.x / length,
        .y = value_.y / length,
        .z = value_.z / length,
    };
}

[[nodiscard]] Float3Value ReadPosition(const vr::geometry::GeometryMeshVertex& vertex_) noexcept {
    return Float3Value{
        .x = vertex_.position_x,
        .y = vertex_.position_y,
        .z = vertex_.position_z,
    };
}

void FillBaseColorTexture(std::vector<std::uint32_t>& pixels_,
                          std::uint32_t width_,
                          std::uint32_t height_) {
    pixels_.resize(static_cast<std::size_t>(width_) * height_);
    for (std::uint32_t y = 0U; y < height_; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(std::max(height_ - 1U, 1U));
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(std::max(width_ - 1U, 1U));
            const float grid = (std::sin(u * 18.0F * k_pi) * std::sin(v * 18.0F * k_pi) + 1.0F) * 0.5F;
            const float edge = std::pow(std::abs(2.0F * u - 1.0F), 3.0F) * 0.15F +
                               std::pow(std::abs(2.0F * v - 1.0F), 3.0F) * 0.15F;
            const float warm = std::clamp(0.65F + 0.20F * (1.0F - v) + 0.10F * grid - edge, 0.0F, 1.0F);
            const float green = std::clamp(0.56F + 0.16F * u + 0.08F * grid - edge * 0.7F, 0.0F, 1.0F);
            const float blue = std::clamp(0.46F + 0.10F * (1.0F - u) + 0.05F * grid - edge * 0.6F, 0.0F, 1.0F);
            pixels_[static_cast<std::size_t>(y) * width_ + x] = PackRgba8(
                Float01ToByte(warm),
                Float01ToByte(green),
                Float01ToByte(blue),
                255U);
        }
    }
}

void FillNormalTexture(std::vector<std::uint32_t>& pixels_,
                       std::uint32_t width_,
                       std::uint32_t height_) {
    pixels_.resize(static_cast<std::size_t>(width_) * height_);
    constexpr float strength = 1.35F;
    constexpr float frequency_u = 6.0F;
    constexpr float frequency_v = 8.0F;
    for (std::uint32_t y = 0U; y < height_; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(std::max(height_ - 1U, 1U));
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(std::max(width_ - 1U, 1U));
            const float angle_u = u * 2.0F * k_pi * frequency_u;
            const float angle_v = v * 2.0F * k_pi * frequency_v;
            const float dh_du = std::cos(angle_u) * std::cos(angle_v) * frequency_u +
                                0.35F * std::cos(angle_u * 0.5F + angle_v * 1.35F) * 0.5F * frequency_u;
            const float dh_dv = -std::sin(angle_u) * std::sin(angle_v) * frequency_v +
                                0.35F * std::cos(angle_u * 0.5F + angle_v * 1.35F) * 1.35F * frequency_v;
            const Float3Value normal = Normalize(Float3Value{
                .x = -dh_du * 0.10F * strength,
                .y = -dh_dv * 0.10F * strength,
                .z = 1.0F,
            });
            pixels_[static_cast<std::size_t>(y) * width_ + x] = PackRgba8(
                Float01ToByte(normal.x * 0.5F + 0.5F),
                Float01ToByte(normal.y * 0.5F + 0.5F),
                Float01ToByte(normal.z * 0.5F + 0.5F),
                255U);
        }
    }
}

void FillOrmTexture(std::vector<std::uint32_t>& pixels_,
                    std::uint32_t width_,
                    std::uint32_t height_) {
    pixels_.resize(static_cast<std::size_t>(width_) * height_);
    for (std::uint32_t y = 0U; y < height_; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(std::max(height_ - 1U, 1U));
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(std::max(width_ - 1U, 1U));
            const float ao = std::clamp(0.55F + 0.45F * std::cos((u - 0.5F) * k_pi), 0.0F, 1.0F);
            const float roughness =
                std::clamp(0.18F + 0.72F * u + 0.08F * std::sin(v * 8.0F * k_pi), 0.04F, 1.0F);
            const float metallic =
                std::clamp(0.15F + 0.80F * (1.0F - v) + 0.05F * std::sin(u * 6.0F * k_pi), 0.0F, 1.0F);
            pixels_[static_cast<std::size_t>(y) * width_ + x] = PackRgba8(
                Float01ToByte(ao),
                Float01ToByte(roughness),
                Float01ToByte(metallic),
                255U);
        }
    }
}

void FillOcclusionTexture(std::vector<std::uint32_t>& pixels_,
                          std::uint32_t width_,
                          std::uint32_t height_) {
    pixels_.resize(static_cast<std::size_t>(width_) * height_);
    for (std::uint32_t y = 0U; y < height_; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(std::max(height_ - 1U, 1U));
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(std::max(width_ - 1U, 1U));
            const float dx = u * 2.0F - 1.0F;
            const float dy = v * 2.0F - 1.0F;
            const float radius = std::sqrt(dx * dx + dy * dy);
            const float ring = 0.55F + 0.45F * std::sin((u + v) * 8.0F * k_pi);
            const float ao = std::clamp(0.35F + 0.65F * (1.0F - std::clamp(radius, 0.0F, 1.0F)) * ring,
                                        0.0F,
                                        1.0F);
            const std::uint8_t ao_byte = Float01ToByte(ao);
            pixels_[static_cast<std::size_t>(y) * width_ + x] = PackRgba8(ao_byte, ao_byte, ao_byte, 255U);
        }
    }
}

void FillEmissiveTexture(std::vector<std::uint32_t>& pixels_,
                         std::uint32_t width_,
                         std::uint32_t height_) {
    pixels_.resize(static_cast<std::size_t>(width_) * height_);
    for (std::uint32_t y = 0U; y < height_; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(std::max(height_ - 1U, 1U));
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(std::max(width_ - 1U, 1U));
            const float meridian = std::pow(std::max(0.0F, std::cos((u - 0.5F) * 12.0F * k_pi)), 16.0F);
            const float equator = std::pow(std::max(0.0F, std::cos((v - 0.5F) * 6.0F * k_pi)), 10.0F);
            const float sparkle = std::pow(std::max(0.0F, std::sin((u + v) * 18.0F * k_pi)), 24.0F);
            const float energy = std::clamp(0.10F + 0.90F * std::max(meridian * equator, sparkle), 0.0F, 1.0F);
            pixels_[static_cast<std::size_t>(y) * width_ + x] = PackRgba8(
                Float01ToByte(0.30F * energy),
                Float01ToByte(0.78F * energy),
                Float01ToByte(energy),
                255U);
        }
    }
}

[[nodiscard]] SphereMeshData BuildUvSphereMesh(std::uint32_t longitude_segments_,
                                               std::uint32_t latitude_segments_) {
    SphereMeshData mesh{};
    const std::uint32_t longitudes = std::max(longitude_segments_, 3U);
    const std::uint32_t latitudes = std::max(latitude_segments_, 2U);

    mesh.vertices.reserve(static_cast<std::size_t>(longitudes + 1U) * (latitudes + 1U));
    for (std::uint32_t latitude = 0U; latitude <= latitudes; ++latitude) {
        const float v = static_cast<float>(latitude) / static_cast<float>(latitudes);
        const float theta = v * k_pi;
        const float sin_theta = std::sin(theta);
        const float cos_theta = std::cos(theta);
        for (std::uint32_t longitude = 0U; longitude <= longitudes; ++longitude) {
            const float u = static_cast<float>(longitude) / static_cast<float>(longitudes);
            const float phi = u * 2.0F * k_pi;
            const float sin_phi = std::sin(phi);
            const float cos_phi = std::cos(phi);
            const Float3Value normal{
                .x = sin_theta * cos_phi,
                .y = cos_theta,
                .z = sin_theta * sin_phi,
            };

            vr::geometry::GeometryMeshVertex vertex{};
            vertex.position_x = normal.x;
            vertex.position_y = normal.y;
            vertex.position_z = normal.z;
            vertex.normal_x = normal.x;
            vertex.normal_y = normal.y;
            vertex.normal_z = normal.z;
            vertex.uv_u = u;
            vertex.uv_v = 1.0F - v;
            mesh.vertices.push_back(vertex);
        }
    }

    const auto append_triangle = [&](std::uint32_t i0_,
                                     std::uint32_t i1_,
                                     std::uint32_t i2_) {
        const Float3Value p0 = ReadPosition(mesh.vertices[i0_]);
        const Float3Value p1 = ReadPosition(mesh.vertices[i1_]);
        const Float3Value p2 = ReadPosition(mesh.vertices[i2_]);
        const Float3Value edge01 = Subtract(p1, p0);
        const Float3Value edge02 = Subtract(p2, p0);
        const Float3Value triangle_normal = Cross(edge01, edge02);
        const Float3Value centroid = Normalize(Float3Value{
            .x = (p0.x + p1.x + p2.x) / 3.0F,
            .y = (p0.y + p1.y + p2.y) / 3.0F,
            .z = (p0.z + p1.z + p2.z) / 3.0F,
        });
        if (Dot(triangle_normal, centroid) < 0.0F) {
            std::swap(i1_, i2_);
        }
        mesh.indices.push_back(i0_);
        mesh.indices.push_back(i1_);
        mesh.indices.push_back(i2_);
    };

    for (std::uint32_t latitude = 0U; latitude < latitudes; ++latitude) {
        for (std::uint32_t longitude = 0U; longitude < longitudes; ++longitude) {
            const std::uint32_t i0 = latitude * (longitudes + 1U) + longitude;
            const std::uint32_t i1 = i0 + 1U;
            const std::uint32_t i2 = i0 + (longitudes + 1U);
            const std::uint32_t i3 = i2 + 1U;

            append_triangle(i0, i2, i1);
            append_triangle(i1, i2, i3);
        }
    }

    mesh.submeshes[0U] = vr::geometry::GeometrySubmeshRange{
        .first_index = 0U,
        .index_count = static_cast<std::uint32_t>(mesh.indices.size()),
        .vertex_offset = 0,
        .reserved0 = 0U,
    };
    return mesh;
}

void UploadGeometryImage(Runtime& runtime_,
                         vr::geometry::GeometryImageHost& image_host_,
                         std::uint32_t image_id_,
                         const std::vector<std::uint32_t>& pixels_,
                         std::uint32_t width_,
                         std::uint32_t height_) {
    vr::geometry::GeometryImageUploadInfo upload{};
    upload.image_id = image_id_;
    upload.pixels = pixels_.data();
    upload.width = width_;
    upload.height = height_;
    upload.format = VK_FORMAT_R8G8B8A8_UNORM;
    upload.bytes_per_pixel = 4U;
    upload.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    upload.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    upload.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_host_.UploadImage(runtime_.Context(),
                            runtime_.Upload(),
                            0U,
                            0U,
                            0U,
                            upload);
}

void InitializeGeometryComponent(Geometry3D& component_,
                                 Appearance3D& appearance_,
                                 std::uint32_t appearance_index_) {
    GeometryMeshSystem3D::Initialize(component_);
    GeometryMeshSystem3D::SetMeshRoute(component_, k_sphere_geometry_id, 0U, 0U);
    GeometryMeshSystem3D::SetTopology(component_, vr::ecs::Geometry3DTopology::triangles);
    GeometryMeshSystem3D::SetBounds(component_,
                                    vr::ecs::Float3{.x = -1.0F, .y = -1.0F, .z = -1.0F},
                                    vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
    GeometrySystem3D::SetDepthBin(component_, 24U);

    AppearanceSystem3D::Initialize(appearance_);
    AppearanceSystem3D::SetDepthTest(appearance_, true);
    AppearanceSystem3D::SetDepthWrite(appearance_, true);
    AppearanceSystem3D::SetDoubleSided(appearance_, false);
    AppearanceSystem3D::SetCastShadow(appearance_, false);
    AppearanceSystem3D::SetReceiveShadow(appearance_, true);
    AppearanceSystem3D::SetShadingModel(appearance_, vr::ecs::AppearanceShadingModel3D::lit_pbr);

    (void)GeometrySystem3D::SetAppearanceRuntimeLink(component_,
                                                     MakeStaticAppearanceHandle(appearance_index_),
                                                     0ULL,
                                                     0ULL,
                                                     0ULL,
                                                     &appearance_.style);
}

void ApplyProceduralSkyEnvironment(vr::render::RenderScenePacket3D& packet_) {
    packet_.extra.environment_gpu = {};
    packet_.extra.ibl_environment_id = 0U;
    auto& environment = packet_.extra.environment;
    environment = {};
    environment.mode = vr::scene::SkyEnvironmentMode::procedural_atmosphere;
    environment.zenith_color = vr::ecs::Float4{.x = 0.07F, .y = 0.17F, .z = 0.42F, .w = 1.0F};
    environment.horizon_color = vr::ecs::Float4{.x = 0.78F, .y = 0.54F, .z = 0.24F, .w = 1.0F};
    environment.ground_color = vr::ecs::Float4{.x = 0.08F, .y = 0.07F, .z = 0.06F, .w = 1.0F};
    environment.tint = vr::ecs::Float4{.x = 1.0F, .y = 0.98F, .z = 1.03F, .w = 1.0F};
    environment.exposure = 1.05F;
    environment.sky_intensity = 1.15F;
    environment.diffuse_ibl_intensity = 1.10F;
    environment.specular_ibl_intensity = 1.18F;
    environment.rotation_y = 0.25F;
    environment.max_specular_lod = -1.0F;
    environment.draw_order = vr::scene::SkyEnvironmentDrawOrder::before_opaque;
    environment.sun_elevation = 0.46F;
    environment.sun_azimuth = -0.72F;
    environment.atmosphere_density = 1.35F;
    environment.mie_scattering = 2.1F;
    environment.rayleigh_scattering = 1.3F;
    environment.flags = 0U;
    environment.revision = 1U;
    vr::render::RefreshRenderScenePacketSignature(packet_);
}

[[nodiscard]] vr::render::SceneRecorder3DCreateInfo BuildRecorderCreateInfo() noexcept {
    vr::render::SceneRecorder3DCreateInfo create_info{};
    create_info.scene_target.color_debug_name = "PbrMaterialGridColor";
    create_info.scene_target.depth_debug_name = "PbrMaterialGridDepth";
    create_info.scene_target.enable_depth = true;
    create_info.scene_target.color_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.depth_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.clear_color = VkClearColorValue{{0.025F, 0.028F, 0.035F, 1.0F}};
    create_info.bloom.clear_swapchain = true;
    create_info.bloom.clear_color = {{0.015F, 0.017F, 0.022F, 1.0F}};
    create_info.bloom.enable_reinhard_tonemap = true;
    create_info.bloom.exposure = 1.0F;
    create_info.bloom.apply_manual_gamma = false;
    create_info.bloom.bloom_threshold = 0.75F;
    create_info.bloom.bloom_knee = 0.42F;
    create_info.bloom.bloom_intensity = 0.90F;
    create_info.bloom.blur_filter_scale = 1.06F;
    create_info.bloom.downsample_scale = 0.5F;
    create_info.reserve_scene_renderer_count = 1U;
    create_info.reserve_overlay_renderer_count = 0U;
    return create_info;
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
    const std::uint32_t max_frames = ParseMaxFrames(argc_, argv_);

    Runtime runtime{};
    vr::geometry::GeometryResourceHost geometry_resource_host{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryImageHost geometry_image_host{};
    vr::render::SceneRecorder3D recorder{};
    vr::geometry::GeometryRenderer3D geometry_renderer{};

    bool runtime_initialized = false;
    bool geometry_resource_host_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_image_host_initialized = false;
    bool geometry_renderer_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "Vulkan SDL3 PBR Material Grid Demo";
        create_info.platform.window.width = 1440;
        create_info.platform.window.height = 900;
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

        recorder.Initialize(BuildRecorderCreateInfo());
        recorder.BindRuntime(runtime);

        vr::geometry::GeometryResourceHostCreateInfo resource_create_info{};
        resource_create_info.reserve_mesh_count = 4U;
        resource_create_info.reserve_submesh_count = 8U;
        resource_create_info.reserve_reusable_buffer_count = 4U;
        geometry_resource_host.Initialize(runtime.Context(),
                                          runtime.GpuMemory(),
                                          resource_create_info);
        geometry_resource_host_initialized = true;

        vr::geometry::GeometryUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_3d_instance_buffer_bytes = 1024U * 1024U;
        geometry_upload_host.Initialize(runtime.Context(),
                                        runtime.GpuMemory(),
                                        upload_create_info);
        geometry_upload_host_initialized = true;

        vr::geometry::GeometryImageHostCreateInfo image_create_info{};
        image_create_info.reserve_image_count = 16U;
        image_create_info.reserve_retired_image_count = 16U;
        geometry_image_host.Initialize(runtime.Context(),
                                       runtime.GpuMemory(),
                                       image_create_info);
        geometry_image_host_initialized = true;

        const SphereMeshData sphere_mesh = BuildUvSphereMesh(48U, 24U);

        runtime.Upload().BeginFrame(runtime.Context(), 0U);

        vr::geometry::GeometryMeshUploadInfo mesh_upload{};
        mesh_upload.geometry_id = k_sphere_geometry_id;
        mesh_upload.vertices = sphere_mesh.vertices.data();
        mesh_upload.vertex_count = static_cast<std::uint32_t>(sphere_mesh.vertices.size());
        mesh_upload.indices = sphere_mesh.indices.data();
        mesh_upload.index_count = static_cast<std::uint32_t>(sphere_mesh.indices.size());
        mesh_upload.submeshes = sphere_mesh.submeshes.data();
        mesh_upload.submesh_count = static_cast<std::uint32_t>(sphere_mesh.submeshes.size());
        mesh_upload.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        mesh_upload.bounds_min = vr::ecs::Float3{.x = -1.0F, .y = -1.0F, .z = -1.0F};
        mesh_upload.bounds_max = vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F};
        geometry_resource_host.UploadMesh(runtime.Context(),
                                          runtime.Upload(),
                                          0U,
                                          0U,
                                          0U,
                                          mesh_upload);

        constexpr std::uint32_t texture_width = 256U;
        constexpr std::uint32_t texture_height = 256U;
        std::vector<std::uint32_t> base_color_pixels{};
        std::vector<std::uint32_t> normal_pixels{};
        std::vector<std::uint32_t> orm_pixels{};
        std::vector<std::uint32_t> occlusion_pixels{};
        std::vector<std::uint32_t> emissive_pixels{};
        FillBaseColorTexture(base_color_pixels, texture_width, texture_height);
        FillNormalTexture(normal_pixels, texture_width, texture_height);
        FillOrmTexture(orm_pixels, texture_width, texture_height);
        FillOcclusionTexture(occlusion_pixels, texture_width, texture_height);
        FillEmissiveTexture(emissive_pixels, texture_width, texture_height);

        UploadGeometryImage(runtime,
                            geometry_image_host,
                            k_texture_base_color_id,
                            base_color_pixels,
                            texture_width,
                            texture_height);
        UploadGeometryImage(runtime,
                            geometry_image_host,
                            k_texture_normal_id,
                            normal_pixels,
                            texture_width,
                            texture_height);
        UploadGeometryImage(runtime,
                            geometry_image_host,
                            k_texture_orm_id,
                            orm_pixels,
                            texture_width,
                            texture_height);
        UploadGeometryImage(runtime,
                            geometry_image_host,
                            k_texture_occlusion_id,
                            occlusion_pixels,
                            texture_width,
                            texture_height);
        UploadGeometryImage(runtime,
                            geometry_image_host,
                            k_texture_emissive_id,
                            emissive_pixels,
                            texture_width,
                            texture_height);

        const vr::render::UploadEndFrameResult upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }

        geometry_resource_host.BeginFrame(runtime.Context(), 0U);
        geometry_image_host.BeginFrame(runtime.Context(), 0U);

        vr::resource::SamplerId sampler_id{};
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
            sampler_id = runtime.Sampler().RegisterSampler(runtime.Context(), sampler_desc);
        }

        std::vector<Geometry3D> geometry_components(k_total_sphere_count);
        std::vector<Appearance3D> appearance_components(k_total_sphere_count);
        std::vector<Transform3D> transforms(k_total_sphere_count);
        std::vector<Bounds3D> bounds(k_total_sphere_count);

        const float grid_spacing = 1.35F;
        const float grid_origin_x = -0.5F * static_cast<float>(k_grid_columns - 1U) * grid_spacing;
        const float grid_origin_y = 0.5F * static_cast<float>(k_grid_rows - 1U) * grid_spacing + 1.0F;
        const vr::ecs::Rgba8 grid_base_color{214U, 186U, 132U, 255U};

        for (std::uint32_t index = 0U; index < k_total_sphere_count; ++index) {
            InitializeGeometryComponent(geometry_components[index], appearance_components[index], index);

            TransformSystem3D::Initialize(transforms[index]);
            BoundsSystem3D::Initialize(bounds[index]);
            BoundsSystem3D::SetLocalCenterExtents(bounds[index],
                                                  vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                  vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        }

        for (std::uint32_t row = 0U; row < k_grid_rows; ++row) {
            const float metallic = (k_grid_rows > 1U)
                ? static_cast<float>(row) / static_cast<float>(k_grid_rows - 1U)
                : 0.0F;
            for (std::uint32_t column = 0U; column < k_grid_columns; ++column) {
                const std::uint32_t index = row * k_grid_columns + column;
                const float roughness = Lerp(0.04F,
                                             1.0F,
                                             (k_grid_columns > 1U)
                                                 ? static_cast<float>(column) /
                                                       static_cast<float>(k_grid_columns - 1U)
                                                 : 0.0F);

                Appearance3D& appearance = appearance_components[index];
                AppearanceSystem3D::SetBaseColor(appearance, grid_base_color);
                AppearanceSystem3D::SetMetallic(appearance, metallic);
                AppearanceSystem3D::SetRoughness(appearance, roughness);
                AppearanceSystem3D::SetNormalScale(appearance, 1.0F);
                AppearanceSystem3D::SetOcclusionStrength(appearance, 1.0F);
                AppearanceSystem3D::SetEmissiveIntensity(appearance, 0.0F);
                AppearanceSystem3D::SetSurfaceSamplerId(appearance, sampler_id.value);

                TransformSystem3D::SetLocalPosition(
                    transforms[index],
                    vr::ecs::Float3{
                        .x = grid_origin_x + static_cast<float>(column) * grid_spacing,
                        .y = grid_origin_y - static_cast<float>(row) * grid_spacing,
                        .z = 0.0F,
                    });
                TransformSystem3D::SetLocalScale(transforms[index],
                                                 vr::ecs::Float3{.x = 0.48F, .y = 0.48F, .z = 0.48F});
            }
        }

        const auto geometry_image_handle = [](std::uint32_t surface_id_) noexcept {
            return vr::render::MakeAppearanceSampledSurfaceHandle(
                surface_id_,
                vr::render::AppearanceSampledSurfaceDomain::geometry_image);
        };

        const std::uint32_t textured_feature_begin = k_grid_sphere_count;

        {
            const std::uint32_t index = textured_feature_begin + 0U;
            Appearance3D& appearance = appearance_components[index];
            AppearanceSystem3D::SetBaseColor(appearance, vr::ecs::Rgba8{245U, 236U, 226U, 255U});
            AppearanceSystem3D::SetMetallic(appearance, 1.0F);
            AppearanceSystem3D::SetRoughness(appearance, 1.0F);
            AppearanceSystem3D::SetNormalScale(appearance, 1.35F);
            AppearanceSystem3D::SetOcclusionStrength(appearance, 1.0F);
            AppearanceSystem3D::SetSurfaceSamplerId(appearance, sampler_id.value);
            AppearanceSystem3D::SetBaseColorSurface(appearance, geometry_image_handle(k_texture_base_color_id));
            AppearanceSystem3D::SetNormalSurface(appearance, geometry_image_handle(k_texture_normal_id));
            AppearanceSystem3D::SetMetalRoughSurface(appearance, geometry_image_handle(k_texture_orm_id));
            AppearanceSystem3D::SetOcclusionSurface(appearance, geometry_image_handle(k_texture_occlusion_id));

            TransformSystem3D::SetLocalPosition(
                transforms[index],
                vr::ecs::Float3{.x = -2.2F, .y = -3.1F, .z = 0.15F});
            TransformSystem3D::SetLocalScale(transforms[index],
                                             vr::ecs::Float3{.x = 0.72F, .y = 0.72F, .z = 0.72F});
        }

        {
            const std::uint32_t index = textured_feature_begin + 1U;
            Appearance3D& appearance = appearance_components[index];
            AppearanceSystem3D::SetBaseColor(appearance, vr::ecs::Rgba8{210U, 224U, 255U, 255U});
            AppearanceSystem3D::SetMetallic(appearance, 0.15F);
            AppearanceSystem3D::SetRoughness(appearance, 0.55F);
            AppearanceSystem3D::SetNormalScale(appearance, 1.10F);
            AppearanceSystem3D::SetOcclusionStrength(appearance, 0.85F);
            AppearanceSystem3D::SetEmissiveColor(appearance, vr::ecs::Rgba8{124U, 220U, 255U, 255U});
            AppearanceSystem3D::SetEmissiveIntensity(appearance, 2.8F);
            AppearanceSystem3D::SetSurfaceSamplerId(appearance, sampler_id.value);
            AppearanceSystem3D::SetBaseColorSurface(appearance, geometry_image_handle(k_texture_base_color_id));
            AppearanceSystem3D::SetNormalSurface(appearance, geometry_image_handle(k_texture_normal_id));
            AppearanceSystem3D::SetEmissiveSurface(appearance, geometry_image_handle(k_texture_emissive_id));

            TransformSystem3D::SetLocalPosition(
                transforms[index],
                vr::ecs::Float3{.x = 0.0F, .y = -3.1F, .z = 0.15F});
            TransformSystem3D::SetLocalScale(transforms[index],
                                             vr::ecs::Float3{.x = 0.72F, .y = 0.72F, .z = 0.72F});
        }

        {
            const std::uint32_t index = textured_feature_begin + 2U;
            Appearance3D& appearance = appearance_components[index];
            AppearanceSystem3D::SetBaseColor(appearance, vr::ecs::Rgba8{184U, 154U, 104U, 255U});
            AppearanceSystem3D::SetMetallic(appearance, 0.95F);
            AppearanceSystem3D::SetRoughness(appearance, 0.38F);
            AppearanceSystem3D::SetNormalScale(appearance, 1.55F);
            AppearanceSystem3D::SetOcclusionStrength(appearance, 1.0F);
            AppearanceSystem3D::SetSurfaceSamplerId(appearance, sampler_id.value);
            AppearanceSystem3D::SetBaseColorSurface(appearance, geometry_image_handle(k_texture_base_color_id));
            AppearanceSystem3D::SetNormalSurface(appearance, geometry_image_handle(k_texture_normal_id));
            AppearanceSystem3D::SetMetalRoughSurface(appearance, geometry_image_handle(k_texture_orm_id));
            AppearanceSystem3D::SetOcclusionSurface(appearance, geometry_image_handle(k_texture_occlusion_id));
            AppearanceSystem3D::SetEmissiveSurface(appearance, geometry_image_handle(k_texture_emissive_id));
            AppearanceSystem3D::SetEmissiveColor(appearance, vr::ecs::Rgba8{255U, 172U, 92U, 255U});
            AppearanceSystem3D::SetEmissiveIntensity(appearance, 0.9F);

            TransformSystem3D::SetLocalPosition(
                transforms[index],
                vr::ecs::Float3{.x = 2.2F, .y = -3.1F, .z = 0.15F});
            TransformSystem3D::SetLocalScale(transforms[index],
                                             vr::ecs::Float3{.x = 0.72F, .y = 0.72F, .z = 0.72F});
        }

        TransformSystem3D::UpdateHierarchy(transforms.data(),
                                           static_cast<std::uint32_t>(transforms.size()));
        (void)BoundsSystem3D::UpdateAligned(bounds.data(),
                                            transforms.data(),
                                            static_cast<std::uint32_t>(bounds.size()));

        Camera3D camera{};
        CameraSystem3D::Initialize(camera);
        CameraSystem3D::SetProjectionMode(camera, vr::ecs::CameraProjectionMode::perspective);
        CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
        CameraSystem3D::SetVerticalFovRadians(camera, 48.0F * 0.01745329251994329577F);

        Transform3D camera_transform{};
        TransformSystem3D::Initialize(camera_transform);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.2F, .z = 11.25F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

        vr::geometry::GeometryRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(geometry_components.size());
        renderer_create_info.reserve_instance_count = 1024U;
        renderer_create_info.reserve_appearance_set_count = 128U;
        renderer_create_info.enable_depth = true;
        renderer_create_info.clear_depth = true;
        renderer_create_info.clear_swapchain = false;
        renderer_create_info.clear_color = {{0.025F, 0.028F, 0.035F, 1.0F}};
        renderer_create_info.directional_light_x = 0.55F;
        renderer_create_info.directional_light_y = -0.72F;
        renderer_create_info.directional_light_z = 0.42F;
        renderer_create_info.directional_light_intensity = 1.35F;
        geometry_renderer.Initialize(renderer_create_info);
        geometry_renderer_initialized = true;
        geometry_renderer.SetHosts(&geometry_resource_host, &geometry_upload_host);
        geometry_renderer.SetAppearanceHosts(nullptr, &geometry_image_host);
        geometry_renderer.SetAppearanceData(appearance_components.data(),
                                            static_cast<std::uint32_t>(appearance_components.size()));
        geometry_renderer.SetSceneData(geometry_components.data(),
                                       transforms.data(),
                                       static_cast<std::uint32_t>(geometry_components.size()),
                                       &camera,
                                       &camera_transform,
                                       bounds.data());

        recorder.RegisterOpaqueSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::single);

        vr::render::RenderView3D main_view{};
        vr::render::RenderScenePacket3D main_scene_packet{};
        vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                           main_scene_packet,
                                                           camera,
                                                           camera_transform,
                                                           runtime.Swapchain().Extent(),
                                                           0U);
        ApplyProceduralSkyEnvironment(main_scene_packet);
        recorder.SetFramePacket(&main_scene_packet);

        std::cout
            << "sdl_pbr_material_grid_demo running "
            << "(rows metallic 0->1 top-to-bottom, columns roughness 0.04->1 left-to-right, "
            << "bottom feature spheres validate base/normal/ORM/occlusion/emissive runtime paths). Close window to exit.\n";

        std::uint64_t frame_index = 0U;
        std::uint64_t fps_window_begin_ticks = SDL_GetTicks();
        std::uint32_t fps_window_frame_count = 0U;

        while (runtime.IsRunning()) {
            const std::uint64_t now_ticks = SDL_GetTicks();
            const float time_seconds = static_cast<float>(now_ticks) * 0.001F;
            const VkExtent2D extent = runtime.Swapchain().Extent();

            TransformSystem3D::SetLocalRotationEulerXyz(transforms[textured_feature_begin + 0U],
                                                        0.16F * std::sin(time_seconds * 0.70F),
                                                        0.55F * time_seconds,
                                                        0.0F);
            TransformSystem3D::SetLocalRotationEulerXyz(transforms[textured_feature_begin + 1U],
                                                        0.0F,
                                                        -0.68F * time_seconds,
                                                        0.12F * std::sin(time_seconds * 1.15F));
            TransformSystem3D::SetLocalRotationEulerXyz(transforms[textured_feature_begin + 2U],
                                                        -0.12F * std::sin(time_seconds * 0.83F),
                                                        0.46F * time_seconds,
                                                        0.0F);
            TransformSystem3D::UpdateHierarchy(transforms.data(),
                                               static_cast<std::uint32_t>(transforms.size()));

            vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                               main_scene_packet,
                                                               camera,
                                                               camera_transform,
                                                               extent,
                                                               frame_index);
            ApplyProceduralSkyEnvironment(main_scene_packet);
            recorder.SetFramePacket(&main_scene_packet);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            (void)tick_result;

            ++frame_index;
            ++fps_window_frame_count;
            if (max_frames > 0U && frame_index >= max_frames) {
                break;
            }

            const std::uint64_t fps_window_elapsed = now_ticks - fps_window_begin_ticks;
            if (fps_window_elapsed >= 1000U) {
                const float fps = (fps_window_elapsed > 0U)
                    ? (1000.0F * static_cast<float>(fps_window_frame_count) /
                       static_cast<float>(fps_window_elapsed))
                    : 0.0F;
                const auto stats = geometry_renderer.Stats();
                const auto bloom_stats = recorder.BloomStats();
                const auto env_stats = recorder.EnvironmentPass().Stats();
                std::cout << "FPS: " << fps
                          << " | Frame:" << frame_index
                          << " | Draw:" << stats.draw_call_count
                          << " Batch:" << stats.draw_batch_count
                          << " Visible:" << stats.visible_component_count
                          << " App:" << stats.appearance_visible_count
                          << " | Bloom C:" << bloom_stats.combine_draw_call_count
                          << " | Sky Prep:" << env_stats.prepare_count
                          << " Sky Draw:" << env_stats.draw_call_count
                          << '\n';
                fps_window_begin_ticks = now_ticks;
                fps_window_frame_count = 0U;
            }
        }
    } catch (const std::exception& exception_) {
        std::cerr << "sdl_pbr_material_grid_demo failed: " << exception_.what() << '\n';

        if (geometry_renderer_initialized) {
            geometry_renderer.Shutdown(runtime.Context());
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
            recorder.Shutdown(runtime.Context());
            runtime.Shutdown();
        }
        return 1;
    }

    if (geometry_renderer_initialized) {
        geometry_renderer.Shutdown(runtime.Context());
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
        recorder.Shutdown(runtime.Context());
        runtime.Shutdown();
    }
    return 0;
}
