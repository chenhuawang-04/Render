#include "support/test_framework.hpp"
#include "vr/geometry/geometry_appearance_host.hpp"
#include "vr/geometry/geometry_appearance_resolver.hpp"
#include "vr/render/appearance_gpu_prepare.hpp"

#include <limits>

namespace {

VR_TEST_CASE(PbrAppearanceContract_geometry_appearance_desc_defaults_match_standard_pbr_baseline,
             "unit;pbr;appearance;contract") {
    const vr::geometry::GeometryAppearanceDesc desc{};

    VR_CHECK(desc.metallic_factor == 0.0F);
    VR_CHECK(desc.roughness_factor == 1.0F);
    VR_CHECK(desc.normal_scale == 1.0F);
    VR_CHECK(desc.occlusion_strength == 1.0F);
}

VR_TEST_CASE(PbrAppearanceContract_geometry_appearance_host_canonicalizes_minimal_pbr_factors,
             "unit;pbr;appearance;contract") {
    vr::geometry::GeometryAppearanceHost host{};
    host.Initialize({});

    vr::geometry::GeometryAppearanceDesc desc{};
    desc.appearance_id = 900U;
    desc.metallic_factor = -1.0F;
    desc.roughness_factor = std::numeric_limits<float>::quiet_NaN();
    desc.normal_scale = -7.0F;
    desc.occlusion_strength = 8.0F;

    host.UpsertAppearance(desc);
    const auto* record = host.FindAppearance(desc.appearance_id);
    VR_REQUIRE(record != nullptr);

    VR_CHECK(record->desc.metallic_factor == 0.0F);
    VR_CHECK(record->desc.roughness_factor == 1.0F);
    VR_CHECK(record->desc.normal_scale == 0.0F);
    VR_CHECK(record->desc.occlusion_strength == 1.0F);

    desc.metallic_factor = 0.82F;
    desc.roughness_factor = 0.02F;
    desc.normal_scale = 3.5F;
    desc.occlusion_strength = 0.35F;
    host.UpsertAppearance(desc);
    record = host.FindAppearance(desc.appearance_id);
    VR_REQUIRE(record != nullptr);

    VR_CHECK(record->desc.metallic_factor == 0.82F);
    VR_CHECK(record->desc.roughness_factor == 0.04F);
    VR_CHECK(record->desc.normal_scale == 3.5F);
    VR_CHECK(record->desc.occlusion_strength == 0.35F);

    host.Shutdown();
}

VR_TEST_CASE(PbrAppearanceContract_resolver_defaults_to_appearance_style_when_no_overrides_are_present,
             "unit;pbr;appearance;contract") {
    vr::ecs::AppearanceStyle3D style{};
    style.base_color = vr::ecs::Rgba8{24U, 48U, 96U, 200U};
    style.shading_model = vr::ecs::AppearanceShadingModel3D::lit_pbr;
    style.metallic = 0.33F;
    style.roughness = 0.61F;
    style.normal_scale = 1.25F;
    style.occlusion_strength = 1.0F;

    const vr::geometry::GeometryAppearanceResolvedState resolved =
        vr::geometry::ResolveGeometryFallbackAppearanceState(&style);

    VR_CHECK(resolved.albedo_color.r == 24U);
    VR_CHECK(resolved.albedo_color.g == 48U);
    VR_CHECK(resolved.albedo_color.b == 96U);
    VR_CHECK(resolved.albedo_color.a == 200U);
    VR_CHECK(resolved.metallic == 0.33F);
    VR_CHECK(resolved.roughness == 0.61F);
    VR_CHECK(resolved.normal_scale == 1.25F);
    VR_CHECK(resolved.occlusion_strength == 1.0F);
    VR_CHECK(!resolved.unlit);
}

VR_TEST_CASE(PbrAppearanceContract_resolver_applies_geometry_appearance_over_style_defaults,
             "unit;pbr;appearance;contract") {
    vr::ecs::AppearanceStyle3D style{};
    style.base_color = vr::ecs::Rgba8{255U, 255U, 255U, 255U};
    style.shading_model = vr::ecs::AppearanceShadingModel3D::unlit;
    style.metallic = 0.05F;
    style.roughness = 0.95F;
    style.normal_scale = 0.8F;

    vr::geometry::GeometryAppearanceDesc desc{};
    desc.metallic_factor = 0.72F;
    desc.roughness_factor = 0.18F;
    desc.normal_scale = 2.0F;
    desc.occlusion_strength = 0.45F;

    const vr::geometry::GeometryAppearanceResolvedState resolved =
        vr::geometry::ResolveGeometryFallbackAppearanceState(&style, &desc);

    VR_CHECK(resolved.metallic == 0.72F);
    VR_CHECK(resolved.roughness == 0.18F);
    VR_CHECK(resolved.normal_scale == 2.0F);
    VR_CHECK(resolved.occlusion_strength == 0.45F);
    VR_CHECK(resolved.unlit);
}

VR_TEST_CASE(PbrAppearanceContract_appearance_overlay_preserves_layered_resolution_order,
             "unit;pbr;appearance;contract") {
    vr::ecs::AppearanceStyle3D style{};
    style.base_color = vr::ecs::Rgba8{18U, 27U, 36U, 255U};
    style.shading_model = vr::ecs::AppearanceShadingModel3D::lit_pbr;
    style.metallic = 0.10F;
    style.roughness = 0.85F;
    style.normal_scale = 1.0F;

    vr::geometry::GeometryAppearanceDesc desc{};
    desc.metallic_factor = 0.35F;
    desc.roughness_factor = 0.45F;
    desc.normal_scale = 1.5F;
    desc.occlusion_strength = 0.60F;

    vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3> appearance{};
    appearance.base_rgba = {0.20F, 0.40F, 0.90F, 0.75F};
    appearance.appearance_params = {0.88F, 0.27F, 2.75F, 0.30F};
    appearance.extras = {1.0F, 0.5F, 0.5F, 0.0F};
    appearance.flags_u32[0U] =
        (static_cast<std::uint32_t>(vr::ecs::AppearanceShadingModel3D::unlit) & 0x3U) << 5U;

    vr::geometry::GeometryAppearanceResolvedState resolved =
        vr::geometry::ResolveGeometryFallbackAppearanceState(&style, &desc);
    resolved = vr::geometry::ApplyAppearanceRecordState(resolved, appearance);

    VR_CHECK(resolved.albedo_color.r == 51U);
    VR_CHECK(resolved.albedo_color.g == 102U);
    VR_CHECK(resolved.albedo_color.b == 230U);
    VR_CHECK(resolved.albedo_color.a == 96U);
    VR_CHECK(resolved.metallic == 0.88F);
    VR_CHECK(resolved.roughness == 0.27F);
    VR_CHECK(resolved.normal_scale == 2.75F);
    VR_CHECK(resolved.occlusion_strength == 0.30F);
    VR_CHECK(resolved.unlit);
}

VR_TEST_CASE(PbrAppearanceContract_resolver_lets_linked_appearance_override_geometry_and_appearance_defaults,
             "unit;pbr;appearance;contract") {
    vr::ecs::AppearanceStyle3D style{};
    style.base_color = vr::ecs::Rgba8{255U, 255U, 255U, 255U};
    style.shading_model = vr::ecs::AppearanceShadingModel3D::lit_pbr;
    style.metallic = 0.10F;
    style.roughness = 0.85F;
    style.normal_scale = 1.0F;

    vr::geometry::GeometryAppearanceDesc desc{};
    desc.metallic_factor = 0.35F;
    desc.roughness_factor = 0.45F;
    desc.normal_scale = 1.5F;
    desc.occlusion_strength = 0.60F;

    vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3> appearance{};
    appearance.base_rgba = {0.20F, 0.40F, 0.90F, 0.75F};
    appearance.appearance_params = {0.88F, 0.27F, 2.75F, 0.30F};
    appearance.extras = {1.0F, 0.5F, 0.5F, 0.0F};
    appearance.flags_u32[0U] =
        (static_cast<std::uint32_t>(vr::ecs::AppearanceShadingModel3D::unlit) & 0x3U) << 5U;

    const vr::geometry::GeometryAppearanceResolvedState resolved =
        vr::geometry::ResolveGeometryAppearanceState(&style, &desc, &appearance);

    VR_CHECK(resolved.albedo_color.r == 51U);
    VR_CHECK(resolved.albedo_color.g == 102U);
    VR_CHECK(resolved.albedo_color.b == 230U);
    VR_CHECK(resolved.albedo_color.a == 96U);
    VR_CHECK(resolved.metallic == 0.88F);
    VR_CHECK(resolved.roughness == 0.27F);
    VR_CHECK(resolved.normal_scale == 2.75F);
    VR_CHECK(resolved.occlusion_strength == 0.30F);
    VR_CHECK(resolved.unlit);
}

VR_TEST_CASE(PbrAppearanceContract_runtime_bridge_record_encodes_texture_source_and_alpha_mode_helpers,
             "unit;pbr;appearance;contract") {
    const vr::ecs::AppearanceRuntimeBridge3D bridge =
        vr::ecs::MakeAppearanceRuntimeBridge3D(nullptr);

    vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3> surface_record{};
    vr::render::BuildAppearanceGpuRecord3DFromRuntimeBridge(
        bridge,
        {
            .base_color_texture_id = 6101U,
            .sampler_state_id = 7U,
            .texture_source = vr::render::AppearanceTextureSource3D::surface_image
        },
        surface_record);
    VR_CHECK(vr::render::ResolveAppearanceTextureSource3D(surface_record) ==
             vr::render::AppearanceTextureSource3D::surface_image);

    vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3> geometry_record{};
    vr::render::BuildAppearanceGpuRecord3DFromRuntimeBridge(
        bridge,
        {
            .base_color_texture_id = 101U,
            .sampler_state_id = 3U,
            .texture_source = vr::render::AppearanceTextureSource3D::geometry_image
        },
        geometry_record);
    VR_CHECK(vr::render::ResolveAppearanceTextureSource3D(geometry_record) ==
             vr::render::AppearanceTextureSource3D::geometry_image);

    vr::render::SetAppearanceGpuRecord3DAlphaMode(
        geometry_record,
        vr::ecs::AppearanceAlphaMode::mask);
    constexpr std::uint32_t alpha_mode_shift = 3U;
    VR_CHECK(((geometry_record.flags_u32[0U] >> alpha_mode_shift) & 0x3U) ==
             static_cast<std::uint32_t>(vr::ecs::AppearanceAlphaMode::mask));
}

} // namespace

