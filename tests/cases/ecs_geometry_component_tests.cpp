#include "support/test_framework.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/component/geometry_component.hpp"
#include "vr/ecs/system/geometry_mesh_system.hpp"
#include "vr/ecs/system/geometry_path_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;

void ApplyGeometry2DAppearanceLayer(vr::ecs::Geometry<vr::ecs::Dim2>& geometry_,
                                    std::int16_t layer_) {
    Appearance2D appearance{};
    AppearanceSystem2D::Initialize(appearance);
    AppearanceSystem2D::SetLayer(appearance, layer_);
    (void)vr::ecs::GeometrySystem<vr::ecs::Dim2>::ApplyAppearanceRuntimeState(geometry_, appearance.style);
}

VR_TEST_CASE(EcsGeometryComponent_is_pure_pod, "unit;core;ecs;geometry") {
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Geometry<vr::ecs::Dim2>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Geometry<vr::ecs::Dim2>>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Geometry<vr::ecs::Dim3>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Geometry<vr::ecs::Dim3>>);
}

VR_TEST_CASE(EcsGeometrySystem_dim2_sort_key_and_path_route, "unit;core;ecs;geometry") {
    using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
    using GeometrySystem2D = vr::ecs::GeometrySystem<vr::ecs::Dim2>;

    Geometry2D geometry{};
    GeometrySystem2D::Initialize(geometry);
    GeometrySystem2D::ClearDirtyFlags(geometry, 0xFFFFFFFFU);

    GeometrySystem2D::SetRuntimeRoute(geometry, 105U, 77U, 31U);
    ApplyGeometry2DAppearanceLayer(geometry, -12);
    GeometrySystem2D::SetRenderPassHint(geometry, vr::ecs::GeometryRenderPassHint::transparent);

    const std::uint64_t sort_key = GeometrySystem2D::SortKey(geometry);
    VR_CHECK(GeometrySystem2D::ExtractPassBucket(sort_key) ==
             static_cast<std::uint32_t>(vr::ecs::GeometryRenderPassHint::transparent));
    VR_CHECK(GeometrySystem2D::ExtractVisualResourceBucket(sort_key) == 77U);
    VR_CHECK(GeometrySystem2D::ExtractGeometryBucket(sort_key) == 105U);
    VR_CHECK(GeometrySystem2D::ExtractBatchBucket(sort_key) == 31U);

    const std::uint32_t expected_minor = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(-12) -
        static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()));
    VR_CHECK(GeometrySystem2D::ExtractMinorBucket(sort_key) == expected_minor);
    VR_CHECK(GeometrySystem2D::HasDirtyFlags(geometry, vr::ecs::geometry_dirty_runtime_flag));
}

VR_TEST_CASE(EcsGeometrySystem_dim2_runtime_bridge_apply_updates_sort_key,
             "unit;core;ecs;geometry") {
    using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
    using GeometrySystem2D = vr::ecs::GeometrySystem<vr::ecs::Dim2>;

    Geometry2D geometry{};
    GeometrySystem2D::Initialize(geometry);
    GeometrySystem2D::ClearDirtyFlags(geometry, 0xFFFFFFFFU);

    auto bridge = vr::ecs::ReadAppearanceRuntimeBridge2D(geometry.runtime);
    bridge.fill_color = vr::ecs::Rgba8{10U, 20U, 30U, 255U};
    bridge.layer = -9;

    VR_REQUIRE(GeometrySystem2D::ApplyAppearanceRuntimeBridgeState(geometry, bridge));
    const auto stored_bridge = vr::ecs::ReadAppearanceRuntimeBridge2D(geometry.runtime);
    VR_CHECK(stored_bridge.fill_color.r == 10U);
    VR_CHECK(stored_bridge.fill_color.g == 20U);
    VR_CHECK(stored_bridge.fill_color.b == 30U);
    VR_CHECK(stored_bridge.layer == -9);

    const std::uint32_t expected_minor = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(-9) -
        static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()));
    VR_CHECK(GeometrySystem2D::ExtractMinorBucket(GeometrySystem2D::SortKey(geometry)) == expected_minor);
    VR_CHECK(GeometrySystem2D::HasDirtyFlags(geometry, vr::ecs::geometry_dirty_runtime_flag));
    VR_CHECK(!GeometrySystem2D::ApplyAppearanceRuntimeBridgeState(geometry, bridge));
}

