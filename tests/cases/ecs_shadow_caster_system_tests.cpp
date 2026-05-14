#include "support/test_framework.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/shadow_caster_system.hpp"
#include "vr/ecs/system/shadow_runtime_system.hpp"
#include "vr/ecs/system/shadow_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>

namespace {

VR_TEST_CASE(EcsShadowCasterSystem_dim3_build, "unit;core;ecs;shadow;culling") {
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;
    using ShadowRuntimeSystem3D = vr::ecs::ShadowRuntimeSystem<vr::ecs::Dim3>;
    using ShadowRuntimeScratch3D = vr::ecs::ShadowRuntimeScratch<vr::ecs::Dim3>;
    using ShadowCasterSystem3D = vr::ecs::ShadowCasterSystem<vr::ecs::Dim3>;
    using ShadowCasterScratch3D = vr::ecs::ShadowCasterScratch<vr::ecs::Dim3>;

    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
    using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

    std::array<Shadow3D, 1U> shadows{};
    std::array<Transform3D, 1U> shadow_transforms{};
    std::array<Bounds3D, 6U> caster_bounds{};
    std::array<Transform3D, 6U> caster_transforms{};

    ShadowSystem3D::Initialize(shadows[0U]);
    ShadowSystem3D::SetCascadeConfig(shadows[0U], 2U, 0.55F);
    ShadowSystem3D::SetProjectionKind(shadows[0U], vr::ecs::ShadowProjectionKind::directional);
    ShadowSystem3D::SetMapResolution(shadows[0U], 1024U, 1024U);

    TransformSystem3D::Initialize(shadow_transforms[0U]);
    TransformSystem3D::SetLocalPosition(shadow_transforms[0U], vr::ecs::Float3{.x = 0.0F, .y = 12.0F, .z = 0.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(shadow_transforms[0U], -1.0F, 0.0F, 0.0F);
    TransformSystem3D::UpdateHierarchy(shadow_transforms.data(), 1U);

    for (std::uint32_t i = 0U; i < caster_bounds.size(); ++i) {
        TransformSystem3D::Initialize(caster_transforms[i]);
        TransformSystem3D::SetLocalPosition(caster_transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i) * 2.0F - 4.0F,
                                                .y = 0.0F,
                                                .z = -6.0F - static_cast<float>(i),
                                            });
        BoundsSystem3D::Initialize(caster_bounds[i]);
        BoundsSystem3D::SetLocalCenterExtents(caster_bounds[i],
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 0.8F, .y = 0.8F, .z = 0.8F});
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
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 2.0F, .z = 14.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(camera_transform, 0.0F, 3.1415926F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkDirty(camera, vr::ecs::camera_dirty_projection_flag | vr::ecs::camera_dirty_runtime_flag);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    ShadowRuntimeScratch3D runtime_scratch{};
    const auto runtime_stats = ShadowRuntimeSystem3D::Build(shadows.data(),
                                                            shadow_transforms.data(),
                                                            &camera,
                                                            static_cast<std::uint32_t>(shadows.size()),
                                                            runtime_scratch);
    VR_REQUIRE(runtime_stats.generated_view_count > 0U);

    ShadowCasterScratch3D caster_scratch{};
    const auto caster_stats = ShadowCasterSystem3D::Build(shadows.data(),
                                                           static_cast<std::uint32_t>(shadows.size()),
                                                           ShadowRuntimeSystem3D::ViewRecords(runtime_scratch),
                                                           ShadowRuntimeSystem3D::ViewRecordCount(runtime_scratch),
                                                           caster_bounds.data(),
                                                           static_cast<std::uint32_t>(caster_bounds.size()),
                                                           caster_scratch);

    VR_CHECK(caster_stats.shadow_view_count == ShadowRuntimeSystem3D::ViewRecordCount(runtime_scratch));
    VR_CHECK(caster_stats.caster_input_count == caster_bounds.size());
    VR_CHECK(caster_stats.candidate_caster_count == caster_bounds.size());
    VR_CHECK(caster_stats.accepted_caster_count > 0U);
    VR_CHECK(ShadowCasterSystem3D::HeaderCount(caster_scratch) == caster_stats.shadow_view_count);
    VR_CHECK(ShadowCasterSystem3D::CasterIndexCount(caster_scratch) == caster_stats.emitted_index_count);
}

} // namespace


