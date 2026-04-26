#include "support/test_framework.hpp"
#include "vr/ecs/component/geometry_component.hpp"
#include "vr/ecs/system/geometry_mesh_system.hpp"
#include "vr/ecs/system/geometry_path_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

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
    GeometrySystem2D::SetLayer(geometry, -12);
    GeometrySystem2D::SetRenderPassHint(geometry, vr::ecs::GeometryRenderPassHint::transparent);

    const std::uint64_t sort_key = GeometrySystem2D::SortKey(geometry);
    VR_CHECK(GeometrySystem2D::ExtractPassBucket(sort_key) ==
             static_cast<std::uint32_t>(vr::ecs::GeometryRenderPassHint::transparent));
    VR_CHECK(GeometrySystem2D::ExtractMaterialBucket(sort_key) == 77U);
    VR_CHECK(GeometrySystem2D::ExtractGeometryBucket(sort_key) == 105U);
    VR_CHECK(GeometrySystem2D::ExtractBatchBucket(sort_key) == 31U);

    const std::uint32_t expected_minor = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(-12) -
        static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()));
    VR_CHECK(GeometrySystem2D::ExtractMinorBucket(sort_key) == expected_minor);
    VR_CHECK(GeometrySystem2D::HasDirtyFlags(geometry, vr::ecs::geometry_dirty_runtime_flag));
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

    GeometrySystem3D::SetMaterialId(geometry, 12U);
    GeometrySystem3D::SetDepthBin(geometry, 33U);
    MeshSystem::SetMeshRoute(geometry, 301U, 2U, 1U);
    MeshSystem::SetTopology(geometry, vr::ecs::Geometry3DTopology::lines);
    MeshSystem::SetShadingModel(geometry, vr::ecs::Geometry3DShadingModel::unlit);
    MeshSystem::SetDoubleSided(geometry, true);
    MeshSystem::SetDepthWrite(geometry, false);
    MeshSystem::SetAlbedoColor(geometry, vr::ecs::Rgba8{180U, 200U, 255U, 255U});
    MeshSystem::SetBounds(geometry,
                          vr::ecs::Float3{.x = -1.0F, .y = -2.0F, .z = -3.0F},
                          vr::ecs::Float3{.x = 4.0F, .y = 5.0F, .z = 6.0F});

    const std::uint64_t sort_key = GeometrySystem3D::SortKey(geometry);
    VR_CHECK(GeometrySystem3D::ExtractGeometryBucket(sort_key) == 301U);
    VR_CHECK(GeometrySystem3D::ExtractMaterialBucket(sort_key) == 12U);
    VR_CHECK(GeometrySystem3D::ExtractMinorBucket(sort_key) == 33U);

    VR_CHECK(geometry.mesh.submesh_index == 2U);
    VR_CHECK(geometry.mesh.lod_index == 1U);
    VR_CHECK(geometry.style.topology == vr::ecs::Geometry3DTopology::lines);
    VR_CHECK(geometry.style.shading_model == vr::ecs::Geometry3DShadingModel::unlit);
    VR_CHECK(geometry.style.double_sided == 1U);
    VR_CHECK(geometry.style.depth_write == 0U);
    VR_CHECK(geometry.runtime.bounds_min.z == -3.0F);
    VR_CHECK(geometry.runtime.bounds_max.z == 6.0F);
    VR_CHECK(geometry.runtime.mesh_revision > 0U);
    VR_CHECK(GeometrySystem3D::IsVisibleForBatch(geometry));
}

} // namespace