VR_TEST_CASE(EcsGeometryPathSystem_append_and_bounds, "unit;core;ecs;geometry;path") {
    using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
    using PathSystem = vr::ecs::GeometryPathSystem;

    Geometry2D geometry{};
    PathSystem::Initialize(geometry);

    VR_REQUIRE(PathSystem::AppendMoveTo(geometry, -1.0F, 2.0F));
    VR_REQUIRE(PathSystem::AppendLineTo(geometry, 5.0F, -3.0F));
    VR_REQUIRE(PathSystem::AppendQuadTo(geometry, 7.0F, 4.0F, 9.0F, 1.0F));
    VR_REQUIRE(PathSystem::AppendClose(geometry));

    VR_CHECK(PathSystem::DataSizeBytes(geometry) > 0U);
    VR_CHECK(PathSystem::CommandCount(geometry) == 4U);
    VR_CHECK(geometry.runtime.path_data_hash != 0U);
    VR_CHECK(geometry.runtime.bounds_min.x == -1.0F);
    VR_CHECK(geometry.runtime.bounds_min.y == -3.0F);
    VR_CHECK(geometry.runtime.bounds_max.x == 9.0F);
    VR_CHECK(geometry.runtime.bounds_max.y == 4.0F);

    std::array<std::uint32_t, 5U> type_counts{};
    PathSystem::ForEachCommandRaw(geometry,
                                  [&](const vr::ecs::GeometryPathCommandView& view_) {
                                      const auto index = static_cast<std::size_t>(view_.type);
                                      if (index < type_counts.size()) {
                                          ++type_counts[index];
                                      }
                                  });

    VR_CHECK(type_counts[static_cast<std::size_t>(vr::ecs::GeometryPathCommandType::move_to)] == 1U);
    VR_CHECK(type_counts[static_cast<std::size_t>(vr::ecs::GeometryPathCommandType::line_to)] == 1U);
    VR_CHECK(type_counts[static_cast<std::size_t>(vr::ecs::GeometryPathCommandType::quad_to)] == 1U);
    VR_CHECK(type_counts[static_cast<std::size_t>(vr::ecs::GeometryPathCommandType::close)] == 1U);
}

VR_TEST_CASE(EcsGeometryMeshSystem_route_style_and_bounds, "unit;core;ecs;geometry;mesh") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;

    Geometry3D geometry{};
    MeshSystem::Initialize(geometry);
    Appearance3D appearance{};
    AppearanceSystem3D::Initialize(appearance);

    GeometrySystem3D::SetAuthoringVisualResourceId(geometry, 12U);
    GeometrySystem3D::SetDepthBin(geometry, 33U);
    MeshSystem::SetMeshRoute(geometry, 301U, 2U, 1U);
    MeshSystem::SetTopology(geometry, vr::ecs::Geometry3DTopology::lines);
    AppearanceSystem3D::SetBaseColor(appearance, vr::ecs::Rgba8{180U, 200U, 255U, 192U});
    AppearanceSystem3D::SetOpacity(appearance, 0.5F);
    AppearanceSystem3D::SetMetallic(appearance, 0.4F);
    AppearanceSystem3D::SetRoughness(appearance, 0.7F);
    AppearanceSystem3D::SetDoubleSided(appearance, true);
    AppearanceSystem3D::SetDepthWrite(appearance, false);
    (void)GeometrySystem3D::ApplyAppearanceRuntimeState(geometry, appearance.style);
    MeshSystem::SetBounds(geometry,
                          vr::ecs::Float3{.x = -1.0F, .y = -2.0F, .z = -3.0F},
                          vr::ecs::Float3{.x = 4.0F, .y = 5.0F, .z = 6.0F});

    const std::uint64_t sort_key = GeometrySystem3D::SortKey(geometry);
    VR_CHECK(GeometrySystem3D::ExtractGeometryBucket(sort_key) == 301U);
    VR_CHECK(GeometrySystem3D::ExtractVisualResourceBucket(sort_key) == 12U);
    VR_CHECK(GeometrySystem3D::ExtractMinorBucket(sort_key) == 33U);

    VR_CHECK(geometry.mesh.submesh_index == 2U);
    VR_CHECK(geometry.mesh.lod_index == 1U);
    VR_CHECK(geometry.style.topology == vr::ecs::Geometry3DTopology::lines);
    const auto appearance_bridge = vr::ecs::ReadAppearanceRuntimeBridge3D(geometry.runtime);
    VR_CHECK(appearance_bridge.base_color.r == 180U);
    VR_CHECK(appearance_bridge.base_color.a == 192U);
    VR_CHECK(appearance_bridge.opacity == 0.5F);
    VR_CHECK(appearance_bridge.metallic == 0.4F);
    VR_CHECK(appearance_bridge.roughness == 0.7F);
    VR_CHECK(vr::ecs::IsAppearanceRuntimeBridge3DDoubleSided(appearance_bridge));
    VR_CHECK(!vr::ecs::IsAppearanceRuntimeBridge3DDepthWriteEnabled(appearance_bridge));
    VR_CHECK(geometry.runtime.bounds_min.z == -3.0F);
    VR_CHECK(geometry.runtime.bounds_max.z == 6.0F);
    VR_CHECK(geometry.runtime.mesh_revision > 0U);
    VR_CHECK(GeometrySystem3D::IsVisibleForBatch(geometry));
}

