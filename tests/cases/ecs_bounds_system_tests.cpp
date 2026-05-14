#include "support/test_framework.hpp"
#include "vr/ecs/component/bounds_component.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace {

VR_TEST_CASE(EcsBoundsComponent_is_pure_pod, "unit;core;ecs;bounds") {
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Bounds<vr::ecs::Dim2>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Bounds<vr::ecs::Dim2>>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Bounds<vr::ecs::Dim3>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Bounds<vr::ecs::Dim3>>);
}

VR_TEST_CASE(EcsBoundsSystem_dim2_world_update_and_revision_cache, "unit;core;ecs;bounds;dim2") {
    using Bounds2D = vr::ecs::Bounds<vr::ecs::Dim2>;
    using BoundsSystem2D = vr::ecs::BoundsSystem<vr::ecs::Dim2>;
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

    Bounds2D bounds{};
    BoundsSystem2D::Initialize(bounds);
    VR_CHECK(!BoundsSystem2D::HasDirtyFlags(bounds, vr::ecs::bounds_dirty_runtime_flag));

    Transform2D transform{};
    TransformSystem2D::Initialize(transform);
    TransformSystem2D::SetLocalPosition(transform, 10.0F, 20.0F);
    TransformSystem2D::SetLocalScale(transform, 2.0F, 3.0F);
    TransformSystem2D::SetLocalRotationRadians(transform, 0.0F);
    TransformSystem2D::UpdateHierarchy(&transform, 1U);

    BoundsSystem2D::SetLocalAabb(bounds,
                                 vr::ecs::Float2{.x = -1.0F, .y = -2.0F},
                                 vr::ecs::Float2{.x = 1.0F, .y = 2.0F});
    const std::uint32_t world_revision_before = BoundsSystem2D::WorldRevision(bounds);
    VR_REQUIRE(BoundsSystem2D::Update(bounds, transform));

    VR_CHECK(bounds.runtime.world_center.x == 10.0F);
    VR_CHECK(bounds.runtime.world_center.y == 20.0F);
    VR_CHECK(bounds.runtime.world_extents.x == 2.0F);
    VR_CHECK(bounds.runtime.world_extents.y == 6.0F);
    VR_CHECK(bounds.runtime.world_min.x == 8.0F);
    VR_CHECK(bounds.runtime.world_min.y == 14.0F);
    VR_CHECK(bounds.runtime.world_max.x == 12.0F);
    VR_CHECK(bounds.runtime.world_max.y == 26.0F);
    VR_CHECK(std::abs(bounds.runtime.world_radius - 6.3245554F) < 1e-5F);
    VR_CHECK(BoundsSystem2D::WorldRevision(bounds) != world_revision_before);
    VR_CHECK(!BoundsSystem2D::HasDirtyFlags(bounds, vr::ecs::bounds_dirty_runtime_flag));

    const std::uint32_t world_revision_after = BoundsSystem2D::WorldRevision(bounds);
    VR_CHECK(!BoundsSystem2D::Update(bounds, transform));
    VR_CHECK(BoundsSystem2D::WorldRevision(bounds) == world_revision_after);
}

VR_TEST_CASE(EcsBoundsSystem_dim3_world_aabb_from_trs, "unit;core;ecs;bounds;dim3") {
    using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
    using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Bounds3D bounds{};
    BoundsSystem3D::Initialize(bounds);
    BoundsSystem3D::SetLocalAabb(bounds,
                                 vr::ecs::Float3{.x = -1.0F, .y = -1.0F, .z = -1.0F},
                                 vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform, vr::ecs::Float3{.x = 5.0F, .y = -2.0F, .z = 3.0F});
    TransformSystem3D::SetLocalScale(transform, vr::ecs::Float3{.x = 2.0F, .y = 3.0F, .z = 4.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(transform, 0.0F, 0.0F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    VR_REQUIRE(BoundsSystem3D::Update(bounds, transform));
    VR_CHECK(bounds.runtime.world_center.x == 5.0F);
    VR_CHECK(bounds.runtime.world_center.y == -2.0F);
    VR_CHECK(bounds.runtime.world_center.z == 3.0F);
    VR_CHECK(bounds.runtime.world_extents.x == 2.0F);
    VR_CHECK(bounds.runtime.world_extents.y == 3.0F);
    VR_CHECK(bounds.runtime.world_extents.z == 4.0F);
    VR_CHECK(bounds.runtime.world_min.x == 3.0F);
    VR_CHECK(bounds.runtime.world_min.y == -5.0F);
    VR_CHECK(bounds.runtime.world_min.z == -1.0F);
    VR_CHECK(bounds.runtime.world_max.x == 7.0F);
    VR_CHECK(bounds.runtime.world_max.y == 1.0F);
    VR_CHECK(bounds.runtime.world_max.z == 7.0F);
    VR_CHECK(std::abs(bounds.runtime.world_radius - 5.3851648F) < 1e-5F);
    VR_CHECK(BoundsSystem3D::ContainsPointWorld(bounds, vr::ecs::Float3{.x = 6.0F, .y = -1.0F, .z = 2.0F}));
    VR_CHECK(!BoundsSystem3D::ContainsPointWorld(bounds, vr::ecs::Float3{.x = 8.0F, .y = 0.0F, .z = 2.0F}));
}

VR_TEST_CASE(EcsBoundsSystem_aligned_dirty_indices_path, "unit;core;ecs;bounds;aligned") {
    using Bounds2D = vr::ecs::Bounds<vr::ecs::Dim2>;
    using BoundsSystem2D = vr::ecs::BoundsSystem<vr::ecs::Dim2>;
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

    std::array<Bounds2D, 4U> bounds{};
    std::array<Transform2D, 4U> transforms{};
    for (std::uint32_t i = 0U; i < 4U; ++i) {
        BoundsSystem2D::Initialize(bounds[i]);
        TransformSystem2D::Initialize(transforms[i]);
        TransformSystem2D::SetLocalPosition(transforms[i],
                                            static_cast<float>(i) * 10.0F,
                                            static_cast<float>(i) * 4.0F);
    }
    TransformSystem2D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));
    (void)BoundsSystem2D::UpdateAligned(bounds.data(),
                                        transforms.data(),
                                        static_cast<std::uint32_t>(bounds.size()));

    const std::uint32_t dirty_indices[2U] = {1U, 3U};
    TransformSystem2D::SetLocalPosition(transforms[1U], 111.0F, 222.0F);
    TransformSystem2D::SetLocalPosition(transforms[3U], -30.0F, 80.0F);
    TransformSystem2D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    const std::uint32_t updated_count = BoundsSystem2D::UpdateAligned(bounds.data(),
                                                                      transforms.data(),
                                                                      static_cast<std::uint32_t>(bounds.size()),
                                                                      dirty_indices,
                                                                      2U);
    VR_CHECK(updated_count == 2U);
    VR_CHECK(bounds[1U].runtime.world_center.x == 111.0F);
    VR_CHECK(bounds[1U].runtime.world_center.y == 222.0F);
    VR_CHECK(bounds[3U].runtime.world_center.x == -30.0F);
    VR_CHECK(bounds[3U].runtime.world_center.y == 80.0F);
}

} // namespace


