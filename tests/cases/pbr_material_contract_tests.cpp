#include "support/test_framework.hpp"
#include "vr/geometry/geometry_material_host.hpp"
#include "vr/geometry/geometry_material_resolver.hpp"

#include <limits>

namespace {

VR_TEST_CASE(PbrMaterialContract_geometry_material_desc_defaults_match_standard_pbr_baseline,
             "unit;pbr;material;contract") {
    const vr::geometry::GeometryMaterialDesc desc{};

    VR_CHECK(desc.metallic_factor == 0.0F);
    VR_CHECK(desc.roughness_factor == 1.0F);
    VR_CHECK(desc.normal_scale == 1.0F);
    VR_CHECK(desc.occlusion_strength == 1.0F);
}

VR_TEST_CASE(PbrMaterialContract_geometry_material_host_canonicalizes_minimal_pbr_factors,
             "unit;pbr;material;contract") {
    vr::geometry::GeometryMaterialHost host{};
    host.Initialize({});

    vr::geometry::GeometryMaterialDesc desc{};
    desc.material_id = 900U;
    desc.metallic_factor = -1.0F;
    desc.roughness_factor = std::numeric_limits<float>::quiet_NaN();
    desc.normal_scale = -7.0F;
    desc.occlusion_strength = 8.0F;

    host.UpsertMaterial(desc);
    const auto* record = host.FindMaterial(desc.material_id);
    VR_REQUIRE(record != nullptr);

    VR_CHECK(record->desc.metallic_factor == 0.0F);
    VR_CHECK(record->desc.roughness_factor == 1.0F);
    VR_CHECK(record->desc.normal_scale == 0.0F);
    VR_CHECK(record->desc.occlusion_strength == 1.0F);

    desc.metallic_factor = 0.82F;
    desc.roughness_factor = 0.02F;
    desc.normal_scale = 3.5F;
    desc.occlusion_strength = 0.35F;
    host.UpsertMaterial(desc);
    record = host.FindMaterial(desc.material_id);
    VR_REQUIRE(record != nullptr);

    VR_CHECK(record->desc.metallic_factor == 0.82F);
    VR_CHECK(record->desc.roughness_factor == 0.04F);
    VR_CHECK(record->desc.normal_scale == 3.5F);
    VR_CHECK(record->desc.occlusion_strength == 0.35F);

    host.Shutdown();
}

VR_TEST_CASE(PbrMaterialContract_resolver_defaults_to_geometry_style_when_no_overrides_are_present,
             "unit;pbr;material;contract") {
    vr::ecs::GeometryStyle3D style{};
    style.albedo_color = vr::ecs::Rgba8{24U, 48U, 96U, 200U};
    style.shading_model = vr::ecs::Geometry3DShadingModel::lit;
    style.metallic = 0.33F;
    style.roughness = 0.61F;
    style.normal_scale = 1.25F;

    const vr::geometry::GeometryMaterialResolvedState resolved =
        vr::geometry::ResolveGeometryFallbackMaterialState(style);

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

VR_TEST_CASE(PbrMaterialContract_resolver_applies_geometry_material_over_style_defaults,
             "unit;pbr;material;contract") {
    vr::ecs::GeometryStyle3D style{};
    style.albedo_color = vr::ecs::Rgba8{255U, 255U, 255U, 255U};
    style.shading_model = vr::ecs::Geometry3DShadingModel::unlit;
    style.metallic = 0.05F;
    style.roughness = 0.95F;
    style.normal_scale = 0.8F;

    vr::geometry::GeometryMaterialDesc desc{};
    desc.metallic_factor = 0.72F;
    desc.roughness_factor = 0.18F;
    desc.normal_scale = 2.0F;
    desc.occlusion_strength = 0.45F;

    const vr::geometry::GeometryMaterialResolvedState resolved =
        vr::geometry::ResolveGeometryFallbackMaterialState(style, &desc);

    VR_CHECK(resolved.metallic == 0.72F);
    VR_CHECK(resolved.roughness == 0.18F);
    VR_CHECK(resolved.normal_scale == 2.0F);
    VR_CHECK(resolved.occlusion_strength == 0.45F);
    VR_CHECK(resolved.unlit);
}

VR_TEST_CASE(PbrMaterialContract_appearance_overlay_preserves_layered_resolution_order,
             "unit;pbr;material;contract") {
    vr::ecs::GeometryStyle3D style{};
    style.albedo_color = vr::ecs::Rgba8{18U, 27U, 36U, 255U};
    style.shading_model = vr::ecs::Geometry3DShadingModel::lit;
    style.metallic = 0.10F;
    style.roughness = 0.85F;
    style.normal_scale = 1.0F;

    vr::geometry::GeometryMaterialDesc desc{};
    desc.metallic_factor = 0.35F;
    desc.roughness_factor = 0.45F;
    desc.normal_scale = 1.5F;
    desc.occlusion_strength = 0.60F;

    vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3> appearance{};
    appearance.base_rgba = {0.20F, 0.40F, 0.90F, 0.75F};
    appearance.material_params = {0.88F, 0.27F, 2.75F, 0.30F};
    appearance.extras = {1.0F, 0.5F, 0.5F, 0.0F};
    appearance.flags_u32[0U] =
        (static_cast<std::uint32_t>(vr::ecs::AppearanceShadingModel3D::unlit) & 0x3U) << 5U;

    vr::geometry::GeometryMaterialResolvedState resolved =
        vr::geometry::ResolveGeometryFallbackMaterialState(style, &desc);
    resolved = vr::geometry::ApplyAppearanceMaterialState(resolved, appearance);

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

VR_TEST_CASE(PbrMaterialContract_resolver_lets_linked_appearance_override_geometry_and_material_defaults,
             "unit;pbr;material;contract") {
    vr::ecs::GeometryStyle3D style{};
    style.albedo_color = vr::ecs::Rgba8{255U, 255U, 255U, 255U};
    style.shading_model = vr::ecs::Geometry3DShadingModel::lit;
    style.metallic = 0.10F;
    style.roughness = 0.85F;
    style.normal_scale = 1.0F;

    vr::geometry::GeometryMaterialDesc desc{};
    desc.metallic_factor = 0.35F;
    desc.roughness_factor = 0.45F;
    desc.normal_scale = 1.5F;
    desc.occlusion_strength = 0.60F;

    vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3> appearance{};
    appearance.base_rgba = {0.20F, 0.40F, 0.90F, 0.75F};
    appearance.material_params = {0.88F, 0.27F, 2.75F, 0.30F};
    appearance.extras = {1.0F, 0.5F, 0.5F, 0.0F};
    appearance.flags_u32[0U] =
        (static_cast<std::uint32_t>(vr::ecs::AppearanceShadingModel3D::unlit) & 0x3U) << 5U;

    const vr::geometry::GeometryMaterialResolvedState resolved =
        vr::geometry::ResolveGeometryMaterialState(style, &desc, &appearance);

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

} // namespace
