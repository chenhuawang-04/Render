#include "support/test_framework.hpp"
#include "vr/ecs/system/geometry_mesh_system.hpp"
#include "vr/ecs/system/geometry_path_system.hpp"
#include "vr/ecs/system/geometry_runtime_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>
#include <cstdint>

namespace {

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim2_builds_primitives_and_batches, "unit;core;ecs;geometry;runtime") {
    using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
    using PathSystem = vr::ecs::GeometryPathSystem;
    using GeometrySystem2D = vr::ecs::GeometrySystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim2>;

    std::array<Geometry2D, 2U> components{};
    for (auto& component : components) {
        PathSystem::Initialize(component);
    }

    // component 0: full path chain
    VR_REQUIRE(PathSystem::AppendMoveTo(components[0U], 0.0F, 0.0F));
    VR_REQUIRE(PathSystem::AppendLineTo(components[0U], 1.0F, 0.0F));
    VR_REQUIRE(PathSystem::AppendQuadTo(components[0U], 1.5F, 0.5F, 1.0F, 1.0F));
    VR_REQUIRE(PathSystem::AppendCubicTo(components[0U], 0.5F, 1.5F, -0.5F, 1.5F, -1.0F, 1.0F));
    VR_REQUIRE(PathSystem::AppendClose(components[0U]));
    GeometrySystem2D::SetRuntimeRoute(components[0U], 2U, 7U, 1U);

    // component 1: empty and hidden to validate stats
    GeometrySystem2D::SetVisible(components[1U], false);

    vr::ecs::Geometry2DRuntimeScratch scratch{};
    vr::ecs::Geometry2DRuntimeBuildConfig config{};
    config.quad_subdivision = 2U;
    config.cubic_subdivision = 3U;
    config.max_primitives_per_component = 0U;
    config.zero_length_epsilon = 1e-7F;

    const auto stats = RuntimeSystem2D::Build(components.data(),
                                              static_cast<std::uint32_t>(components.size()),
                                              scratch,
                                              config);

    VR_CHECK(stats.batch.total_count == static_cast<std::uint32_t>(components.size()));
    VR_CHECK(stats.batch.visible_count == 1U);
    VR_CHECK(stats.batch.hidden_count == 1U);
    VR_CHECK(stats.batch.empty_count == 0U);

    // line(1) + quad(2) + cubic(3) + close(1) = 7
    VR_CHECK(stats.emitted_primitive_count == 7U);
    VR_CHECK(stats.emitted_batch_count == 1U);
    VR_CHECK(stats.truncated_component_count == 0U);
    VR_CHECK(!scratch.primitives.empty());
    VR_CHECK(!scratch.draw_batches.empty());

    const vr::ecs::Geometry2DPathPrimitive& first = scratch.primitives[0U];
    VR_CHECK(first.x0 == 0.0F);
    VR_CHECK(first.y0 == 0.0F);
    VR_CHECK(first.x1 == 1.0F);
    VR_CHECK(first.y1 == 0.0F);
    VR_CHECK(first.component_index == 0U);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim2_respects_primitive_limit, "unit;core;ecs;geometry;runtime") {
    using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
    using PathSystem = vr::ecs::GeometryPathSystem;
    using RuntimeSystem2D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim2>;

    Geometry2D component{};
    PathSystem::Initialize(component);
    VR_REQUIRE(PathSystem::AppendMoveTo(component, 0.0F, 0.0F));
    for (std::uint32_t i = 0U; i < 16U; ++i) {
        VR_REQUIRE(PathSystem::AppendLineTo(component, static_cast<float>(i + 1U), 0.0F));
    }

    vr::ecs::Geometry2DRuntimeScratch scratch{};
    vr::ecs::Geometry2DRuntimeBuildConfig config{};
    config.max_primitives_per_component = 5U;

    const auto stats = RuntimeSystem2D::Build(&component, 1U, scratch, config);
    VR_CHECK(stats.emitted_primitive_count == 5U);
    VR_CHECK(stats.truncated_component_count == 1U);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_builds_instances_and_batches, "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    std::array<Geometry3D, 2U> components{};
    std::array<Transform3D, 2U> transforms{};

    for (std::uint32_t i = 0U; i < components.size(); ++i) {
        MeshSystem::Initialize(components[i]);
        MeshSystem::SetMeshRoute(components[i], 41U, 0U, 0U);
        GeometrySystem3D::SetMaterialId(components[i], 5U);
        GeometrySystem3D::SetBatchTag(components[i], 0U);
        GeometrySystem3D::SetDepthBin(components[i], 2U);
        MeshSystem::SetDepthTest(components[i], true);
        MeshSystem::SetDepthWrite(components[i], (i == 0U));
        MeshSystem::SetCastShadow(components[i], true);

        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i),
                                                .y = 0.5F * static_cast<float>(i),
                                                .z = 2.0F + static_cast<float>(i)
                                            });
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    const auto stats = RuntimeSystem3D::Build(components.data(),
                                              transforms.data(),
                                              static_cast<std::uint32_t>(components.size()),
                                              scratch,
                                              {});

    VR_CHECK(stats.batch.visible_count == 2U);
    VR_CHECK(stats.emitted_instance_count == 2U);
    // two components differ in depth_write, so params differ => cannot merge
    VR_CHECK(stats.emitted_batch_count == 2U);
    VR_CHECK(stats.depth_test_batch_count == 2U);
    VR_CHECK(stats.depth_write_batch_count == 1U);
    VR_CHECK(stats.shadow_cast_batch_count == 2U);

    const vr::ecs::Geometry3DGpuInstance& instance0 = scratch.instances[0U];
    const vr::ecs::Geometry3DGpuInstance& instance1 = scratch.instances[1U];
    VR_CHECK(instance0.geometry_id == 41U);
    VR_CHECK(instance1.geometry_id == 41U);
    VR_CHECK(instance0.material_id == 5U);
    VR_CHECK(instance0.world_m32 == 2.0F);
    VR_CHECK(instance1.world_m30 == 1.0F);
}

} // namespace
