#include "support/test_framework.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace {

[[nodiscard]] bool NearlyEqual(float lhs_, float rhs_, float epsilon_ = 1e-4F) {
    return std::abs(lhs_ - rhs_) <= epsilon_;
}

[[nodiscard]] float Mat4At(const vr::ecs::Matrix4x4& matrix_,
                           std::uint32_t row_,
                           std::uint32_t column_) {
    return matrix_.m[column_ * 4U + row_];
}

VR_TEST_CASE(EcsTransformCameraComponents_are_pure_pod, "unit;core;ecs;transform;camera") {
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Transform<vr::ecs::Dim2>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Transform<vr::ecs::Dim2>>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Transform<vr::ecs::Dim3>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Transform<vr::ecs::Dim3>>);

    VR_CHECK(std::is_standard_layout_v<vr::ecs::Camera<vr::ecs::Dim2>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Camera<vr::ecs::Dim2>>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Camera<vr::ecs::Dim3>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Camera<vr::ecs::Dim3>>);
}

VR_TEST_CASE(EcsTransformSystem_dim2_hierarchy_world_update, "unit;core;ecs;transform") {
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

    std::array<Transform2D, 2U> transforms{};
    for (auto& transform : transforms) {
        TransformSystem2D::Initialize(transform);
    }

    TransformSystem2D::SetLocalPosition(transforms[0U], 10.0F, 5.0F);
    TransformSystem2D::SetLocalPosition(transforms[1U], 2.0F, 1.0F);

    VR_REQUIRE(TransformSystem2D::AttachChild(transforms.data(),
                                              static_cast<std::uint32_t>(transforms.size()),
                                              1U,
                                              0U));

    TransformSystem2D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    VR_CHECK(NearlyEqual(transforms[0U].runtime.world_matrix.m02, 10.0F));
    VR_CHECK(NearlyEqual(transforms[0U].runtime.world_matrix.m12, 5.0F));
    VR_CHECK(NearlyEqual(transforms[1U].runtime.world_matrix.m02, 12.0F));
    VR_CHECK(NearlyEqual(transforms[1U].runtime.world_matrix.m12, 6.0F));

    VR_CHECK(transforms[1U].runtime.hierarchy.parent_index == 0);
    VR_CHECK(transforms[0U].runtime.hierarchy.first_child_index == 1);
    VR_CHECK(transforms[1U].runtime.dirty_flags == 0U);
}

VR_TEST_CASE(EcsTransformSystem_dim3_hierarchy_and_revisions, "unit;core;ecs;transform") {
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    std::array<Transform3D, 2U> transforms{};
    for (auto& transform : transforms) {
        TransformSystem3D::Initialize(transform);
    }

    TransformSystem3D::SetLocalPosition(transforms[0U], vr::ecs::Float3{.x = 1.0F, .y = 2.0F, .z = 3.0F});
    TransformSystem3D::SetLocalPosition(transforms[1U], vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = -2.0F});

    VR_REQUIRE(TransformSystem3D::AttachChild(transforms.data(),
                                              static_cast<std::uint32_t>(transforms.size()),
                                              1U,
                                              0U));

    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    VR_CHECK(NearlyEqual(Mat4At(transforms[1U].runtime.world_matrix, 0U, 3U), 1.0F));
    VR_CHECK(NearlyEqual(Mat4At(transforms[1U].runtime.world_matrix, 1U, 3U), 2.0F));
    VR_CHECK(NearlyEqual(Mat4At(transforms[1U].runtime.world_matrix, 2U, 3U), 1.0F));

    const std::uint32_t child_world_revision_before = transforms[1U].runtime.world_revision;
    TransformSystem3D::SetLocalRotationEulerXyz(transforms[0U], 0.0F, 1.57079632679F, 0.0F);
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    VR_CHECK(transforms[1U].runtime.world_revision > child_world_revision_before);

    const float dx = Mat4At(transforms[1U].runtime.world_matrix, 0U, 3U) -
                     Mat4At(transforms[0U].runtime.world_matrix, 0U, 3U);
    const float dy = Mat4At(transforms[1U].runtime.world_matrix, 1U, 3U) -
                     Mat4At(transforms[0U].runtime.world_matrix, 1U, 3U);
    const float dz = Mat4At(transforms[1U].runtime.world_matrix, 2U, 3U) -
                     Mat4At(transforms[0U].runtime.world_matrix, 2U, 3U);
    const float distance_sq = dx * dx + dy * dy + dz * dz;
    VR_CHECK(NearlyEqual(distance_sq, 4.0F, 1e-3F));
}

