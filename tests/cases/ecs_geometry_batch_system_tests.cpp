#include "support/test_framework.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/geometry_batch_system.hpp"
#include "vr/ecs/system/geometry_mesh_system.hpp"
#include "vr/ecs/system/geometry_path_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"

#include <array>
#include <cstdint>

namespace {

void ApplyGeometry2DAppearanceLayer(vr::ecs::Geometry<vr::ecs::Dim2>& geometry_,
                                    std::int16_t layer_) {
    vr::ecs::Appearance<vr::ecs::Dim2> appearance{};
    vr::ecs::AppearanceSystem<vr::ecs::Dim2>::Initialize(appearance);
    vr::ecs::AppearanceSystem<vr::ecs::Dim2>::SetLayer(appearance, layer_);
    (void)vr::ecs::GeometrySystem<vr::ecs::Dim2>::ApplyAppearanceRuntimeState(geometry_, appearance.style);
}

VR_TEST_CASE(EcsGeometryBatchSystem_dim2_build_sort_and_group, "unit;core;ecs;geometry;batch") {
    using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
    using GeometrySystem2D = vr::ecs::GeometrySystem<vr::ecs::Dim2>;
    using PathSystem = vr::ecs::GeometryPathSystem;
    using BatchSystem = vr::ecs::GeometryBatchSystem<vr::ecs::Dim2>;

    std::array<Geometry2D, 6U> components{};
    for (auto& component : components) {
        PathSystem::Initialize(component);
    }

    // 0: visible
    VR_REQUIRE(PathSystem::AppendMoveTo(components[0U], 0.0F, 0.0F));
    VR_REQUIRE(PathSystem::AppendLineTo(components[0U], 1.0F, 0.0F));
    GeometrySystem2D::SetRuntimeRoute(components[0U], 7U, 5U, 3U);
    ApplyGeometry2DAppearanceLayer(components[0U], 2);

    // 1: visible (sorts before 0 by visual resource)
    VR_REQUIRE(PathSystem::AppendMoveTo(components[1U], 0.0F, 0.0F));
    VR_REQUIRE(PathSystem::AppendLineTo(components[1U], 0.0F, 1.0F));
    GeometrySystem2D::SetRuntimeRoute(components[1U], 7U, 4U, 1U);
    ApplyGeometry2DAppearanceLayer(components[1U], 2);

    // 2: empty

    // 3: hidden
    VR_REQUIRE(PathSystem::AppendMoveTo(components[3U], 0.0F, 0.0F));
    VR_REQUIRE(PathSystem::AppendLineTo(components[3U], 1.0F, 1.0F));
    GeometrySystem2D::SetVisible(components[3U], false);

    // 4/5: same binding key (different batch only)
    VR_REQUIRE(PathSystem::AppendMoveTo(components[4U], 0.0F, 0.0F));
    VR_REQUIRE(PathSystem::AppendLineTo(components[4U], 2.0F, 0.0F));
    GeometrySystem2D::SetRuntimeRoute(components[4U], 3U, 8U, 1U);
    ApplyGeometry2DAppearanceLayer(components[4U], -1);

    VR_REQUIRE(PathSystem::AppendMoveTo(components[5U], 0.0F, 0.0F));
    VR_REQUIRE(PathSystem::AppendLineTo(components[5U], 3.0F, 0.0F));
    GeometrySystem2D::SetRuntimeRoute(components[5U], 3U, 8U, 7U);
    ApplyGeometry2DAppearanceLayer(components[5U], -1);

    vr::ecs::GeometryBatchScratch<vr::ecs::Dim2> scratch{};
    const auto stats = BatchSystem::BuildAndSort(components.data(),
                                                 static_cast<std::uint32_t>(components.size()),
                                                 scratch,
                                                 true);

    VR_CHECK(stats.total_count == static_cast<std::uint32_t>(components.size()));
    VR_CHECK(stats.visible_count == 4U);
    VR_CHECK(stats.hidden_count == 1U);
    VR_CHECK(stats.empty_count == 1U);

    VR_CHECK(BatchSystem::OrderedIndexCount(scratch) == 4U);
    const std::uint32_t* indices = BatchSystem::OrderedIndices(scratch);
    VR_REQUIRE(indices != nullptr);

    // Expected key order: visual resource 4 -> visual resource 5 -> visual resource 8(batch1) -> visual resource 8(batch7)
    VR_CHECK(indices[0U] == 1U);
    VR_CHECK(indices[1U] == 0U);
    VR_CHECK(indices[2U] == 4U);
    VR_CHECK(indices[3U] == 5U);

    std::uint32_t binding_group_count = 0U;
    std::uint32_t grouped_item_count = 0U;
    BatchSystem::ForEachBindingGroup(scratch,
                                     [&](std::uint32_t begin_,
                                         std::uint32_t count_,
                                         std::uint64_t binding_key_) {
                                         (void)begin_;
                                         (void)binding_key_;
                                         ++binding_group_count;
                                         grouped_item_count += count_;
                                     });
    VR_CHECK(binding_group_count == 3U);
    VR_CHECK(grouped_item_count == stats.visible_count);
}

VR_TEST_CASE(EcsGeometryBatchSystem_dim3_depth_bin_affects_binding, "unit;core;ecs;geometry;batch") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using BatchSystem = vr::ecs::GeometryBatchSystem<vr::ecs::Dim3>;

