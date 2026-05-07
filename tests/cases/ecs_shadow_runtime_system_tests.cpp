#include "support/test_framework.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/shadow_runtime_system.hpp"
#include "vr/ecs/system/shadow_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>
#include <cmath>

namespace {

VR_TEST_CASE(EcsShadowRuntimeSystem_dim3_full_and_transform_update, "unit;core;ecs;shadow;runtime") {
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::ShadowRuntimeSystem<vr::ecs::Dim3>;
    using RuntimeScratch3D = vr::ecs::ShadowRuntimeScratch<vr::ecs::Dim3>;

    std::array<Shadow3D, 3U> shadows{};
    std::array<Transform3D, 3U> transforms{};

    for (std::uint32_t i = 0U; i < shadows.size(); ++i) {
        ShadowSystem3D::Initialize(shadows[i]);
        ShadowSystem3D::SetCascadeConfig(shadows[i], 2U, 0.5F);
        ShadowSystem3D::SetProjectionKind(shadows[i], vr::ecs::ShadowProjectionKind::directional);
        ShadowSystem3D::SetMapResolution(shadows[i], 1024U, 1024U);
        ShadowSystem3D::SetLightComponentIndex(shadows[i], i);
        ShadowSystem3D::SetTransformComponentIndex(shadows[i], i);

        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = 2.0F * static_cast<float>(i),
                                                .y = 8.0F,
                                                .z = -4.0F - static_cast<float>(i),
                                            });
        TransformSystem3D::SetLocalRotationEulerXyz(transforms[i],
                                                    -0.65F,
                                                    0.20F * static_cast<float>(i),
                                                    0.0F);
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    Camera3D camera{};
    Transform3D camera_transform{};
    CameraSystem3D::Initialize(camera);
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 3.0F, .z = 12.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(camera_transform, 0.0F, 3.1415926F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkDirty(camera, vr::ecs::camera_dirty_projection_flag | vr::ecs::camera_dirty_runtime_flag);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    RuntimeScratch3D runtime_scratch{};
    const auto full_stats = RuntimeSystem3D::Build(shadows.data(),
                                                   transforms.data(),
                                                   &camera,
                                                   static_cast<std::uint32_t>(shadows.size()),
                                                   runtime_scratch);

    VR_CHECK(full_stats.component_count == shadows.size());
    VR_CHECK(full_stats.updated_record_count == shadows.size());
    VR_CHECK(RuntimeSystem3D::GpuRecordCount(runtime_scratch) == shadows.size());
    VR_CHECK(RuntimeSystem3D::ViewRecordCount(runtime_scratch) == shadows.size() * 2U);
    VR_CHECK(full_stats.generated_view_count == RuntimeSystem3D::ViewRecordCount(runtime_scratch));
    VR_CHECK(full_stats.upload_range_count >= 1U);
    VR_CHECK(full_stats.view_upload_range_count >= 1U);

    TransformSystem3D::SetLocalPosition(transforms[1U],
                                        vr::ecs::Float3{.x = 9.0F, .y = 6.0F, .z = -3.0F});
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    const std::uint32_t transform_dirty_index = 1U;
    vr::ecs::ShadowRuntimeBuildHint build_hint{};
    build_hint.transform_dirty_component_indices = &transform_dirty_index;
    build_hint.transform_dirty_component_count = 1U;
    build_hint.use_transform_dirty_component_indices = 1U;

    const auto transform_stats = RuntimeSystem3D::Build(shadows.data(),
                                                        transforms.data(),
                                                        &camera,
                                                        static_cast<std::uint32_t>(shadows.size()),
                                                        runtime_scratch,
                                                        vr::ecs::ShadowRuntimeBuildConfig{},
                                                        build_hint);

    VR_CHECK(transform_stats.updated_transform_only_count >= 1U);
    VR_CHECK(transform_stats.updated_style_or_binding_count == 0U);
    VR_CHECK(transform_stats.transform_only_update);
    VR_CHECK(transform_stats.view_upload_range_count >= 1U);
}

VR_TEST_CASE(EcsShadowRuntimeSystem_dim3_view_flags_encode_filter_kernel, "unit;core;ecs;shadow;runtime") {
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::ShadowRuntimeSystem<vr::ecs::Dim3>;
    using RuntimeScratch3D = vr::ecs::ShadowRuntimeScratch<vr::ecs::Dim3>;

    std::array<Shadow3D, 1U> shadows{};
    std::array<Transform3D, 1U> transforms{};
    ShadowSystem3D::Initialize(shadows[0U]);
    ShadowSystem3D::SetProjectionKind(shadows[0U], vr::ecs::ShadowProjectionKind::directional);
    ShadowSystem3D::SetCascadeConfig(shadows[0U], 2U, 0.6F);
    ShadowSystem3D::SetFilterKernel(shadows[0U], vr::ecs::ShadowFilterKernel::pcf5x5);
    ShadowSystem3D::SetStabilize(shadows[0U], false);
    ShadowSystem3D::SetReverseZ(shadows[0U], true);

    TransformSystem3D::Initialize(transforms[0U]);
    TransformSystem3D::SetLocalPosition(transforms[0U],
                                        vr::ecs::Float3{.x = 0.0F, .y = 8.0F, .z = -4.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(transforms[0U], -0.65F, 0.0F, 0.0F);
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    Camera3D camera{};
    Transform3D camera_transform{};
    CameraSystem3D::Initialize(camera);
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 3.0F, .z = 10.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(camera_transform, 0.0F, 3.1415926F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkDirty(camera, vr::ecs::camera_dirty_projection_flag | vr::ecs::camera_dirty_runtime_flag);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    RuntimeScratch3D runtime_scratch{};
    const auto stats = RuntimeSystem3D::Build(shadows.data(),
                                              transforms.data(),
                                              &camera,
                                              static_cast<std::uint32_t>(shadows.size()),
                                              runtime_scratch);

    VR_CHECK(stats.generated_view_count == 2U);
    const vr::ecs::ShadowViewGpuRecord* view_records = RuntimeSystem3D::ViewRecords(runtime_scratch);
    VR_REQUIRE(view_records != nullptr);
    VR_CHECK((view_records[0U].flags & vr::ecs::shadow_view_flag_stabilize) == 0U);
    VR_CHECK((view_records[0U].flags & vr::ecs::shadow_view_flag_reverse_z) != 0U);
    VR_CHECK(vr::ecs::DecodeShadowViewFilterKernelFlags(view_records[0U].flags) ==
             vr::ecs::ShadowFilterKernel::pcf5x5);
}

VR_TEST_CASE(EcsShadowRuntimeSystem_dim3_stabilize_snaps_directional_shadow_center, "unit;core;ecs;shadow;runtime") {
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::ShadowRuntimeSystem<vr::ecs::Dim3>;
    using RuntimeScratch3D = vr::ecs::ShadowRuntimeScratch<vr::ecs::Dim3>;

    Shadow3D shadow{};
    Transform3D light_transform{};
    ShadowSystem3D::Initialize(shadow);
    ShadowSystem3D::SetProjectionKind(shadow, vr::ecs::ShadowProjectionKind::directional);
    ShadowSystem3D::SetCascadeConfig(shadow, 1U, 0.55F);
    ShadowSystem3D::SetMapResolution(shadow, 1024U, 1024U);
    ShadowSystem3D::SetStabilize(shadow, true);

    TransformSystem3D::Initialize(light_transform);
    TransformSystem3D::SetLocalPosition(light_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 12.0F, .z = 0.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(light_transform, -0.72F, 0.18F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&light_transform, 1U);

    Camera3D camera{};
    Transform3D camera_transform{};
    CameraSystem3D::Initialize(camera);
    CameraSystem3D::SetNearFar(camera, 0.05F, 128.0F);
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 3.0F, .z = 12.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(camera_transform, 0.0F, 3.1415926F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkDirty(camera, vr::ecs::camera_dirty_projection_flag | vr::ecs::camera_dirty_runtime_flag);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    std::array<Shadow3D, 1U> shadows{shadow};
    std::array<Transform3D, 1U> shadow_transforms{light_transform};
    RuntimeScratch3D stabilized_scratch{};
    (void)RuntimeSystem3D::Build(shadows.data(),
                                 shadow_transforms.data(),
                                 &camera,
                                 static_cast<std::uint32_t>(shadows.size()),
                                 stabilized_scratch);
    const vr::ecs::ShadowViewGpuRecord* stable_view_records_a =
        RuntimeSystem3D::ViewRecords(stabilized_scratch);
    VR_REQUIRE(stable_view_records_a != nullptr);
    const float stable_tx_a = stable_view_records_a[0U].view_matrix.m[12];
    const float stable_ty_a = stable_view_records_a[0U].view_matrix.m[13];

    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.25F, .y = 3.0F, .z = 12.0F});
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);
    (void)RuntimeSystem3D::Build(shadows.data(),
                                 shadow_transforms.data(),
                                 &camera,
                                 static_cast<std::uint32_t>(shadows.size()),
                                 stabilized_scratch);
    const vr::ecs::ShadowViewGpuRecord* stable_view_records_b =
        RuntimeSystem3D::ViewRecords(stabilized_scratch);
    VR_REQUIRE(stable_view_records_b != nullptr);
    const float stable_tx_b = stable_view_records_b[0U].view_matrix.m[12];
    const float stable_ty_b = stable_view_records_b[0U].view_matrix.m[13];

    VR_CHECK(std::abs(stable_tx_a - stable_tx_b) < 1e-6F);
    VR_CHECK(std::abs(stable_ty_a - stable_ty_b) < 1e-6F);

    ShadowSystem3D::SetStabilize(shadows[0U], false);
    RuntimeScratch3D unstabilized_scratch{};
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 3.0F, .z = 12.0F});
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);
    (void)RuntimeSystem3D::Build(shadows.data(),
                                 shadow_transforms.data(),
                                 &camera,
                                 static_cast<std::uint32_t>(shadows.size()),
                                 unstabilized_scratch);
    const vr::ecs::ShadowViewGpuRecord* unstable_view_records_a =
        RuntimeSystem3D::ViewRecords(unstabilized_scratch);
    VR_REQUIRE(unstable_view_records_a != nullptr);
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.25F, .y = 3.0F, .z = 12.0F});
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);
    (void)RuntimeSystem3D::Build(shadows.data(),
                                 shadow_transforms.data(),
                                 &camera,
                                 static_cast<std::uint32_t>(shadows.size()),
                                 unstabilized_scratch);
    const vr::ecs::ShadowViewGpuRecord* unstable_view_records_b =
        RuntimeSystem3D::ViewRecords(unstabilized_scratch);
    VR_REQUIRE(unstable_view_records_b != nullptr);
    const float unstable_tx_b = unstable_view_records_b[0U].view_matrix.m[12];
    const float unstable_ty_b = unstable_view_records_b[0U].view_matrix.m[13];

    VR_CHECK(std::abs(unstable_tx_b - stable_tx_b) > 1e-6F ||
             std::abs(unstable_ty_b - stable_ty_b) > 1e-6F);
}

