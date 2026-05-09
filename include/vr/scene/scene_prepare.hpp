#pragma once

#include "vr/scene/scene.hpp"

namespace vr::scene {

template<ecs::DimensionTag DimT, typename BackgroundT>
class ScenePrepare;

template<>
class ScenePrepare<ecs::Dim2, SpriteBackground> final {
public:
    using SceneType = Scene2D;
    using RenderState = Background2DRenderState;

    [[nodiscard]] static RenderState ResolveBackground(
        const SpriteBackground& background_) noexcept {
        return RenderState{
            .mode = background_.mode,
            .image_id = background_.image_id,
            .sprite_id = background_.sprite_id,
            .surface_entity_id = background_.surface_entity_id,
            .color0 = background_.color0,
            .color1 = background_.color1,
            .opacity = background_.opacity,
            .layer = background_.layer,
            .flags = background_.flags,
            .revision = background_.revision,
        };
    }

    [[nodiscard]] static RenderState Resolve(
        const SceneType& scene_) noexcept {
        return ResolveBackground(scene_.background);
    }
};

template<>
class ScenePrepare<ecs::Dim3, SkyEnvironment> final {
public:
    using SceneType = Scene3D;
    using RenderState = SkyEnvironmentRenderState;

    [[nodiscard]] static RenderState ResolveEnvironment(
        const SkyEnvironment& environment_) noexcept {
        return RenderState{
            .mode = environment_.mode,
            .sky_texture_id = environment_.sky_texture_id,
            .irradiance_texture_id = environment_.irradiance_texture_id,
            .prefiltered_texture_id = environment_.prefiltered_texture_id,
            .brdf_lut_texture_id = environment_.brdf_lut_texture_id,
            .zenith_color = environment_.zenith_color,
            .horizon_color = environment_.horizon_color,
            .ground_color = environment_.ground_color,
            .tint = environment_.tint,
            .sh9 = {},
            .exposure = environment_.exposure,
            .sky_intensity = environment_.sky_intensity,
            .diffuse_ibl_intensity = environment_.diffuse_ibl_intensity,
            .specular_ibl_intensity = environment_.specular_ibl_intensity,
            .rotation_y = environment_.rotation_y,
            .max_specular_lod = environment_.max_specular_lod,
            .draw_order = environment_.draw_order,
            .sun_elevation = environment_.sun_elevation,
            .sun_azimuth = environment_.sun_azimuth,
            .atmosphere_density = environment_.atmosphere_density,
            .mie_scattering = environment_.mie_scattering,
            .rayleigh_scattering = environment_.rayleigh_scattering,
            .flags = environment_.flags,
            .revision = environment_.revision,
        };
    }

    [[nodiscard]] static RenderState Resolve(
        const SceneType& scene_) noexcept {
        return ResolveEnvironment(scene_.background);
    }
};

} // namespace vr::scene
