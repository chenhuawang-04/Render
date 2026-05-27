#pragma once

#include "vr/ecs/component/spatial_types.hpp"
#include "vr/runtime/runtime_ingress_ids.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace vr::scene {

enum class SkyEnvironmentMode : std::uint8_t {
    none = 0U,
    solid_color = 1U,
    gradient = 2U,
    cubemap = 3U,
    equirectangular_hdr = 4U,
    procedural_atmosphere = 5U,
};

enum class SkyEnvironmentDrawOrder : std::uint8_t {
    before_opaque = 0U,
    after_opaque_depth_tested = 1U,
};

struct SkyEnvironment final {
    SkyEnvironmentMode mode;

    asset::TextureId sky_texture_id{};
    geometry::GeometryAppearanceId sky_appearance_id{};

    asset::TextureId irradiance_texture_id{};
    asset::TextureId prefiltered_texture_id{};
    asset::TextureId brdf_lut_texture_id{};

    ecs::Float4 zenith_color;
    ecs::Float4 horizon_color;
    ecs::Float4 ground_color;
    ecs::Float4 tint;

    float exposure;
    float sky_intensity;
    float diffuse_ibl_intensity;
    float specular_ibl_intensity;
    float rotation_y;
    float max_specular_lod;
    SkyEnvironmentDrawOrder draw_order;

    float sun_elevation;
    float sun_azimuth;
    float atmosphere_density;
    float mie_scattering;
    float rayleigh_scattering;

    std::uint32_t flags;
    std::uint32_t revision;
};

struct SkyEnvironmentGpuHandle final {
    std::uint32_t index;
    std::uint32_t generation;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return index != 0U;
    }
};

struct SkyEnvironmentRenderState final {
    SkyEnvironmentMode mode;

    asset::TextureId sky_texture_id{};
    asset::TextureId irradiance_texture_id{};
    asset::TextureId prefiltered_texture_id{};
    asset::TextureId brdf_lut_texture_id{};

    ecs::Float4 zenith_color;
    ecs::Float4 horizon_color;
    ecs::Float4 ground_color;
    ecs::Float4 tint;
    std::array<ecs::Float4, 9U> sh9;

    float exposure;
    float sky_intensity;
    float diffuse_ibl_intensity;
    float specular_ibl_intensity;
    float rotation_y;
    float max_specular_lod;
    SkyEnvironmentDrawOrder draw_order;
    float sun_elevation;
    float sun_azimuth;
    float atmosphere_density;
    float mie_scattering;
    float rayleigh_scattering;

    std::uint32_t flags;
    std::uint32_t revision;
};

static_assert(std::is_standard_layout_v<SkyEnvironment>);
static_assert(std::is_trivially_copyable_v<SkyEnvironment>);
static_assert(std::is_standard_layout_v<SkyEnvironmentGpuHandle>);
static_assert(std::is_trivially_copyable_v<SkyEnvironmentGpuHandle>);
static_assert(std::is_standard_layout_v<SkyEnvironmentRenderState>);
static_assert(std::is_trivially_copyable_v<SkyEnvironmentRenderState>);

} // namespace vr::scene

