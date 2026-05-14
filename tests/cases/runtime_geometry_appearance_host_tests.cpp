#include "support/test_framework.hpp"
#include "vr/geometry/geometry_appearance_host.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

VR_TEST_CASE(RuntimeGeometryAppearanceHost_upsert_lookup_remove, "unit;runtime;geometry;appearance") {
    vr::geometry::GeometryAppearanceHost host{};
    vr::geometry::GeometryAppearanceHostCreateInfo create_info{};
    create_info.reserve_appearance_count = 8U;
    host.Initialize(create_info);

    vr::geometry::GeometryAppearanceDesc appearance_a{};
    appearance_a.appearance_id = 30U;
    appearance_a.image_id = 300U;
    appearance_a.flags = 0x1U;
    appearance_a.uv_scale_u = 1.0F;
    appearance_a.uv_scale_v = 1.0F;
    appearance_a.metallic_factor = 0.15F;
    appearance_a.roughness_factor = 0.92F;
    appearance_a.normal_scale = 1.25F;
    appearance_a.occlusion_strength = 0.80F;

    vr::geometry::GeometryAppearanceDesc appearance_b{};
    appearance_b.appearance_id = 10U;
    appearance_b.image_id = 100U;
    appearance_b.flags = 0x2U;
    appearance_b.uv_scale_u = 2.0F;
    appearance_b.uv_scale_v = 2.0F;
    appearance_b.uv_bias_u = 0.25F;
    appearance_b.alpha_cutoff = 0.35F;
    appearance_b.metallic_factor = 0.65F;
    appearance_b.roughness_factor = 0.28F;
    appearance_b.normal_scale = 2.5F;
    appearance_b.occlusion_strength = 0.55F;

    vr::geometry::GeometryAppearanceDesc appearance_c{};
    appearance_c.appearance_id = 20U;
    appearance_c.image_id = 200U;
    appearance_c.flags = 0x4U;
    appearance_c.uv_scale_u = 0.5F;
    appearance_c.uv_scale_v = 0.5F;
    appearance_c.uv_bias_v = -0.25F;
    appearance_c.metallic_factor = 0.05F;
    appearance_c.roughness_factor = 0.72F;
    appearance_c.normal_scale = 0.75F;
    appearance_c.occlusion_strength = 0.90F;

    host.UpsertAppearance(appearance_a);
    host.UpsertAppearance(appearance_b);
    host.UpsertAppearance(appearance_c);

    const auto* record_b = host.FindAppearance(10U);
    const auto* record_c = host.FindAppearance(20U);
    const auto* record_a = host.FindAppearance(30U);
    VR_REQUIRE(record_b != nullptr);
    VR_REQUIRE(record_c != nullptr);
    VR_REQUIRE(record_a != nullptr);

    VR_CHECK(record_b->desc.image_id == 100U);
    VR_CHECK(record_b->desc.alpha_cutoff == 0.35F);
    VR_CHECK(record_b->desc.metallic_factor == 0.65F);
    VR_CHECK(record_b->desc.roughness_factor == 0.28F);
    VR_CHECK(record_b->desc.normal_scale == 2.5F);
    VR_CHECK(record_b->desc.occlusion_strength == 0.55F);
    VR_CHECK(record_c->desc.image_id == 200U);
    VR_CHECK(record_a->desc.image_id == 300U);
    VR_CHECK(record_b->revision == 1U);

    appearance_b.image_id = 101U;
    appearance_b.uv_bias_v = 0.5F;
    appearance_b.alpha_cutoff = 0.6F;
    appearance_b.metallic_factor = 0.42F;
    appearance_b.roughness_factor = 0.84F;
    appearance_b.normal_scale = 3.0F;
    appearance_b.occlusion_strength = 0.20F;
    host.UpsertAppearance(appearance_b);

    record_b = host.FindAppearance(10U);
    VR_REQUIRE(record_b != nullptr);
    VR_CHECK(record_b->desc.image_id == 101U);
    VR_CHECK(record_b->desc.alpha_cutoff == 0.6F);
    VR_CHECK(record_b->desc.metallic_factor == 0.42F);
    VR_CHECK(record_b->desc.roughness_factor == 0.84F);
    VR_CHECK(record_b->desc.normal_scale == 3.0F);
    VR_CHECK(record_b->desc.occlusion_strength == 0.20F);
    VR_CHECK(record_b->revision == 2U);

    VR_CHECK(!host.RemoveAppearance(999U));
    VR_CHECK(host.RemoveAppearance(20U));
    VR_CHECK(host.FindAppearance(20U) == nullptr);

    const vr::geometry::GeometryAppearanceHostStats stats = host.Stats();
    VR_CHECK(stats.appearance_count == 2U);
    VR_CHECK(stats.added_appearance_count == 3U);
    VR_CHECK(stats.updated_appearance_count == 1U);
    VR_CHECK(stats.removed_appearance_count == 1U);

    host.Shutdown();
    VR_CHECK(!host.IsInitialized());
}

VR_TEST_CASE(RuntimeGeometryAppearanceHost_invalid_appearance_id_throws, "unit;runtime;geometry;appearance") {
    vr::geometry::GeometryAppearanceHost host{};
    host.Initialize({});

    vr::geometry::GeometryAppearanceDesc invalid_desc{};
    invalid_desc.appearance_id = 0U;
    bool threw = false;
    try {
        host.UpsertAppearance(invalid_desc);
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {
        threw = true;
    }

    VR_CHECK(threw);
    host.Shutdown();
}

VR_TEST_CASE(RuntimeGeometryAppearanceHost_clamps_minimal_pbr_factors, "unit;runtime;geometry;appearance;pbr") {
    vr::geometry::GeometryAppearanceHost host{};
    host.Initialize({});

    vr::geometry::GeometryAppearanceDesc desc{};
    desc.appearance_id = 77U;
    desc.metallic_factor = 4.0F;
    desc.roughness_factor = 0.0F;
    desc.normal_scale = 99.0F;
    desc.occlusion_strength = -5.0F;
    desc.alpha_cutoff = 2.0F;
    desc.uv_scale_u = std::numeric_limits<float>::quiet_NaN();
    desc.uv_bias_v = std::numeric_limits<float>::infinity();

    host.UpsertAppearance(desc);
    const auto* record = host.FindAppearance(77U);
    VR_REQUIRE(record != nullptr);
    VR_CHECK(record->desc.metallic_factor == 1.0F);
    VR_CHECK(record->desc.roughness_factor == 0.04F);
    VR_CHECK(record->desc.normal_scale == 4.0F);
    VR_CHECK(record->desc.occlusion_strength == 0.0F);
    VR_CHECK(record->desc.alpha_cutoff == 1.0F);
    VR_CHECK(record->desc.uv_scale_u == 1.0F);
    VR_CHECK(record->desc.uv_bias_v == 0.0F);

    host.Shutdown();
}

} // namespace

