#include "support/test_framework.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/culling_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>

namespace {

[[nodiscard]] vr::ecs::Bounds<vr::ecs::Dim2> MakeWorldBounds2D(const vr::ecs::Float2& center_,
                                                                const vr::ecs::Float2& extents_,
                                                                std::uint32_t visibility_mask_) {
    using Bounds2D = vr::ecs::Bounds<vr::ecs::Dim2>;
    using BoundsSystem2D = vr::ecs::BoundsSystem<vr::ecs::Dim2>;

    Bounds2D bounds{};
    BoundsSystem2D::Initialize(bounds);

    const float extent_x = std::max(0.0F, extents_.x);
    const float extent_y = std::max(0.0F, extents_.y);
    bounds.runtime.world_center = center_;
    bounds.runtime.world_extents = vr::ecs::Float2{.x = extent_x, .y = extent_y};
    bounds.runtime.world_min = vr::ecs::Float2{
        .x = center_.x - extent_x,
        .y = center_.y - extent_y
    };
    bounds.runtime.world_max = vr::ecs::Float2{
        .x = center_.x + extent_x,
        .y = center_.y + extent_y
    };
    bounds.runtime.world_radius = std::sqrt(extent_x * extent_x + extent_y * extent_y);
    bounds.runtime.visibility_mask = visibility_mask_;
    bounds.runtime.dirty_flags = 0U;
    return bounds;
}

[[nodiscard]] vr::ecs::Bounds<vr::ecs::Dim3> MakeWorldBounds3D(const vr::ecs::Float3& center_,
                                                                const vr::ecs::Float3& extents_,
                                                                std::uint32_t visibility_mask_) {
    using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
    using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;

    Bounds3D bounds{};
    BoundsSystem3D::Initialize(bounds);

    const float extent_x = std::max(0.0F, extents_.x);
    const float extent_y = std::max(0.0F, extents_.y);
    const float extent_z = std::max(0.0F, extents_.z);
    bounds.runtime.world_center = center_;
    bounds.runtime.world_extents = vr::ecs::Float3{.x = extent_x, .y = extent_y, .z = extent_z};
    bounds.runtime.world_min = vr::ecs::Float3{
        .x = center_.x - extent_x,
        .y = center_.y - extent_y,
        .z = center_.z - extent_z
    };
    bounds.runtime.world_max = vr::ecs::Float3{
        .x = center_.x + extent_x,
        .y = center_.y + extent_y,
        .z = center_.z + extent_z
    };
    bounds.runtime.world_radius = std::sqrt(extent_x * extent_x + extent_y * extent_y + extent_z * extent_z);
    bounds.runtime.visibility_mask = visibility_mask_;
    bounds.runtime.dirty_flags = 0U;
    return bounds;
}

struct ClipPoint final {
    float x;
    float y;
    float z;
    float w;
};

[[nodiscard]] ClipPoint TransformPointClip(const vr::ecs::Matrix4x4& matrix_,
                                           const vr::ecs::Float3& position_) {
    return ClipPoint{
        .x = matrix_.m[0] * position_.x + matrix_.m[4] * position_.y + matrix_.m[8] * position_.z + matrix_.m[12],
        .y = matrix_.m[1] * position_.x + matrix_.m[5] * position_.y + matrix_.m[9] * position_.z + matrix_.m[13],
        .z = matrix_.m[2] * position_.x + matrix_.m[6] * position_.y + matrix_.m[10] * position_.z + matrix_.m[14],
        .w = matrix_.m[3] * position_.x + matrix_.m[7] * position_.y + matrix_.m[11] * position_.z + matrix_.m[15]
    };
}

[[nodiscard]] bool IsInsideD3dClip(const ClipPoint& clip_) {
    if (clip_.w <= 1e-7F) {
        return false;
    }
    return clip_.x >= -clip_.w &&
           clip_.x <= clip_.w &&
           clip_.y >= -clip_.w &&
           clip_.y <= clip_.w &&
           clip_.z >= 0.0F &&
           clip_.z <= clip_.w;
}

[[nodiscard]] bool ContainsIndex(const vr::ecs::CullingSystemMcVector<std::uint32_t>& indices_,
                                 std::uint32_t component_index_) {
    return std::find(indices_.begin(), indices_.end(), component_index_) != indices_.end();
}

VR_TEST_CASE(EcsCullingSystem_dim2_mask_filter_and_visibility_bits, "unit;core;ecs;culling;dim2") {
    using Bounds2D = vr::ecs::Bounds<vr::ecs::Dim2>;
    using Camera2D = vr::ecs::Camera<vr::ecs::Dim2>;
    using CameraSystem2D = vr::ecs::CameraSystem<vr::ecs::Dim2>;
    using CullingSystem2D = vr::ecs::CullingSystem<vr::ecs::Dim2>;
    using Scratch2D = vr::ecs::CullingScratch<vr::ecs::Dim2>;
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

    std::array<Bounds2D, 3U> bounds{
        MakeWorldBounds2D(vr::ecs::Float2{.x = 0.0F, .y = 0.0F},
                          vr::ecs::Float2{.x = 1.0F, .y = 1.0F},
                          0x1U),
        MakeWorldBounds2D(vr::ecs::Float2{.x = 2.0F, .y = 0.0F},
                          vr::ecs::Float2{.x = 0.25F, .y = 0.5F},
                          0x2U),
        MakeWorldBounds2D(vr::ecs::Float2{.x = 0.0F, .y = 0.0F},
                          vr::ecs::Float2{.x = 0.5F, .y = 0.5F},
                          0x1U),
    };
    bounds[2U].runtime.world_min.x = 5.0F;
    bounds[2U].runtime.world_max.x = -5.0F;

    Transform2D camera_transform{};
    TransformSystem2D::Initialize(camera_transform);
    TransformSystem2D::UpdateHierarchy(&camera_transform, 1U);

    Camera2D camera{};
    CameraSystem2D::Initialize(camera);
    CameraSystem2D::SetCullingMask(camera, 0x1U);
    CameraSystem2D::MarkViewDirty(camera);
    CameraSystem2D::Update(camera, camera_transform);

    Scratch2D scratch{};
    const vr::ecs::CullingBuildOptions options{
        .enable_culling_mask_filter = true,
        .enable_frustum_culling = false,
        .enable_aabb_refine = false,
        .write_visibility_bits = true
    };

    const vr::ecs::CullingBuildStats stats = CullingSystem2D::BuildVisibleSet(bounds.data(),
                                                                               static_cast<std::uint32_t>(bounds.size()),
                                                                               camera,
                                                                               scratch,
                                                                               options);

    VR_CHECK(stats.input_count == 3U);
    VR_CHECK(stats.scanned_count == 3U);
    VR_CHECK(stats.visible_count == 1U);
    VR_CHECK(stats.culled_by_mask_count == 1U);
    VR_CHECK(stats.culled_by_invalid_bounds_count == 1U);
    VR_CHECK(stats.used_mask_filter);
    VR_CHECK(!stats.used_frustum_filter);
    VR_CHECK(stats.wrote_visibility_bits);
    VR_CHECK(stats.visible_set_signature != 0U);
    VR_REQUIRE(CullingSystem2D::VisibleIndices(scratch).size() == 1U);
    VR_CHECK(CullingSystem2D::VisibleIndices(scratch)[0U] == 0U);
    VR_CHECK(CullingSystem2D::IsVisible(scratch, 0U));
    VR_CHECK(!CullingSystem2D::IsVisible(scratch, 1U));
    VR_CHECK(!CullingSystem2D::IsVisible(scratch, 2U));
}

VR_TEST_CASE(EcsCullingSystem_dim3_frustum_matches_clip_space_point_test, "unit;core;ecs;culling;dim3") {
    using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    using CullingSystem3D = vr::ecs::CullingSystem<vr::ecs::Dim3>;
    using Scratch3D = vr::ecs::CullingScratch<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Transform3D camera_transform{};
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 5.0F});
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);

    Camera3D camera{};
    CameraSystem3D::Initialize(camera);
    CameraSystem3D::SetAspectRatio(camera, 1.0F);
    CameraSystem3D::SetVerticalFovRadians(camera, 1.0471975512F);
    CameraSystem3D::SetNearFar(camera, 0.1F, 100.0F);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    const std::array<vr::ecs::Float3, 6U> centers{
        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
        vr::ecs::Float3{.x = 2.0F, .y = 2.0F, .z = 0.0F},
        vr::ecs::Float3{.x = 200.0F, .y = 0.0F, .z = 0.0F},
        vr::ecs::Float3{.x = 0.0F, .y = 200.0F, .z = 0.0F},
        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 200.0F},
        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = -200.0F},
    };

    std::array<Bounds3D, centers.size()> bounds{};
    for (std::size_t i = 0U; i < centers.size(); ++i) {
        bounds[i] = MakeWorldBounds3D(centers[i],
                                      vr::ecs::Float3{.x = 0.01F, .y = 0.01F, .z = 0.01F},
                                      0xFFFFFFFFU);
    }

    const vr::ecs::CullingBuildOptions options{
        .enable_culling_mask_filter = false,
        .enable_frustum_culling = true,
        .enable_aabb_refine = false,
        .write_visibility_bits = true
    };

    Scratch3D scratch{};
    const vr::ecs::CullingBuildStats stats = CullingSystem3D::BuildVisibleSet(bounds.data(),
                                                                               static_cast<std::uint32_t>(bounds.size()),
                                                                               camera,
                                                                               scratch,
                                                                               options);

    std::uint32_t expected_visible_count = 0U;
    for (std::uint32_t i = 0U; i < static_cast<std::uint32_t>(centers.size()); ++i) {
        const ClipPoint clip = TransformPointClip(camera.runtime.view_projection_matrix, centers[i]);
        const bool expected_visible = IsInsideD3dClip(clip);
        const bool actual_visible = CullingSystem3D::IsVisible(scratch, i);
        if (expected_visible) {
            ++expected_visible_count;
        }
        VR_CHECK(expected_visible == actual_visible);
    }

    VR_CHECK(expected_visible_count > 0U);
    VR_CHECK(expected_visible_count < static_cast<std::uint32_t>(centers.size()));
    VR_CHECK(stats.visible_count == expected_visible_count);
    VR_CHECK(stats.culled_by_frustum_count + stats.visible_count == stats.scanned_count);
    VR_CHECK(stats.used_frustum_filter);
    VR_CHECK(!stats.used_mask_filter);
    VR_CHECK(!stats.used_aabb_refine);
}