VR_TEST_CASE(EcsGeometrySystem_dim3_unlinked_appearance_bridge_promotes_transparent_sort,
             "unit;core;ecs;geometry") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;

    Geometry3D geometry{};
    GeometrySystem3D::Initialize(geometry);
    GeometrySystem3D::SetGeometryId(geometry, 6U);
    GeometrySystem3D::SetAuthoringVisualResourceId(geometry, 10U);
    GeometrySystem3D::SetDepthBin(geometry, 21U);

    Appearance3D appearance{};
    AppearanceSystem3D::Initialize(appearance);
    AppearanceSystem3D::SetBlendMode(appearance, vr::ecs::AppearanceBlendMode::alpha);
    AppearanceSystem3D::SetAlphaMode(appearance, vr::ecs::AppearanceAlphaMode::blend);
    AppearanceSystem3D::SetOpacity(appearance, 0.6F);
    (void)GeometrySystem3D::ApplyAppearanceRuntimeState(geometry, appearance.style);

    const std::uint64_t sort_key = GeometrySystem3D::SortKey(geometry);
    VR_CHECK(GeometrySystem3D::ExtractPassBucket(sort_key) ==
             static_cast<std::uint32_t>(vr::ecs::GeometryRenderPassHint::transparent));
    VR_CHECK(GeometrySystem3D::ExtractMinorBucket(sort_key) ==
             static_cast<std::uint32_t>((std::numeric_limits<std::uint16_t>::max)() - 21U));
}

VR_TEST_CASE(EcsGeometrySystem_dim3_transparent_sort_reverses_depth_minor_bucket,
             "unit;core;ecs;geometry") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;

    Geometry3D geometry{};
    GeometrySystem3D::Initialize(geometry);
    GeometrySystem3D::SetGeometryId(geometry, 5U);
    GeometrySystem3D::SetAuthoringVisualResourceId(geometry, 9U);
    GeometrySystem3D::SetDepthBin(geometry, 33U);
    GeometrySystem3D::SetRenderPassHint(geometry, vr::ecs::GeometryRenderPassHint::transparent);

    const std::uint64_t sort_key = GeometrySystem3D::SortKey(geometry);
    VR_CHECK(GeometrySystem3D::ExtractPassBucket(sort_key) ==
             static_cast<std::uint32_t>(vr::ecs::GeometryRenderPassHint::transparent));
    VR_CHECK(GeometrySystem3D::ExtractMinorBucket(sort_key) ==
             static_cast<std::uint32_t>((std::numeric_limits<std::uint16_t>::max)() - 33U));
}

