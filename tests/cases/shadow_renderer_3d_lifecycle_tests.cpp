#include "support/test_framework.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/shadow_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/shadow/shadow_renderer_3d.hpp"

#include <array>

namespace {

VR_TEST_CASE(ShadowRenderer3D_lifecycle_no_runtime_calls, "unit;core;shadow;renderer") {
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
    using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;

    std::array<Shadow3D, 1U> shadows{};
    std::array<Geometry3D, 1U> geometry_components{};
    std::array<Transform3D, 1U> transforms{};
    std::array<Bounds3D, 1U> bounds{};
    Camera3D camera{};

    ShadowSystem3D::Initialize(shadows[0U]);
    GeometrySystem3D::Initialize(geometry_components[0U]);
    GeometrySystem3D::SetRuntimeRoute(geometry_components[0U], 1U, 1U, 0U);
    TransformSystem3D::Initialize(transforms[0U]);
    TransformSystem3D::UpdateHierarchy(transforms.data(), 1U);
    BoundsSystem3D::Initialize(bounds[0U]);
    BoundsSystem3D::SetLocalAabb(bounds[0U],
                                 vr::ecs::Float3{.x = -1.0F, .y = -1.0F, .z = -1.0F},
                                 vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
    (void)BoundsSystem3D::UpdateAligned(bounds.data(), transforms.data(), 1U);
    CameraSystem3D::Initialize(camera);

    vr::shadow::ShadowRenderer3D renderer{};
    renderer.Initialize();
    VR_CHECK(renderer.IsInitialized());

    renderer.SetSceneData(shadows.data(),
                          transforms.data(),
                          1U,
                          &camera,
                          bounds.data());
    renderer.SetGeometryData(geometry_components.data(), transforms.data(), 1U);
    const std::uint32_t dirty_index = 0U;
    renderer.SetShadowDirtyHint(&dirty_index, 1U);
    renderer.SetTransformDirtyHint(&dirty_index, 1U);

    const auto& stats = renderer.Stats();
    VR_CHECK(stats.shadow_component_count == 0U);
    VR_CHECK(stats.shadow_view_count == 0U);
}

} // namespace