VR_TEST_CASE(EcsShadowRuntimeSystem_dim3_fit_mode_changes_directional_extent, "unit;core;ecs;shadow;runtime") {
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::ShadowRuntimeSystem<vr::ecs::Dim3>;
    using RuntimeScratch3D = vr::ecs::ShadowRuntimeScratch<vr::ecs::Dim3>;

    Shadow3D shadow{};
    Transform3D light_transform{};
    ShadowSystem3D::Initialize(shadow);
    ShadowSystem3D::SetProjectionKind(shadow, vr::ecs::ShadowProjectionKind::directional);
    ShadowSystem3D::SetCascadeConfig(shadow, 1U, 0.55F);
    ShadowSystem3D::SetMapResolution(shadow, 1024U, 1024U);
    ShadowSystem3D::SetStabilize(shadow, false);

    TransformSystem3D::Initialize(light_transform);
    TransformSystem3D::SetLocalPosition(light_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 12.0F, .z = 0.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(light_transform, -0.72F, 0.18F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&light_transform, 1U);

    Camera3D camera{};
    Transform3D camera_transform{};
    CameraSystem3D::Initialize(camera);
    CameraSystem3D::SetAspectRatio(camera, 16.0F / 9.0F);
    CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);
    CameraSystem3D::SetNearFar(camera, 0.05F, 128.0F);
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 3.0F, .z = 12.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(camera_transform, 0.0F, 3.1415926F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkDirty(camera, vr::ecs::camera_dirty_projection_flag | vr::ecs::camera_dirty_runtime_flag);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    std::array<Shadow3D, 1U> shadows{shadow};
    std::array<Transform3D, 1U> shadow_transforms{light_transform};

    RuntimeScratch3D stable_scratch{};
    ShadowSystem3D::SetFitMode(shadows[0U], vr::ecs::ShadowFitMode::stable);
    (void)RuntimeSystem3D::Build(shadows.data(),
                                 shadow_transforms.data(),
                                 &camera,
                                 static_cast<std::uint32_t>(shadows.size()),
                                 stable_scratch);
    const vr::ecs::ShadowViewGpuRecord* stable_view_records = RuntimeSystem3D::ViewRecords(stable_scratch);
    VR_REQUIRE(stable_view_records != nullptr);

    RuntimeScratch3D tight_scratch{};
    ShadowSystem3D::SetFitMode(shadows[0U], vr::ecs::ShadowFitMode::tight);
    (void)RuntimeSystem3D::Build(shadows.data(),
                                 shadow_transforms.data(),
                                 &camera,
                                 static_cast<std::uint32_t>(shadows.size()),
                                 tight_scratch);
    const vr::ecs::ShadowViewGpuRecord* tight_view_records = RuntimeSystem3D::ViewRecords(tight_scratch);
    VR_REQUIRE(tight_view_records != nullptr);

    VR_CHECK(stable_view_records[0U].projection_matrix.m[5] <
             tight_view_records[0U].projection_matrix.m[5]);
}

VR_TEST_CASE(EcsShadowRuntimeSystem_dim2_build_views, "unit;core;ecs;shadow;runtime") {
    using Shadow2D = vr::ecs::Shadow<vr::ecs::Dim2>;
    using ShadowSystem2D = vr::ecs::ShadowSystem<vr::ecs::Dim2>;
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::ShadowRuntimeSystem<vr::ecs::Dim2>;
    using RuntimeScratch2D = vr::ecs::ShadowRuntimeScratch<vr::ecs::Dim2>;

    std::array<Shadow2D, 2U> shadows{};
    std::array<Transform2D, 2U> transforms{};

    for (std::uint32_t i = 0U; i < shadows.size(); ++i) {
        ShadowSystem2D::Initialize(shadows[i]);
        ShadowSystem2D::SetMapResolution(shadows[i], 512U, 512U);
        ShadowSystem2D::SetLightComponentIndex(shadows[i], i);
        ShadowSystem2D::SetTransformComponentIndex(shadows[i], i);

        TransformSystem2D::Initialize(transforms[i]);
        TransformSystem2D::SetLocalPosition(transforms[i],
                                            static_cast<float>(i) * 40.0F,
                                            12.0F);
    }
    TransformSystem2D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    RuntimeScratch2D runtime_scratch{};
    const auto stats = RuntimeSystem2D::Build(shadows.data(),
                                              transforms.data(),
                                              static_cast<const vr::ecs::Camera<vr::ecs::Dim2>*>(nullptr),
                                              static_cast<std::uint32_t>(shadows.size()),
                                              runtime_scratch);

    VR_CHECK(stats.updated_record_count == shadows.size());
    VR_CHECK(RuntimeSystem2D::ViewRecordCount(runtime_scratch) == shadows.size());
    VR_CHECK(stats.generated_view_count == shadows.size());
}

} // namespace

