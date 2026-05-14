#include "support/test_framework.hpp"
#include "vr/geometry/geometry_material_host.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

VR_TEST_CASE(RuntimeGeometryMaterialHost_upsert_lookup_remove, "unit;runtime;geometry;material") {
    vr::geometry::GeometryMaterialHost host{};
    vr::geometry::GeometryMaterialHostCreateInfo create_info{};
    create_info.reserve_material_count = 8U;
    host.Initialize(create_info);

    vr::geometry::GeometryMaterialDesc material_a{};
    material_a.material_id = 30U;
    material_a.image_id = 300U;
    material_a.flags = 0x1U;
    material_a.uv_scale_u = 1.0F;
    material_a.uv_scale_v = 1.0F;
    material_a.metallic_factor = 0.15F;
    material_a.roughness_factor = 0.92F;
    material_a.normal_scale = 1.25F;
    material_a.occlusion_strength = 0.80F;

    vr::geometry::GeometryMaterialDesc material_b{};
    material_b.material_id = 10U;
    material_b.image_id = 100U;
    material_b.flags = 0x2U;
    material_b.uv_scale_u = 2.0F;
    material_b.uv_scale_v = 2.0F;
    material_b.uv_bias_u = 0.25F;
    material_b.alpha_cutoff = 0.35F;
    material_b.metallic_factor = 0.65F;
    material_b.roughness_factor = 0.28F;
    material_b.normal_scale = 2.5F;
    material_b.occlusion_strength = 0.55F;

    vr::geometry::GeometryMaterialDesc material_c{};
    material_c.material_id = 20U;
    material_c.image_id = 200U;
    material_c.flags = 0x4U;
    material_c.uv_scale_u = 0.5F;
    material_c.uv_scale_v = 0.5F;
    material_c.uv_bias_v = -0.25F;
    material_c.metallic_factor = 0.05F;
    material_c.roughness_factor = 0.72F;
    material_c.normal_scale = 0.75F;
    material_c.occlusion_strength = 0.90F;

    host.UpsertMaterial(material_a);
    host.UpsertMaterial(material_b);
    host.UpsertMaterial(material_c);

    const auto* record_b = host.FindMaterial(10U);
    const auto* record_c = host.FindMaterial(20U);
    const auto* record_a = host.FindMaterial(30U);
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

    material_b.image_id = 101U;
    material_b.uv_bias_v = 0.5F;
    material_b.alpha_cutoff = 0.6F;
    material_b.metallic_factor = 0.42F;
    material_b.roughness_factor = 0.84F;
    material_b.normal_scale = 3.0F;
    material_b.occlusion_strength = 0.20F;
    host.UpsertMaterial(material_b);

    record_b = host.FindMaterial(10U);
    VR_REQUIRE(record_b != nullptr);
    VR_CHECK(record_b->desc.image_id == 101U);
    VR_CHECK(record_b->desc.alpha_cutoff == 0.6F);
    VR_CHECK(record_b->desc.metallic_factor == 0.42F);
    VR_CHECK(record_b->desc.roughness_factor == 0.84F);
    VR_CHECK(record_b->desc.normal_scale == 3.0F);
    VR_CHECK(record_b->desc.occlusion_strength == 0.20F);
    VR_CHECK(record_b->revision == 2U);

    VR_CHECK(!host.RemoveMaterial(999U));
    VR_CHECK(host.RemoveMaterial(20U));
    VR_CHECK(host.FindMaterial(20U) == nullptr);

    const vr::geometry::GeometryMaterialHostStats stats = host.Stats();
    VR_CHECK(stats.material_count == 2U);
    VR_CHECK(stats.added_material_count == 3U);
    VR_CHECK(stats.updated_material_count == 1U);
    VR_CHECK(stats.removed_material_count == 1U);

    host.Shutdown();
    VR_CHECK(!host.IsInitialized());
}

VR_TEST_CASE(RuntimeGeometryMaterialHost_invalid_material_id_throws, "unit;runtime;geometry;material") {
    vr::geometry::GeometryMaterialHost host{};
    host.Initialize({});

    vr::geometry::GeometryMaterialDesc invalid_desc{};
    invalid_desc.material_id = 0U;
    bool threw = false;
    try {
        host.UpsertMaterial(invalid_desc);
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {
        threw = true;
    }

    VR_CHECK(threw);
    host.Shutdown();
}

VR_TEST_CASE(RuntimeGeometryMaterialHost_clamps_minimal_pbr_factors, "unit;runtime;geometry;material;pbr") {
    vr::geometry::GeometryMaterialHost host{};
    host.Initialize({});

    vr::geometry::GeometryMaterialDesc desc{};
    desc.material_id = 77U;
    desc.metallic_factor = 4.0F;
    desc.roughness_factor = 0.0F;
    desc.normal_scale = 99.0F;
    desc.occlusion_strength = -5.0F;
    desc.alpha_cutoff = 2.0F;
    desc.uv_scale_u = std::numeric_limits<float>::quiet_NaN();
    desc.uv_bias_v = std::numeric_limits<float>::infinity();

    host.UpsertMaterial(desc);
    const auto* record = host.FindMaterial(77U);
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
