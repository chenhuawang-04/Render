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

VR_TEST_CASE(PbrAppearanceContract_authored_appearance_defaults_to_style_when_no_overrides_are_present,
             "unit;pbr;appearance;contract") {
    vr::ecs::AppearanceStyle3D style{};
    style.base_color = vr::ecs::Rgba8{24U, 48U, 96U, 200U};
    style.shading_model = vr::ecs::AppearanceShadingModel3D::lit_pbr;
    style.metallic = 0.33F;
    style.roughness = 0.61F;
    style.normal_scale = 1.25F;
    style.occlusion_strength = 1.0F;

    const vr::geometry::GeometryAppearanceResolvedState resolved =
        vr::geometry::ResolveGeometryAuthoredAppearanceState(&style);

    VR_CHECK(resolved.base_color.r == 24U);
    VR_CHECK(resolved.base_color.g == 48U);
    VR_CHECK(resolved.base_color.b == 96U);
    VR_CHECK(resolved.base_color.a == 200U);
    VR_CHECK(resolved.metallic == 0.33F);
    VR_CHECK(resolved.roughness == 0.61F);
    VR_CHECK(resolved.normal_scale == 1.25F);
    VR_CHECK(resolved.occlusion_strength == 1.0F);
    VR_CHECK(!resolved.unlit);
}

VR_TEST_CASE(PbrAppearanceContract_authored_appearance_applies_geometry_over_style_defaults,
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
        vr::geometry::ResolveGeometryAuthoredAppearanceState(&style, &desc);

    VR_CHECK(resolved.metallic == 0.72F);
    VR_CHECK(resolved.roughness == 0.18F);
    VR_CHECK(resolved.normal_scale == 2.0F);
    VR_CHECK(resolved.occlusion_strength == 0.45F);
    VR_CHECK(resolved.unlit);
}

VR_TEST_CASE(PbrAppearanceContract_linked_appearance_overlay_preserves_layered_resolution_order,
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
        vr::geometry::ResolveGeometryAuthoredAppearanceState(&style, &desc);
    resolved = vr::geometry::OverlayLinkedAppearanceRecordState(resolved, appearance);

    VR_CHECK(resolved.base_color.r == 51U);
    VR_CHECK(resolved.base_color.g == 102U);
    VR_CHECK(resolved.base_color.b == 230U);
    VR_CHECK(resolved.base_color.a == 96U);
    VR_CHECK(resolved.metallic == 0.88F);
    VR_CHECK(resolved.roughness == 0.27F);
    VR_CHECK(resolved.normal_scale == 2.75F);
    VR_CHECK(resolved.occlusion_strength == 0.30F);
    VR_CHECK(resolved.unlit);
}

VR_TEST_CASE(PbrAppearanceContract_final_resolved_appearance_lets_linked_overlay_override_authored_defaults,
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
        vr::geometry::ResolveFinalGeometryAppearanceState(&style, &desc, &appearance);

    VR_CHECK(resolved.base_color.r == 51U);
    VR_CHECK(resolved.base_color.g == 102U);
    VR_CHECK(resolved.base_color.b == 230U);
    VR_CHECK(resolved.base_color.a == 96U);
    VR_CHECK(resolved.metallic == 0.88F);
    VR_CHECK(resolved.roughness == 0.27F);
    VR_CHECK(resolved.normal_scale == 2.75F);
    VR_CHECK(resolved.occlusion_strength == 0.30F);
    VR_CHECK(resolved.unlit);
}

VR_TEST_CASE(PbrAppearanceContract_runtime_bridge_record_encodes_surface_domains_and_alpha_mode_helpers,
             "unit;pbr;appearance;contract") {
    const vr::ecs::AppearanceRuntimeBridge3D bridge =
        vr::ecs::MakeAppearanceRuntimeBridge3D(nullptr);

    vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3> surface_record{};
    vr::render::BuildAppearanceGpuRecord3DFromRuntimeBridge(
        bridge,
        {
            .base_color_surface = {
                .surface_id = 6101U,
                .domain = vr::render::AppearanceSampledSurfaceDomain::surface_image
            },
            .surface_sampler_id = 7U,
        },
        surface_record);
    VR_CHECK(vr::render::ResolveAppearanceSampledSurfaceDomain3D(
                 surface_record,
                 vr::render::AppearanceSampledSurfaceSlot3D::base_color) ==
             vr::render::AppearanceSampledSurfaceDomain::surface_image);

    vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3> geometry_record{};
    vr::render::BuildAppearanceGpuRecord3DFromRuntimeBridge(
        bridge,
        {
            .base_color_surface = {
                .surface_id = 101U,
                .domain = vr::render::AppearanceSampledSurfaceDomain::geometry_image
            },
            .surface_sampler_id = 3U,
        },
        geometry_record);
    VR_CHECK(vr::render::ResolveAppearanceSampledSurfaceDomain3D(
                 geometry_record,
                 vr::render::AppearanceSampledSurfaceSlot3D::base_color) ==
             vr::render::AppearanceSampledSurfaceDomain::geometry_image);

    vr::render::SetAppearanceGpuRecord3DAlphaMode(
        geometry_record,
        vr::ecs::AppearanceAlphaMode::mask);
    constexpr std::uint32_t alpha_mode_shift = 3U;
    VR_CHECK(((geometry_record.flags_u32[0U] >> alpha_mode_shift) & 0x3U) ==
             static_cast<std::uint32_t>(vr::ecs::AppearanceAlphaMode::mask));
}

