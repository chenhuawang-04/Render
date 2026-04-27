#include "support/test_framework.hpp"
#include "vr/geometry/geometry_material_host.hpp"

#include <cstdint>
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

    vr::geometry::GeometryMaterialDesc material_b{};
    material_b.material_id = 10U;
    material_b.image_id = 100U;
    material_b.flags = 0x2U;
    material_b.uv_scale_u = 2.0F;
    material_b.uv_scale_v = 2.0F;
    material_b.uv_bias_u = 0.25F;

    vr::geometry::GeometryMaterialDesc material_c{};
    material_c.material_id = 20U;
    material_c.image_id = 200U;
    material_c.flags = 0x4U;
    material_c.uv_scale_u = 0.5F;
    material_c.uv_scale_v = 0.5F;
    material_c.uv_bias_v = -0.25F;

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
    VR_CHECK(record_c->desc.image_id == 200U);
    VR_CHECK(record_a->desc.image_id == 300U);
    VR_CHECK(record_b->revision == 1U);

    material_b.image_id = 101U;
    material_b.uv_bias_v = 0.5F;
    host.UpsertMaterial(material_b);

    record_b = host.FindMaterial(10U);
    VR_REQUIRE(record_b != nullptr);
    VR_CHECK(record_b->desc.image_id == 101U);
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

} // namespace