VR_TEST_CASE(EcsGeometrySystem_appearance_handle_mutation_serial_monotonic,
             "unit;core;ecs;geometry") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;

    Geometry3D geometry{};
    GeometrySystem3D::Initialize(geometry);

    const std::uint64_t serial_before = GeometrySystem3D::AppearanceHandleMutationSerial();
    const vr::ecs::AppearanceHandle handle_a{.index = 3U, .generation = 1U};
    GeometrySystem3D::SetAppearanceHandle(geometry, handle_a);
    const std::uint64_t serial_after_set = GeometrySystem3D::AppearanceHandleMutationSerial();
    VR_CHECK(serial_after_set > serial_before);

    GeometrySystem3D::SetAppearanceHandle(geometry, handle_a);
    const std::uint64_t serial_after_same_set = GeometrySystem3D::AppearanceHandleMutationSerial();
    VR_CHECK(serial_after_same_set == serial_after_set);

    const bool runtime_changed = GeometrySystem3D::SetAppearanceRuntimeLink(geometry,
                                                                             handle_a,
                                                                             1234ULL,
                                                                             11ULL,
                                                                             22ULL);
    VR_CHECK(runtime_changed);
    const std::uint64_t serial_after_runtime_link_same_handle =
        GeometrySystem3D::AppearanceHandleMutationSerial();
    VR_CHECK(serial_after_runtime_link_same_handle == serial_after_same_set);

    const vr::ecs::AppearanceHandle handle_b{.index = 5U, .generation = 1U};
    const bool runtime_handle_switched = GeometrySystem3D::SetAppearanceRuntimeLink(geometry,
                                                                                     handle_b,
                                                                                     2345ULL,
                                                                                     12ULL,
                                                                                     33ULL);
    VR_CHECK(runtime_handle_switched);
    const std::uint64_t serial_after_runtime_link_changed_handle =
        GeometrySystem3D::AppearanceHandleMutationSerial();
    VR_CHECK(serial_after_runtime_link_changed_handle > serial_after_runtime_link_same_handle);
}

VR_TEST_CASE(EcsGeometrySystem_appearance_link_preserves_base_visual_resource_and_unlink_restores_effective_route,
             "unit;core;ecs;geometry") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;

    Geometry3D geometry{};
    GeometrySystem3D::Initialize(geometry);
    GeometrySystem3D::SetGeometryId(geometry, 41U);
    GeometrySystem3D::SetAuthoringVisualResourceId(geometry, 77U);
    GeometrySystem3D::SetDepthBin(geometry, 3U);

    const vr::ecs::AppearanceHandle handle{.index = 8U, .generation = 2U};
    const bool linked = GeometrySystem3D::SetAppearanceRuntimeLink(geometry,
                                                                   handle,
                                                                   0ULL,
                                                                   0x02000000ULL,
                                                                   1234ULL);
    VR_CHECK(linked);
    VR_CHECK(geometry.runtime.route.authoring_visual_resource_id == 77U);
    VR_CHECK(geometry.runtime.route.linked_visual_resource_id == 1234U);
    VR_CHECK(vr::ecs::ResolveEffectiveVisualResourceId(geometry.runtime.route) == 1234U);
    VR_CHECK(GeometrySystem3D::ExtractVisualResourceBucket(geometry.runtime.route.sort_key) == (1234U & 0xFFFFU));

    GeometrySystem3D::SetAuthoringVisualResourceId(geometry, 91U);
    VR_CHECK(geometry.runtime.route.authoring_visual_resource_id == 91U);
    VR_CHECK(vr::ecs::ResolveEffectiveVisualResourceId(geometry.runtime.route) == 1234U);
    VR_CHECK(GeometrySystem3D::ExtractVisualResourceBucket(geometry.runtime.route.sort_key) == (1234U & 0xFFFFU));

    GeometrySystem3D::ClearAppearanceHandle(geometry);
    VR_CHECK(geometry.runtime.route.authoring_visual_resource_id == 91U);
    VR_CHECK(geometry.runtime.route.linked_visual_resource_id == 0U);
    VR_CHECK(vr::ecs::ResolveEffectiveVisualResourceId(geometry.runtime.route) == 91U);
    VR_CHECK(GeometrySystem3D::ExtractVisualResourceBucket(geometry.runtime.route.sort_key) == 91U);
}

} // namespace