VR_TEST_CASE(EcsCameraSystem_dim2_projection_and_view_follow_transform, "unit;core;ecs;camera") {
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
    using Camera2D = vr::ecs::Camera<vr::ecs::Dim2>;
    using CameraSystem2D = vr::ecs::CameraSystem<vr::ecs::Dim2>;

    Transform2D transform{};
    TransformSystem2D::Initialize(transform);
    TransformSystem2D::SetLocalPosition(transform, 5.0F, 3.0F);
    TransformSystem2D::UpdateHierarchy(&transform, 1U);

    Camera2D camera{};
    CameraSystem2D::Initialize(camera);
    CameraSystem2D::MarkViewDirty(camera);
    CameraSystem2D::Update(camera, transform);

    VR_CHECK(NearlyEqual(Mat4At(camera.runtime.view_matrix, 0U, 3U), -5.0F));
    VR_CHECK(NearlyEqual(Mat4At(camera.runtime.view_matrix, 1U, 3U), -3.0F));
    VR_CHECK(Mat4At(camera.runtime.projection_matrix, 0U, 0U) > 0.0F);
    VR_CHECK(Mat4At(camera.runtime.projection_matrix, 1U, 1U) > 0.0F);

    CameraSystem2D::SetYDown(camera, true);
    CameraSystem2D::Update(camera, transform);
    VR_CHECK(Mat4At(camera.runtime.projection_matrix, 1U, 1U) < 0.0F);
}

VR_TEST_CASE(EcsCameraSystem_dim3_perspective_and_ortho_modes, "unit;core;ecs;camera") {
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 5.0F});
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    Camera3D camera{};
    CameraSystem3D::Initialize(camera);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, transform);

    VR_CHECK(NearlyEqual(Mat4At(camera.runtime.view_matrix, 2U, 3U), -5.0F));
    VR_CHECK(NearlyEqual(Mat4At(camera.runtime.projection_matrix, 3U, 2U), -1.0F));

    CameraSystem3D::SetProjectionMode(camera, vr::ecs::CameraProjectionMode::orthographic);
    CameraSystem3D::Update(camera, transform);

    VR_CHECK(NearlyEqual(Mat4At(camera.runtime.projection_matrix, 3U, 2U), 0.0F));
    VR_CHECK(NearlyEqual(Mat4At(camera.runtime.projection_matrix, 3U, 3U), 1.0F));
}

VR_TEST_CASE(EcsCameraSystem_update_aligned_batch, "unit;core;ecs;camera") {
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
    using Camera2D = vr::ecs::Camera<vr::ecs::Dim2>;
    using CameraSystem2D = vr::ecs::CameraSystem<vr::ecs::Dim2>;

    std::array<Transform2D, 3U> transforms{};
    std::array<Camera2D, 3U> cameras{};

    for (std::uint32_t i = 0U; i < transforms.size(); ++i) {
        TransformSystem2D::Initialize(transforms[i]);
        TransformSystem2D::SetLocalPosition(transforms[i], static_cast<float>(i), static_cast<float>(i * 2U));

        CameraSystem2D::Initialize(cameras[i]);
        CameraSystem2D::MarkViewDirty(cameras[i]);
    }

    TransformSystem2D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));
    CameraSystem2D::UpdateAligned(cameras.data(), transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    VR_CHECK(NearlyEqual(Mat4At(cameras[0U].runtime.view_matrix, 0U, 3U), 0.0F));
    VR_CHECK(NearlyEqual(Mat4At(cameras[1U].runtime.view_matrix, 0U, 3U), -1.0F));
    VR_CHECK(NearlyEqual(Mat4At(cameras[2U].runtime.view_matrix, 0U, 3U), -2.0F));
    VR_CHECK(cameras[2U].runtime.revision > 0U);
}

} // namespace
