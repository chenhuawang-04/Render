#include "support/test_framework.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/light_culling_system.hpp"
#include "vr/ecs/system/light_runtime_system.hpp"
#include "vr/ecs/system/light_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>

namespace {

VR_TEST_CASE(EcsLightCullingSystem_dim2_single_tile_collects_visible_lights, "unit;core;ecs;light;culling") {
    using Light2D = vr::ecs::Light<vr::ecs::Dim2>;
    using LightSystem2D = vr::ecs::LightSystem<vr::ecs::Dim2>;
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
    using Camera2D = vr::ecs::Camera<vr::ecs::Dim2>;
    using CameraSystem2D = vr::ecs::CameraSystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::LightRuntimeSystem<vr::ecs::Dim2>;
    using CullingSystem2D = vr::ecs::LightCullingSystem<vr::ecs::Dim2>;

    std::array<Light2D, 2U> lights{};
    std::array<Transform2D, 2U> transforms{};
    for (std::uint32_t i = 0U; i < lights.size(); ++i) {
        LightSystem2D::Initialize(lights[i]);
        TransformSystem2D::Initialize(transforms[i]);
    }
    LightSystem2D::SetRange(lights[0U], 8.0F);
    LightSystem2D::SetRange(lights[1U], 8.0F);
    TransformSystem2D::SetLocalPosition(transforms[0U], -1.0F, 0.0F);
    TransformSystem2D::SetLocalPosition(transforms[1U], 1.0F, 0.0F);
    TransformSystem2D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    Camera2D camera{};
    Transform2D camera_transform{};
    CameraSystem2D::Initialize(camera);
    TransformSystem2D::Initialize(camera_transform);
    CameraSystem2D::MarkViewDirty(camera);
    CameraSystem2D::Update(camera, camera_transform);

    vr::ecs::LightRuntimeScratch<vr::ecs::Dim2> runtime_scratch{};
    (void)RuntimeSystem2D::Build(lights.data(),
                                 transforms.data(),
                                 static_cast<std::uint32_t>(lights.size()),
                                 runtime_scratch,
                                 {},
                                 {});

    vr::ecs::LightCullingScratch<vr::ecs::Dim2> culling_scratch{};
    auto config = CullingSystem2D::DefaultBuildConfig();
    config.tile_count_x = 1U;
    config.tile_count_y = 1U;
    config.max_lights_per_tile = 8U;

    const auto stats = CullingSystem2D::Build(lights.data(),
                                              RuntimeSystem2D::DerivedGeomData(runtime_scratch),
                                              RuntimeSystem2D::DerivedOpticalData(runtime_scratch),
                                              static_cast<std::uint32_t>(lights.size()),
                                              &camera,
                                              culling_scratch,
                                              config);

    VR_CHECK(stats.candidate_light_count == 2U);
    VR_CHECK(stats.accepted_light_count == 2U);
    VR_CHECK(stats.cluster_count == 1U);
    VR_CHECK(stats.emitted_index_count == 2U);
    VR_CHECK(CullingSystem2D::ClusterHeaderCount(culling_scratch) == 1U);
    VR_CHECK(CullingSystem2D::ClusterHeaders(culling_scratch)[0U].count == 2U);
}

VR_TEST_CASE(EcsLightCullingSystem_dim3_single_cluster_stable_order, "unit;core;ecs;light;culling") {
    using Light3D = vr::ecs::Light<vr::ecs::Dim3>;
    using LightSystem3D = vr::ecs::LightSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::LightRuntimeSystem<vr::ecs::Dim3>;
    using CullingSystem3D = vr::ecs::LightCullingSystem<vr::ecs::Dim3>;

    std::array<Light3D, 2U> lights{};
    std::array<Transform3D, 2U> transforms{};
    for (std::uint32_t i = 0U; i < lights.size(); ++i) {
        LightSystem3D::Initialize(lights[i]);
        LightSystem3D::SetRange(lights[i], 10.0F);
        TransformSystem3D::Initialize(transforms[i]);
    }
    TransformSystem3D::SetLocalPosition(transforms[0U], vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = -4.0F});
    TransformSystem3D::SetLocalPosition(transforms[1U], vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = -8.0F});
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    Camera3D camera{};
    Transform3D camera_transform{};
    CameraSystem3D::Initialize(camera);
    TransformSystem3D::Initialize(camera_transform);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    vr::ecs::LightRuntimeScratch<vr::ecs::Dim3> runtime_scratch{};
    (void)RuntimeSystem3D::Build(lights.data(),
                                 transforms.data(),
                                 static_cast<std::uint32_t>(lights.size()),
                                 runtime_scratch,
                                 {},
                                 {});

    vr::ecs::LightCullingScratch<vr::ecs::Dim3> culling_scratch{};
    auto config = CullingSystem3D::DefaultBuildConfig();
    config.cluster_count_x = 1U;
    config.cluster_count_y = 1U;
    config.cluster_count_z = 1U;
    config.max_lights_per_cluster = 8U;
    config.stable_sort = 1U;

    const auto stats = CullingSystem3D::Build(lights.data(),
                                              RuntimeSystem3D::DerivedGeomData(runtime_scratch),
                                              RuntimeSystem3D::DerivedOpticalData(runtime_scratch),
                                              static_cast<std::uint32_t>(lights.size()),
                                              &camera,
                                              culling_scratch,
                                              config);

    VR_CHECK(stats.candidate_light_count == 2U);
    VR_CHECK(stats.accepted_light_count == 2U);
    VR_CHECK(stats.cluster_count == 1U);
    VR_CHECK(stats.emitted_index_count == 2U);
    VR_CHECK(stats.stable_sort_cluster_count == 1U);

    const auto* headers = CullingSystem3D::ClusterHeaders(culling_scratch);
    const auto* indices = CullingSystem3D::ClusterLightIndices(culling_scratch);
    VR_REQUIRE(headers != nullptr);
    VR_REQUIRE(indices != nullptr);
    VR_CHECK(headers[0U].count == 2U);
    // 近处 light[0] 应该在前
    VR_CHECK(indices[0U] == 0U);
    VR_CHECK(indices[1U] == 1U);
}

} // namespace