    std::array<Geometry3D, 3U> components{};
    for (auto& component : components) {
        MeshSystem::Initialize(component);
        MeshSystem::SetMeshRoute(component, 12U, 0U, 0U);
        GeometrySystem3D::SetVisualResourceId(component, 8U);
    }

    GeometrySystem3D::SetDepthBin(components[0U], 0U);
    GeometrySystem3D::SetBatchTag(components[0U], 1U);

    GeometrySystem3D::SetDepthBin(components[1U], 0U);
    GeometrySystem3D::SetBatchTag(components[1U], 9U);

    GeometrySystem3D::SetDepthBin(components[2U], 2U);
    GeometrySystem3D::SetBatchTag(components[2U], 3U);

    vr::ecs::GeometryBatchScratch<vr::ecs::Dim3> scratch{};
    const auto stats = BatchSystem::BuildAndSort(components.data(),
                                                 static_cast<std::uint32_t>(components.size()),
                                                 scratch,
                                                 true);
    VR_CHECK(stats.visible_count == 3U);

    const std::uint32_t* indices = BatchSystem::OrderedIndices(scratch);
    VR_REQUIRE(indices != nullptr);
    // depth_bin 0 first (batch 1 then 9), then depth_bin 2
    VR_CHECK(indices[0U] == 0U);
    VR_CHECK(indices[1U] == 1U);
    VR_CHECK(indices[2U] == 2U);

    std::uint32_t binding_group_count = 0U;
    std::uint32_t grouped_item_count = 0U;
    BatchSystem::ForEachBindingGroup(scratch,
                                     [&](std::uint32_t begin_,
                                         std::uint32_t count_,
                                         std::uint64_t binding_key_) {
                                         (void)begin_;
                                         (void)binding_key_;
                                         ++binding_group_count;
                                         grouped_item_count += count_;
                                     });
    VR_CHECK(binding_group_count == 2U);
    VR_CHECK(grouped_item_count == stats.visible_count);
}

VR_TEST_CASE(EcsGeometryBatchSystem_dim3_candidate_scan_preserves_candidate_order_before_sort,
             "unit;core;ecs;geometry;batch") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using BatchSystem = vr::ecs::GeometryBatchSystem<vr::ecs::Dim3>;

    std::array<Geometry3D, 5U> components{};
    for (std::uint32_t i = 0U; i < components.size(); ++i) {
        MeshSystem::Initialize(components[i]);
        MeshSystem::SetMeshRoute(components[i], 12U, 0U, 0U);
        GeometrySystem3D::SetVisualResourceId(components[i], 10U + i);
    }
    GeometrySystem3D::SetVisible(components[2U], false);

    const std::uint32_t candidates[6U] = {4U, 2U, 1U, 99U, 1U, 0U};

    vr::ecs::GeometryBatchScratch<vr::ecs::Dim3> scratch{};
    const auto stats = BatchSystem::BuildAndSortFromCandidates(components.data(),
                                                                static_cast<std::uint32_t>(components.size()),
                                                                candidates,
                                                                6U,
                                                                scratch,
                                                                true);
    VR_CHECK(stats.total_count == static_cast<std::uint32_t>(components.size()));
    VR_CHECK(stats.scanned_count == 6U);
    VR_CHECK(stats.used_candidate_indices == 1U);
    VR_CHECK(stats.hidden_count == 1U);
    VR_CHECK(stats.empty_count == 0U);
    VR_CHECK(stats.out_of_range_candidate_count == 1U);
    VR_CHECK(stats.visible_count == 4U);

    const std::uint32_t* indices = BatchSystem::OrderedIndices(scratch);
    VR_REQUIRE(indices != nullptr);
    VR_CHECK(BatchSystem::OrderedIndexCount(scratch) == 4U);
    // sorted by sort_key (visual resource id ascending): 0,1,1,4
    VR_CHECK(indices[0U] == 0U);
    VR_CHECK(indices[1U] == 1U);
    VR_CHECK(indices[2U] == 1U);
    VR_CHECK(indices[3U] == 4U);
}

} // namespace

