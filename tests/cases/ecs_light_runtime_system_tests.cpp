#include "support/test_framework.hpp"
#include "vr/ecs/system/light_runtime_system.hpp"
#include "vr/ecs/system/light_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>
#include <cmath>

namespace {

VR_TEST_CASE(EcsLightRuntimeSystem_dim2_build_and_transform_partial_update, "unit;core;ecs;light;runtime") {
    using Light2D = vr::ecs::Light<vr::ecs::Dim2>;
    using LightSystem2D = vr::ecs::LightSystem<vr::ecs::Dim2>;
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::LightRuntimeSystem<vr::ecs::Dim2>;

    Light2D light{};
    Transform2D transform{};
    LightSystem2D::Initialize(light);
    TransformSystem2D::Initialize(transform);

    LightSystem2D::SetIntensity(light, 3.0F);
    LightSystem2D::SetRange(light, 128.0F);
    TransformSystem2D::SetLocalPosition(transform, 4.0F, 6.0F);
    TransformSystem2D::SetLocalRotationRadians(transform, 0.0F);
    TransformSystem2D::UpdateHierarchy(&transform, 1U);

    vr::ecs::LightRuntimeScratch<vr::ecs::Dim2> scratch{};
    const auto stats0 = RuntimeSystem2D::Build(&light, &transform, 1U, scratch, {}, {});
    VR_CHECK(stats0.component_count == 1U);
    VR_CHECK(stats0.updated_record_count == 1U);
    VR_CHECK(stats0.upload_range_count == 1U);
    VR_CHECK(!stats0.cache_reused);

    const auto* records = RuntimeSystem2D::GpuRecords(scratch);
    VR_REQUIRE(records != nullptr);
    VR_CHECK(records[0U].position_x == 4.0F);
    VR_CHECK(records[0U].position_y == 6.0F);
    VR_CHECK(records[0U].radius == 128.0F);
    VR_CHECK(records[0U].intensity == 3.0F);

    const float prev_x = records[0U].position_x;
    TransformSystem2D::SetLocalPosition(transform, 14.0F, -2.0F);
    TransformSystem2D::UpdateHierarchy(&transform, 1U);

    const std::uint32_t transform_dirty_index = 0U;
    vr::ecs::LightRuntimeBuildHint hint{};
    hint.transform_dirty_component_indices = &transform_dirty_index;
    hint.transform_dirty_component_count = 1U;
    hint.use_transform_dirty_component_indices = 1U;

    const auto stats1 = RuntimeSystem2D::Build(&light, &transform, 1U, scratch, {}, hint);
    VR_CHECK(stats1.updated_transform_only_count == 1U);
    VR_CHECK(stats1.transform_only_update);
    VR_CHECK(RuntimeSystem2D::GpuRecords(scratch)[0U].position_x != prev_x);
    VR_CHECK(RuntimeSystem2D::GpuRecords(scratch)[0U].position_x == 14.0F);
}

VR_TEST_CASE(EcsLightRuntimeSystem_dim3_build_dirty_and_upload_ranges, "unit;core;ecs;light;runtime") {
    using Light3D = vr::ecs::Light<vr::ecs::Dim3>;
    using LightSystem3D = vr::ecs::LightSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::LightRuntimeSystem<vr::ecs::Dim3>;

    std::array<Light3D, 2U> lights{};
    std::array<Transform3D, 2U> transforms{};
    for (std::uint32_t i = 0U; i < lights.size(); ++i) {
        LightSystem3D::Initialize(lights[i]);
        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i),
                                                .y = 0.0F,
                                                .z = -5.0F - static_cast<float>(i),
                                            });
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::LightRuntimeScratch<vr::ecs::Dim3> scratch{};
    vr::ecs::LightRuntimeBuildConfig build_config{};
    build_config.merge_gap = 1U;

    const auto stats0 = RuntimeSystem3D::Build(lights.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(lights.size()),
                                               scratch,
                                               build_config,
                                               {});
    VR_CHECK(stats0.updated_record_count == 2U);
    VR_CHECK(stats0.upload_range_count == 1U);
    VR_CHECK(stats0.updated_style_or_binding_count == 2U);

    LightSystem3D::SetIntensity(lights[1U], 1234.0F);
    const std::uint32_t dirty_index = 1U;
    vr::ecs::LightRuntimeBuildHint hint{};
    hint.dirty_component_indices = &dirty_index;
    hint.dirty_component_count = 1U;
    hint.use_dirty_component_indices = 1U;

    const auto stats1 = RuntimeSystem3D::Build(lights.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(lights.size()),
                                               scratch,
                                               build_config,
                                               hint);
    VR_CHECK(stats1.updated_record_count == 1U);
    VR_CHECK(stats1.updated_style_or_binding_count == 1U);
    VR_CHECK(stats1.upload_range_count == 1U);
    VR_CHECK(!stats1.transform_only_update);
    VR_CHECK(std::abs(RuntimeSystem3D::GpuRecords(scratch)[1U].intensity - 1234.0F) < 0.001F);
}

} // namespace

