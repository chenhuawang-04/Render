#include "support/test_framework.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/shadow_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/shadow_frame_coordinator.hpp"

#include <array>

namespace {

VR_TEST_CASE(RenderShadowFrameCoordinator_dim3_reuse_and_dirty, "unit;core;render;shadow;coordinator") {
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
    using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::ShadowFrameCoordinator<vr::ecs::Dim3>;

    std::array<Shadow3D, 2U> shadows{};
    std::array<Transform3D, 2U> shadow_transforms{};

    std::array<Bounds3D, 8U> caster_bounds{};
    std::array<Transform3D, 8U> caster_transforms{};

    for (std::uint32_t i = 0U; i < shadows.size(); ++i) {
        ShadowSystem3D::Initialize(shadows[i]);
        ShadowSystem3D::SetCascadeConfig(shadows[i], 2U, 0.5F);
        ShadowSystem3D::SetProjectionKind(shadows[i], vr::ecs::ShadowProjectionKind::directional);

        TransformSystem3D::Initialize(shadow_transforms[i]);
        TransformSystem3D::SetLocalPosition(shadow_transforms[i],
                                            vr::ecs::Float3{.x = 0.0F, .y = 10.0F + static_cast<float>(i), .z = -2.0F});
        TransformSystem3D::SetLocalRotationEulerXyz(shadow_transforms[i], -0.9F, 0.2F * static_cast<float>(i), 0.0F);
    }
    TransformSystem3D::UpdateHierarchy(shadow_transforms.data(), static_cast<std::uint32_t>(shadow_transforms.size()));

    for (std::uint32_t i = 0U; i < caster_bounds.size(); ++i) {
        TransformSystem3D::Initialize(caster_transforms[i]);
        TransformSystem3D::SetLocalPosition(caster_transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i) * 1.5F - 5.0F,
                                                .y = 0.0F,
                                                .z = -6.0F - static_cast<float>(i),
                                            });
        BoundsSystem3D::Initialize(caster_bounds[i]);
        BoundsSystem3D::SetLocalCenterExtents(caster_bounds[i],
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 0.9F, .y = 0.9F, .z = 0.9F});
    }
    TransformSystem3D::UpdateHierarchy(caster_transforms.data(), static_cast<std::uint32_t>(caster_transforms.size()));
    const std::uint32_t updated_bounds_count = BoundsSystem3D::UpdateAligned(
        caster_bounds.data(),
        caster_transforms.data(),
        static_cast<std::uint32_t>(caster_bounds.size()));
    VR_CHECK(updated_bounds_count == caster_bounds.size());

    Camera3D camera{};
    Transform3D camera_transform{};
    CameraSystem3D::Initialize(camera);
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform, vr::ecs::Float3{.x = 0.0F, .y = 2.0F, .z = 12.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(camera_transform, 0.0F, 3.1415926F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkDirty(camera, vr::ecs::camera_dirty_projection_flag | vr::ecs::camera_dirty_runtime_flag);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    Coordinator3D coordinator{};
    coordinator.SetShadowData(shadows.data(), shadow_transforms.data(), static_cast<std::uint32_t>(shadows.size()));
    coordinator.SetCamera(&camera);
    coordinator.SetCasterBounds(caster_bounds.data(), static_cast<std::uint32_t>(caster_bounds.size()));

    const auto frame0 = coordinator.PrepareFrame(11U);
    VR_CHECK(frame0.has_shadow_data);
    VR_CHECK(frame0.runtime_build_invoked);
    VR_CHECK(frame0.caster_build_invoked);

    const auto frame1 = coordinator.PrepareFrame(11U);
    VR_CHECK(frame1.has_shadow_data);
    VR_CHECK(!frame1.runtime_build_invoked);
    VR_CHECK(!frame1.caster_build_invoked);

    TransformSystem3D::SetLocalPosition(shadow_transforms[0U],
                                        vr::ecs::Float3{.x = 0.0F, .y = 12.0F, .z = -3.0F});
    TransformSystem3D::UpdateHierarchy(shadow_transforms.data(), static_cast<std::uint32_t>(shadow_transforms.size()));
    const std::uint32_t transform_dirty_index = 0U;
    coordinator.SetTransformDirtyHint(&transform_dirty_index, 1U);

    const auto frame2 = coordinator.PrepareFrame(12U);
    VR_CHECK(frame2.runtime_build_invoked);
    VR_CHECK(frame2.caster_build_invoked);
    VR_CHECK(frame2.runtime_stats.updated_transform_only_count >= 1U);

    const auto& stats = coordinator.Stats();
    VR_CHECK(stats.prepare_call_count == 3U);
    VR_CHECK(stats.runtime_build_call_count >= 2U);
    VR_CHECK(stats.caster_build_call_count >= 2U);
    VR_CHECK(stats.same_frame_reuse_hit_count >= 1U);
    VR_CHECK(stats.transform_dirty_hint_input_count == 1U);
    VR_CHECK(stats.transform_dirty_hint_unique_count >= 1U);
}

} // namespace