VR_TEST_CASE(PbrAppearanceContract_typed_sampled_surface_helpers_preserve_domain_and_storage_ids,
             "unit;pbr;appearance;contract") {
    const vr::render::AppearanceSampledSurfaceHandle texture_handle =
        vr::render::MakeAppearanceTextureHandle(vr::asset::TextureId{41U});
    const vr::render::AppearanceSampledSurfaceHandle surface_handle =
        vr::render::MakeAppearanceSurfaceImageHandle(vr::surface::SurfaceImageId{52U});
    const vr::render::AppearanceSampledSurfaceHandle geometry_handle =
        vr::render::MakeAppearanceGeometryImageHandle(vr::geometry::GeometryImageId{63U});

    VR_CHECK(texture_handle.surface_id == 41U);
    VR_CHECK(texture_handle.domain == vr::render::AppearanceSampledSurfaceDomain::asset_texture);
    VR_CHECK(surface_handle.surface_id == 52U);
    VR_CHECK(surface_handle.domain == vr::render::AppearanceSampledSurfaceDomain::surface_image);
    VR_CHECK(geometry_handle.surface_id == 63U);
    VR_CHECK(geometry_handle.domain == vr::render::AppearanceSampledSurfaceDomain::geometry_image);

    VR_CHECK(vr::render::MakeAppearanceSampledSurfaceHandle(vr::asset::TextureId{71U}).domain ==
             vr::render::AppearanceSampledSurfaceDomain::asset_texture);
    VR_CHECK(vr::render::MakeAppearanceSampledSurfaceHandle(vr::surface::SurfaceImageId{72U}).domain ==
             vr::render::AppearanceSampledSurfaceDomain::surface_image);
    VR_CHECK(vr::render::MakeAppearanceSampledSurfaceHandle(vr::geometry::GeometryImageId{73U}).domain ==
             vr::render::AppearanceSampledSurfaceDomain::geometry_image);
}

VR_TEST_CASE(PbrAppearanceContract_unavailable_sampled_surface_resolver_keeps_safe_unbound_fallback,
             "unit;pbr;appearance;contract") {
    const vr::render::AppearanceSampledSurfaceResolver3D resolver{};

    const auto resolved_texture = vr::render::ResolveAppearanceSampledSurfaceBinding3D(
        resolver,
        vr::render::MakeAppearanceTextureHandle(vr::asset::TextureId{91U}));
    const auto resolved_surface = vr::render::ResolveAppearanceSampledSurfaceBinding3D(
        resolver,
        vr::render::MakeAppearanceSurfaceImageHandle(vr::surface::SurfaceImageId{92U}));
    const auto resolved_geometry = vr::render::ResolveAppearanceSampledSurfaceBinding3D(
        resolver,
        vr::render::MakeAppearanceGeometryImageHandle(vr::geometry::GeometryImageId{93U}));

    VR_CHECK(!resolved_texture.present);
    VR_CHECK(!resolved_surface.present);
    VR_CHECK(!resolved_geometry.present);
    VR_CHECK(resolved_texture.slot == 0U);
    VR_CHECK(resolved_surface.slot == 0U);
    VR_CHECK(resolved_geometry.slot == 0U);
}

VR_TEST_CASE(PbrAppearanceContract_appearance_binding3d_preserves_sampled_surface_semantics,
             "unit;pbr;appearance;contract") {
    vr::ecs::AppearanceBinding3D binding{};

    VR_CHECK(vr::ecs::SetAppearanceBinding3DSampledSurface(
        binding,
        vr::render::AppearanceSampledSurfaceSlot3D::base_color,
        {
            .surface_id = 42U,
            .domain = vr::render::AppearanceSampledSurfaceDomain::surface_image,
        }));
    VR_CHECK(vr::ecs::SetAppearanceBinding3DSampledSurface(
        binding,
        vr::render::AppearanceSampledSurfaceSlot3D::normal,
        {
            .surface_id = 77U,
            .domain = vr::render::AppearanceSampledSurfaceDomain::geometry_image,
        }));

    const auto base_color =
        vr::ecs::ResolveAppearanceBinding3DSampledSurface(
            binding,
            vr::render::AppearanceSampledSurfaceSlot3D::base_color);
    const auto normal =
        vr::ecs::ResolveAppearanceBinding3DSampledSurface(
            binding,
            vr::render::AppearanceSampledSurfaceSlot3D::normal);

    VR_CHECK(base_color.surface_id == 42U);
    VR_CHECK(base_color.domain == vr::render::AppearanceSampledSurfaceDomain::surface_image);
    VR_CHECK(normal.surface_id == 77U);
    VR_CHECK(normal.domain == vr::render::AppearanceSampledSurfaceDomain::geometry_image);
}

} // namespace