VR_TEST_CASE(EcsCullingSystem_candidate_indices_and_prepared_camera_path, "unit;core;ecs;culling;candidates") {
    using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
    using CullingSystem3D = vr::ecs::CullingSystem<vr::ecs::Dim3>;
    using Scratch3D = vr::ecs::CullingScratch<vr::ecs::Dim3>;

    std::array<Bounds3D, 4U> bounds{
        MakeWorldBounds3D(vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                          vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.5F},
                          0x1U),
        MakeWorldBounds3D(vr::ecs::Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F},
                          vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.5F},
                          0x1U),
        MakeWorldBounds3D(vr::ecs::Float3{.x = 2.0F, .y = 0.0F, .z = 0.0F},
                          vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.5F},
                          0x1U),
        MakeWorldBounds3D(vr::ecs::Float3{.x = 3.0F, .y = 0.0F, .z = 0.0F},
                          vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.5F},
                          0x1U),
    };

    const vr::ecs::CullingBuildOptions options{
        .enable_culling_mask_filter = false,
        .enable_frustum_culling = false,
        .enable_aabb_refine = false,
        .write_visibility_bits = true
    };

    const CullingSystem3D::PreparedCamera prepared_camera = CullingSystem3D::PrepareCamera(nullptr, options);
    VR_CHECK(prepared_camera.has_camera == 0U);
    VR_CHECK(prepared_camera.use_mask_filter == 0U);
    VR_CHECK(prepared_camera.use_frustum_filter == 0U);

    Scratch3D scratch{};
    const std::uint32_t candidates[4U] = {2U, 1U, 99U, 2U};
    const vr::ecs::CullingBuildStats candidate_stats = CullingSystem3D::BuildVisibleSetFromCandidates(
        bounds.data(),
        static_cast<std::uint32_t>(bounds.size()),
        candidates,
        4U,
        prepared_camera,
        scratch,
        options);

    const auto& visible_indices = CullingSystem3D::VisibleIndices(scratch);
    VR_REQUIRE(visible_indices.size() == 3U);
    VR_CHECK(visible_indices[0U] == 2U);
    VR_CHECK(visible_indices[1U] == 1U);
    VR_CHECK(visible_indices[2U] == 2U);
    VR_CHECK(candidate_stats.input_count == 4U);
    VR_CHECK(candidate_stats.candidate_count == 4U);
    VR_CHECK(candidate_stats.scanned_count == 4U);
    VR_CHECK(candidate_stats.out_of_range_candidate_count == 1U);
    VR_CHECK(candidate_stats.visible_count == 3U);
    VR_CHECK(candidate_stats.visible_set_signature != 0U);
    VR_CHECK(CullingSystem3D::IsVisible(scratch, 1U));
    VR_CHECK(CullingSystem3D::IsVisible(scratch, 2U));
    VR_CHECK(!CullingSystem3D::IsVisible(scratch, 0U));
    VR_CHECK(!CullingSystem3D::IsVisible(scratch, 3U));

    const vr::ecs::CullingBuildStats full_scan_stats = CullingSystem3D::BuildVisibleSet(bounds.data(),
                                                                                         static_cast<std::uint32_t>(bounds.size()),
                                                                                         prepared_camera,
                                                                                         scratch,
                                                                                         options);
    VR_CHECK(full_scan_stats.visible_count == static_cast<std::uint32_t>(bounds.size()));
    VR_CHECK(full_scan_stats.visible_set_signature != candidate_stats.visible_set_signature);
    for (std::uint32_t i = 0U; i < static_cast<std::uint32_t>(bounds.size()); ++i) {
        VR_CHECK(ContainsIndex(CullingSystem3D::VisibleIndices(scratch), i));
        VR_CHECK(CullingSystem3D::IsVisible(scratch, i));
    }
}

} // namespace

