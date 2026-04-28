#include "support/test_framework.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/light_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/light_frame_coordinator.hpp"

#include <array>

namespace {

VR_TEST_CASE(RenderLightFrameCoordinator_dim3_reuse_in_same_frame, "unit;core;render;light;coordinator") {
    using Light3D = vr::ecs::Light<vr::ecs::Dim3>;
    using LightSystem3D = vr::ecs::LightSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::LightFrameCoordinator<vr::ecs::Dim3>;

    std::array<Light3D, 2U> lights{};
    std::array<Transform3D, 2U> transforms{};
    for (std::uint32_t i = 0U; i < lights.size(); ++i) {
        LightSystem3D::Initialize(lights[i]);
        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = 0.0F,
                                                .y = 0.0F,
                                                .z = -4.0F - static_cast<float>(i),
                                            });
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    Camera3D camera{};
    Transform3D camera_transform{};
    CameraSystem3D::Initialize(camera);
    TransformSystem3D::Initialize(camera_transform);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    Coordinator3D coordinator{};
    coordinator.SetLightData(lights.data(), transforms.data(), static_cast<std::uint32_t>(lights.size()));
    coordinator.SetCamera(&camera);

    const auto result0 = coordinator.PrepareFrame(3U);
    VR_CHECK(result0.has_light_data);
    VR_CHECK(result0.runtime_build_invoked);
    VR_CHECK(result0.culling_build_invoked);

    const auto result1 = coordinator.PrepareFrame(3U);
    VR_CHECK(result1.has_light_data);
    VR_CHECK(!result1.runtime_build_invoked);
    VR_CHECK(!result1.culling_build_invoked);

    const auto& stats = coordinator.Stats();
    VR_CHECK(stats.prepare_call_count == 2U);
    VR_CHECK(stats.runtime_build_call_count == 1U);
    VR_CHECK(stats.culling_build_call_count == 1U);
    VR_CHECK(stats.same_frame_reuse_hit_count >= 1U);
}

VR_TEST_CASE(RenderLightFrameCoordinator_dim3_dirty_hint_rebuilds_runtime, "unit;core;render;light;coordinator") {
    using Light3D = vr::ecs::Light<vr::ecs::Dim3>;
    using LightSystem3D = vr::ecs::LightSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::LightFrameCoordinator<vr::ecs::Dim3>;

    std::array<Light3D, 2U> lights{};
    std::array<Transform3D, 2U> transforms{};
    for (std::uint32_t i = 0U; i < lights.size(); ++i) {
        LightSystem3D::Initialize(lights[i]);
        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i),
                                                .y = 0.0F,
                                                .z = -6.0F,
                                            });
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    Camera3D camera{};
    Transform3D camera_transform{};
    CameraSystem3D::Initialize(camera);
    TransformSystem3D::Initialize(camera_transform);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    Coordinator3D coordinator{};
    coordinator.SetLightData(lights.data(), transforms.data(), static_cast<std::uint32_t>(lights.size()));
    coordinator.SetCamera(&camera);

    const auto frame0 = coordinator.PrepareFrame(0U);
    VR_CHECK(frame0.runtime_build_invoked);
    VR_CHECK(frame0.culling_build_invoked);

    LightSystem3D::SetIntensity(lights[1U], 777.0F);
    const std::uint32_t dirty_index = 1U;
    coordinator.SetLightDirtyHint(&dirty_index, 1U);

    const auto frame1 = coordinator.PrepareFrame(1U);
    VR_CHECK(frame1.runtime_build_invoked);
    VR_CHECK(frame1.culling_build_invoked);
    VR_CHECK(frame1.runtime_stats.updated_record_count >= 1U);
    VR_CHECK(frame1.runtime_stats.updated_style_or_binding_count >= 1U);

    const auto& stats = coordinator.Stats();
    VR_CHECK(stats.runtime_build_call_count >= 2U);
    VR_CHECK(stats.light_dirty_hint_input_count == 1U);
    VR_CHECK(stats.light_dirty_hint_unique_count >= 1U);
}

} // namespace

