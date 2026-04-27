#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/surface_runtime_system.hpp"
#include "vr/ecs/system/surface_upload_plan_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include "support/test_framework.hpp"

#include <array>
#include <cstdint>

namespace {

using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;
using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
using SurfaceRuntimeSystem2D = vr::ecs::SurfaceRuntimeSystem<vr::ecs::Dim2>;
using SurfaceRuntimeSystem3D = vr::ecs::SurfaceRuntimeSystem<vr::ecs::Dim3>;
using SurfaceUploadPlanSystem2D = vr::ecs::SurfaceUploadPlanSystem<vr::ecs::Dim2>;
using SurfaceUploadPlanSystem3D = vr::ecs::SurfaceUploadPlanSystem<vr::ecs::Dim3>;
using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

VR_TEST_CASE(EcsSurfaceUploadPlan_dim3_sparse_dirty_builds_merged_ranges, "unit;core;ecs;surface;surface-upload-plan") {
    std::array<Surface3D, 8U> surfaces{};
    std::array<Transform3D, 8U> transforms{};

    for (std::uint32_t i = 0U; i < surfaces.size(); ++i) {
        SurfaceSystem3D::Initialize(surfaces[i]);
        SurfaceSystem3D::SetTextureRoute(surfaces[i], 1000U + i, 10U, 0U, 0U);
        SurfaceSystem3D::SetRuntimeRoute(surfaces[i], 1000U + i, 2U, 0U);
        TransformSystem3D::Initialize(transforms[i]);
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Surface3DRuntimeScratch runtime_scratch{};
    const auto runtime_stats = SurfaceRuntimeSystem3D::Build(surfaces.data(),
                                                              transforms.data(),
                                                              static_cast<std::uint32_t>(surfaces.size()),
                                                              runtime_scratch);
    VR_REQUIRE(runtime_stats.emitted_instance_count == surfaces.size());

    const std::array<std::uint32_t, 5U> dirty_components{6U, 2U, 3U, 6U, 1U};

    vr::ecs::SurfaceUploadPlanScratch<vr::ecs::Dim3> plan_scratch{};
    const auto plan_stats = SurfaceUploadPlanSystem3D::BuildRangesFromDirtyComponents(
        runtime_scratch,
        dirty_components.data(),
        static_cast<std::uint32_t>(dirty_components.size()),
        plan_scratch);

    VR_CHECK(plan_stats.used_dense_path == false);
    VR_CHECK(plan_stats.requested_component_count == dirty_components.size());
    VR_CHECK(plan_stats.resolved_instance_count == 4U);
    VR_CHECK(plan_stats.dropped_component_count == 0U);
    VR_CHECK(plan_stats.range_count == 2U);
    VR_CHECK(plan_stats.merged_adjacent_count == 2U);
    VR_REQUIRE(SurfaceUploadPlanSystem3D::RangeCount(plan_scratch) == 2U);

    const auto* ranges = SurfaceUploadPlanSystem3D::Ranges(plan_scratch);
    VR_CHECK(ranges[0U].instance_begin == 1U);
    VR_CHECK(ranges[0U].instance_count == 3U);
    VR_CHECK(ranges[1U].instance_begin == 6U);
    VR_CHECK(ranges[1U].instance_count == 1U);
}

VR_TEST_CASE(EcsSurfaceUploadPlan_dim3_dense_dirty_prefers_dense_path, "unit;core;ecs;surface;surface-upload-plan") {
    std::array<Surface3D, 256U> surfaces{};
    std::array<Transform3D, 256U> transforms{};

    for (std::uint32_t i = 0U; i < surfaces.size(); ++i) {
        SurfaceSystem3D::Initialize(surfaces[i]);
        SurfaceSystem3D::SetTextureRoute(surfaces[i], 2000U + i, 9U, 0U, 0U);
        SurfaceSystem3D::SetRuntimeRoute(surfaces[i], 2000U + i, 3U, 0U);
        TransformSystem3D::Initialize(transforms[i]);
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Surface3DRuntimeScratch runtime_scratch{};
    const auto runtime_stats = SurfaceRuntimeSystem3D::Build(surfaces.data(),
                                                              transforms.data(),
                                                              static_cast<std::uint32_t>(surfaces.size()),
                                                              runtime_scratch);
    VR_REQUIRE(runtime_stats.emitted_instance_count == surfaces.size());

    std::array<std::uint32_t, 192U> dirty_components{};
    for (std::uint32_t i = 0U; i < dirty_components.size(); ++i) {
        dirty_components[i] = i;
    }

    vr::ecs::SurfaceUploadPlanScratch<vr::ecs::Dim3> plan_scratch{};
    const auto plan_stats = SurfaceUploadPlanSystem3D::BuildRangesFromDirtyComponents(
        runtime_scratch,
        dirty_components.data(),
        static_cast<std::uint32_t>(dirty_components.size()),
        plan_scratch);

    VR_CHECK(plan_stats.used_dense_path == true);
    VR_CHECK(plan_stats.resolved_instance_count == dirty_components.size());
    VR_CHECK(plan_stats.dropped_component_count == 0U);
    VR_CHECK(plan_stats.range_count == 1U);
    VR_REQUIRE(SurfaceUploadPlanSystem3D::RangeCount(plan_scratch) == 1U);

    const auto* ranges = SurfaceUploadPlanSystem3D::Ranges(plan_scratch);
    VR_CHECK(ranges[0U].instance_begin == 0U);
    VR_CHECK(ranges[0U].instance_count == dirty_components.size());
}

VR_TEST_CASE(EcsSurfaceUploadPlan_dim2_skips_invalid_or_unmapped_components, "unit;core;ecs;surface;surface-upload-plan") {
    std::array<Surface2D, 5U> surfaces{};
    std::array<Transform2D, 5U> transforms{};

    for (std::uint32_t i = 0U; i < surfaces.size(); ++i) {
        SurfaceSystem2D::Initialize(surfaces[i]);
        SurfaceSystem2D::SetRuntimeRoute(surfaces[i], 3000U + i, 1U, 0U);
        SurfaceSystem2D::SetSize(surfaces[i], vr::ecs::Float2{.x = 16.0F, .y = 16.0F});
        TransformSystem2D::Initialize(transforms[i]);
    }

    SurfaceSystem2D::SetImageId(surfaces[0U], 4001U);
    SurfaceSystem2D::SetImageId(surfaces[1U], 0U);
    SurfaceSystem2D::SetImageId(surfaces[2U], 4003U);
    SurfaceSystem2D::SetVisible(surfaces[2U], false);
    SurfaceSystem2D::SetSpriteId(surfaces[3U], 5001U);
    SurfaceSystem2D::SetImageId(surfaces[4U], 4005U);
    SurfaceSystem2D::SetVisible(surfaces[4U], false);

    TransformSystem2D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Surface2DRuntimeScratch runtime_scratch{};
    const auto runtime_stats = SurfaceRuntimeSystem2D::Build(surfaces.data(),
                                                              transforms.data(),
                                                              static_cast<std::uint32_t>(surfaces.size()),
                                                              runtime_scratch);
    VR_REQUIRE(runtime_stats.emitted_instance_count == 2U);

    const std::array<std::uint32_t, 6U> dirty_components{0U, 1U, 2U, 3U, 4U, 99U};
    vr::ecs::SurfaceUploadPlanScratch<vr::ecs::Dim2> plan_scratch{};
    const auto plan_stats = SurfaceUploadPlanSystem2D::BuildRangesFromDirtyComponents(
        runtime_scratch,
        dirty_components.data(),
        static_cast<std::uint32_t>(dirty_components.size()),
        plan_scratch);

    VR_CHECK(plan_stats.resolved_instance_count == 2U);
    VR_CHECK(plan_stats.dropped_component_count == 4U);
    VR_CHECK(plan_stats.range_count >= 1U);
    VR_REQUIRE(SurfaceUploadPlanSystem2D::RangeCount(plan_scratch) >= 1U);

    const auto* ranges = SurfaceUploadPlanSystem2D::Ranges(plan_scratch);
    for (std::uint32_t i = 0U; i < SurfaceUploadPlanSystem2D::RangeCount(plan_scratch); ++i) {
        VR_CHECK(ranges[i].instance_count > 0U);
        VR_CHECK(ranges[i].instance_begin + ranges[i].instance_count <= runtime_stats.emitted_instance_count);
    }
}

VR_TEST_CASE(EcsSurfaceUploadPlan_dim3_merge_gap_options_reduce_copy_ranges, "unit;core;ecs;surface;surface-upload-plan") {
    std::array<Surface3D, 16U> surfaces{};
    std::array<Transform3D, 16U> transforms{};

    for (std::uint32_t i = 0U; i < surfaces.size(); ++i) {
        SurfaceSystem3D::Initialize(surfaces[i]);
        SurfaceSystem3D::SetTextureRoute(surfaces[i], 5000U + i, 31U, 0U, 0U);
        SurfaceSystem3D::SetRuntimeRoute(surfaces[i], 5000U + i, 4U, 0U);
        TransformSystem3D::Initialize(transforms[i]);
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Surface3DRuntimeScratch runtime_scratch{};
    const auto runtime_stats = SurfaceRuntimeSystem3D::Build(surfaces.data(),
                                                              transforms.data(),
                                                              static_cast<std::uint32_t>(surfaces.size()),
                                                              runtime_scratch);
    VR_REQUIRE(runtime_stats.emitted_instance_count == surfaces.size());

    const std::array<std::uint32_t, 4U> dirty_components{1U, 3U, 7U, 9U};

    vr::ecs::SurfaceUploadPlanScratch<vr::ecs::Dim3> plan_scratch{};
    const auto no_gap_stats = SurfaceUploadPlanSystem3D::BuildRangesFromDirtyComponents(
        runtime_scratch,
        dirty_components.data(),
        static_cast<std::uint32_t>(dirty_components.size()),
        plan_scratch);
    VR_REQUIRE(no_gap_stats.range_count == 4U);
    VR_CHECK(no_gap_stats.covered_instance_count == 4U);
    VR_CHECK(no_gap_stats.merged_gap_instance_count == 0U);

    const vr::ecs::SurfaceUploadPlanBuildOptions gap_options{
        .merge_gap_instances = 1U,
        .dense_path_min_dirty_count = 64U,
        .dense_path_min_coverage_percent = 25U
    };
    const auto with_gap_stats = SurfaceUploadPlanSystem3D::BuildRangesFromDirtyComponents(
        runtime_scratch,
        dirty_components.data(),
        static_cast<std::uint32_t>(dirty_components.size()),
        gap_options,
        plan_scratch);

    VR_REQUIRE(with_gap_stats.range_count == 2U);
    VR_CHECK(with_gap_stats.covered_instance_count == 6U);
    VR_CHECK(with_gap_stats.resolved_instance_count == 4U);
    VR_CHECK(with_gap_stats.merged_gap_instance_count == 2U);

    const auto* ranges = SurfaceUploadPlanSystem3D::Ranges(plan_scratch);
    VR_CHECK(ranges[0U].instance_begin == 1U);
    VR_CHECK(ranges[0U].instance_count == 3U);
    VR_CHECK(ranges[1U].instance_begin == 7U);
    VR_CHECK(ranges[1U].instance_count == 3U);
}

} // namespace
