#include "support/test_framework.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/appearance_runtime_system.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/geometry_mesh_system.hpp"
#include "vr/ecs/system/geometry_path_system.hpp"
#include "vr/ecs/system/geometry_runtime_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>
#include <cstdint>

namespace {

using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
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
    VR_CHECK(stats.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::cold_start);
    VR_CHECK(!stats.cache_reused);
    VR_CHECK(!stats.cache_valid_before_build);
    VR_CHECK(!stats.cache_key_matched);
    VR_CHECK(stats.cache_epoch > 0U);
    VR_CHECK(!scratch.primitives.empty());
    VR_CHECK(!scratch.draw_batches.empty());

    const vr::ecs::Geometry2DPathPrimitive& first = scratch.primitives[0U];
    VR_CHECK(first.x0 == 0.0F);
    VR_CHECK(first.y0 == 0.0F);
    VR_CHECK(first.x1 == 1.0F);
    VR_CHECK(first.y1 == 0.0F);
    VR_CHECK(first.component_index == 0U);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim2_reuses_cache_for_unchanged_input, "unit;core;ecs;geometry;runtime") {
    using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
    using PathSystem = vr::ecs::GeometryPathSystem;
    using RuntimeSystem2D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim2>;

    Geometry2D component{};
    PathSystem::Initialize(component);
    VR_REQUIRE(PathSystem::AppendMoveTo(component, -2.0F, 0.0F));
    VR_REQUIRE(PathSystem::AppendLineTo(component, 2.0F, 0.0F));

    vr::ecs::Geometry2DRuntimeScratch scratch{};
    const auto stats0 = RuntimeSystem2D::Build(&component, 1U, scratch, {});
    const std::uint32_t cache_epoch0 = stats0.cache_epoch;
    VR_CHECK(stats0.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats0.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::cold_start);
    VR_CHECK(!stats0.cache_reused);
    VR_CHECK(!stats0.cache_valid_before_build);
    VR_CHECK(!stats0.cache_key_matched);
    VR_CHECK(cache_epoch0 > 0U);
    VR_CHECK(stats0.emitted_primitive_count == 1U);

    const auto stats1 = RuntimeSystem2D::Build(&component, 1U, scratch, {});
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::hit_reused);
    VR_CHECK(stats1.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::none);
    VR_CHECK(stats1.cache_reused);
    VR_CHECK(stats1.cache_valid_before_build);
    VR_CHECK(stats1.cache_key_matched);
    VR_CHECK(stats1.cache_epoch == cache_epoch0);
    VR_CHECK(stats1.emitted_primitive_count == stats0.emitted_primitive_count);
    VR_CHECK(stats1.emitted_batch_count == stats0.emitted_batch_count);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim2_accepts_external_signature_hint, "unit;core;ecs;geometry;runtime") {
    using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
    using PathSystem = vr::ecs::GeometryPathSystem;
    using RuntimeSystem2D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim2>;

    Geometry2D component{};
    PathSystem::Initialize(component);
    VR_REQUIRE(PathSystem::AppendMoveTo(component, 0.0F, 0.0F));
    VR_REQUIRE(PathSystem::AppendLineTo(component, 3.0F, 0.0F));

    vr::ecs::Geometry2DRuntimeScratch scratch{};
    const auto stats0 = RuntimeSystem2D::Build(&component, 1U, scratch, {});
    VR_CHECK(!stats0.signature_from_hint);
    VR_CHECK(stats0.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats0.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::cold_start);

    vr::ecs::Geometry2DRuntimeBuildHint hint{};
    hint.external_build_signature = stats0.build_signature;
    hint.use_external_build_signature = 1U;
    const auto stats1 = RuntimeSystem2D::Build(&component, 1U, scratch, {}, hint);
    VR_CHECK(stats1.signature_from_hint);
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::hit_reused);
    VR_CHECK(stats1.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::none);
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

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim2_linked_appearance_encodes_effective_blend,
             "unit;core;ecs;geometry;runtime") {
    using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
    using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
    using AppearanceRuntimeSystem2D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim2>;
    using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
    using PathSystem = vr::ecs::GeometryPathSystem;
    using GeometrySystem2D = vr::ecs::GeometrySystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim2>;

    Appearance2D appearance{};
    AppearanceSystem2D::Initialize(appearance);
    AppearanceSystem2D::SetTextureBaseId(appearance, 9U);
    AppearanceSystem2D::SetBindingLayoutId(appearance, 1U);
    AppearanceSystem2D::SetSamplerStateId(appearance, 2U);
    AppearanceSystem2D::SetBlendMode(appearance, vr::ecs::AppearanceBlendMode::opaque);
    AppearanceSystem2D::SetAlphaMode(appearance, vr::ecs::AppearanceAlphaMode::opaque);

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim2> appearance_scratch{};
    (void)AppearanceRuntimeSystem2D::Build(&appearance, 1U, appearance_scratch);

    std::array<Geometry2D, 2U> components{};
    for (auto& component : components) {
        PathSystem::Initialize(component);
        VR_REQUIRE(PathSystem::AppendMoveTo(component, 0.0F, 0.0F));
        VR_REQUIRE(PathSystem::AppendLineTo(component, 1.0F, 0.0F));
        GeometrySystem2D::SetRuntimeRoute(component, 2U, 7U, 1U);
    }

    (void)GeometrySystem2D::SetAppearanceRuntimeLink(components[1U],
                                                     appearance.runtime.gpu_record_handle,
                                                     appearance.runtime.sort_key,
                                                     appearance.runtime.pipeline_key,
                                                     appearance.runtime.resource_key);

    vr::ecs::Geometry2DRuntimeScratch scratch{};
    const auto stats = RuntimeSystem2D::Build(components.data(),
                                              static_cast<std::uint32_t>(components.size()),
                                              scratch,
                                              {});

    VR_REQUIRE(stats.emitted_batch_count == 2U);
    VR_REQUIRE(scratch.draw_batches.size() == 2U);
    std::uint32_t alpha_batch_count = 0U;
    std::uint32_t opaque_batch_count = 0U;
    for (const auto& batch : scratch.draw_batches) {
        const auto preset = vr::ecs::DecodeRuntimeBlendPresetBits(batch.params,
                                                                  vr::ecs::geometry2d_runtime_blend_shift);
        if (preset == vr::ecs::RuntimeBlendPreset::alpha) {
            ++alpha_batch_count;
        } else if (preset == vr::ecs::RuntimeBlendPreset::opaque) {
            ++opaque_batch_count;
        }
    }
    VR_CHECK(alpha_batch_count == 1U);
    VR_CHECK(opaque_batch_count == 1U);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_builds_instances_and_batches, "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    std::array<Geometry3D, 2U> components{};
    std::array<Appearance3D, 2U> appearances{};
    std::array<Transform3D, 2U> transforms{};

    for (std::uint32_t i = 0U; i < components.size(); ++i) {
        MeshSystem::Initialize(components[i]);
        MeshSystem::SetMeshRoute(components[i], 41U, 0U, 0U);
        GeometrySystem3D::SetVisualResourceId(components[i], 5U);
        GeometrySystem3D::SetBatchTag(components[i], 0U);
        GeometrySystem3D::SetDepthBin(components[i], 2U);
        AppearanceSystem3D::Initialize(appearances[i]);
        AppearanceSystem3D::SetDepthTest(appearances[i], true);
        AppearanceSystem3D::SetDepthWrite(appearances[i], (i == 0U));
        AppearanceSystem3D::SetCastShadow(appearances[i], true);
        (void)GeometrySystem3D::ApplyAppearanceRuntimeState(components[i], appearances[i].style);

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
    VR_CHECK(stats.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::cold_start);
    VR_CHECK(!stats.cache_reused);
    VR_CHECK(!stats.transform_only_update);
    VR_CHECK(!stats.cache_valid_before_build);
    VR_CHECK(!stats.cache_key_matched);
    VR_CHECK(stats.cache_epoch > 0U);

    const vr::ecs::Geometry3DGpuInstance& instance0 = scratch.instances[0U];
    const vr::ecs::Geometry3DGpuInstance& instance1 = scratch.instances[1U];
    VR_REQUIRE(!scratch.draw_batches.empty());
    VR_CHECK(instance0.geometry_id == 41U);
    VR_CHECK(instance1.geometry_id == 41U);
    VR_CHECK(instance0.visual_resource_id == 5U);
    VR_CHECK(scratch.draw_batches[0U].visual_resource_id == 5U);
    VR_CHECK(instance0.world_m32 == 2.0F);
    VR_CHECK(instance1.world_m30 == 1.0F);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_updates_transform_without_rebuild, "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Geometry3D component{};
    MeshSystem::Initialize(component);
    MeshSystem::SetMeshRoute(component, 88U, 0U, 0U);
    GeometrySystem3D::SetVisualResourceId(component, 9U);

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform,
                                        vr::ecs::Float3{
                                            .x = 0.0F,
                                            .y = 0.0F,
                                            .z = 2.0F
                                        });
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    const auto stats0 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {});
    const std::uint32_t cache_epoch0 = stats0.cache_epoch;
    VR_CHECK(stats0.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats0.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::cold_start);
    VR_CHECK(!stats0.cache_reused);
    VR_CHECK(!stats0.transform_only_update);
    VR_CHECK(!stats0.cache_valid_before_build);
    VR_CHECK(!stats0.cache_key_matched);
    VR_CHECK(cache_epoch0 > 0U);
    VR_CHECK(stats0.transform_rewritten_instance_count == 1U);
    VR_CHECK(scratch.instances.size() == 1U);
    const float z0 = scratch.instances[0U].world_m32;

    TransformSystem3D::SetLocalPosition(transform,
                                        vr::ecs::Float3{
                                            .x = 1.0F,
                                            .y = 2.0F,
                                            .z = 3.0F
                                        });
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    const auto stats1 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {});
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::hit_partial_update);
    VR_CHECK(stats1.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::none);
    VR_CHECK(stats1.cache_reused);
    VR_CHECK(stats1.transform_only_update);
    VR_CHECK(stats1.cache_valid_before_build);
    VR_CHECK(stats1.cache_key_matched);
    VR_CHECK(stats1.cache_epoch == cache_epoch0);
    VR_CHECK(stats1.transform_rewritten_instance_count == 1U);
    VR_CHECK(stats1.emitted_batch_count == stats0.emitted_batch_count);
    VR_CHECK(stats1.emitted_instance_count == stats0.emitted_instance_count);
    VR_CHECK(scratch.instances[0U].world_m30 == 1.0F);
    VR_CHECK(scratch.instances[0U].world_m31 == 2.0F);
    VR_CHECK(scratch.instances[0U].world_m32 == 3.0F);
    VR_CHECK(scratch.instances[0U].world_m32 != z0);

    const auto stats2 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {});
    VR_CHECK(stats2.cache_status == vr::ecs::GeometryRuntimeCacheStatus::hit_reused);
    VR_CHECK(stats2.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::none);
    VR_CHECK(stats2.cache_reused);
    VR_CHECK(!stats2.transform_only_update);
    VR_CHECK(stats2.cache_valid_before_build);
    VR_CHECK(stats2.cache_key_matched);
    VR_CHECK(stats2.cache_epoch == cache_epoch0);
    VR_CHECK(stats2.transform_rewritten_instance_count == 0U);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_unlinked_appearance_bridge_drives_fallback_appearance_state,
             "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Geometry3D component{};
    MeshSystem::Initialize(component);
    MeshSystem::SetMeshRoute(component, 91U, 0U, 0U);
    GeometrySystem3D::SetVisualResourceId(component, 13U);

    Appearance3D appearance{};
    AppearanceSystem3D::Initialize(appearance);
    AppearanceSystem3D::SetBaseColor(appearance, vr::ecs::Rgba8{120U, 160U, 200U, 180U});
    AppearanceSystem3D::SetOpacity(appearance, 0.5F);
    AppearanceSystem3D::SetMetallic(appearance, 0.35F);
    AppearanceSystem3D::SetRoughness(appearance, 0.65F);
    AppearanceSystem3D::SetNormalScale(appearance, 2.25F);
    AppearanceSystem3D::SetOcclusionStrength(appearance, 0.45F);
    AppearanceSystem3D::SetShadingModel(appearance, vr::ecs::AppearanceShadingModel3D::unlit);
    AppearanceSystem3D::SetDepthWrite(appearance, true);
    AppearanceSystem3D::SetBlendMode(appearance, vr::ecs::AppearanceBlendMode::alpha);
    AppearanceSystem3D::SetAlphaMode(appearance, vr::ecs::AppearanceAlphaMode::blend);
    (void)GeometrySystem3D::ApplyAppearanceRuntimeState(component, appearance.style);

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform,
                                        vr::ecs::Float3{
                                            .x = 0.0F,
                                            .y = 0.0F,
                                            .z = 2.5F
                                        });
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    const auto stats = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {});
    VR_REQUIRE(stats.emitted_instance_count == 1U);
    VR_REQUIRE(scratch.instances.size() == 1U);

    const auto& instance = scratch.instances[0U];
    VR_CHECK(instance.visual_resource_id == 13U);
    VR_CHECK(instance.appearance_record_index == vr::ecs::invalid_appearance_index);
    VR_CHECK(instance.component_index == 0U);
    VR_CHECK((instance.params & 0x2U) == 0U);
    VR_CHECK((instance.params & (0x1U << 7U)) == 0U);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_linked_appearance_uses_effective_visual_resource_id,
             "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Geometry3D component{};
    MeshSystem::Initialize(component);
    MeshSystem::SetMeshRoute(component, 77U, 0U, 0U);
    GeometrySystem3D::SetVisualResourceId(component, 11U);
    (void)GeometrySystem3D::SetAppearanceRuntimeLink(component,
                                                     vr::ecs::AppearanceHandle{.index = 4U, .generation = 1U},
                                                     0ULL,
                                                     0ULL,
                                                     901ULL);

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 1.0F});
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    const auto stats = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {});
    VR_REQUIRE(stats.emitted_instance_count == 1U);
    VR_REQUIRE(!scratch.instances.empty());
    VR_REQUIRE(!scratch.draw_batches.empty());
    VR_CHECK(component.runtime.route.visual_resource_id == 11U);
    VR_CHECK(scratch.instances[0U].visual_resource_id == 901U);
    VR_CHECK(scratch.draw_batches[0U].visual_resource_id == 901U);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_linked_appearance_ignores_fallback_appearance_style_for_batching,
             "unit;core;ecs;geometry;runtime") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using AppearanceRuntimeSystem3D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Appearance3D appearance{};
    AppearanceSystem3D::Initialize(appearance);
    AppearanceSystem3D::SetTextureBaseColorId(appearance, 31U);
    AppearanceSystem3D::SetBindingLayoutId(appearance, 4U);
    AppearanceSystem3D::SetSamplerStateId(appearance, 2U);
    AppearanceSystem3D::SetBlendMode(appearance, vr::ecs::AppearanceBlendMode::opaque);
    AppearanceSystem3D::SetAlphaMode(appearance, vr::ecs::AppearanceAlphaMode::opaque);

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim3> appearance_scratch{};
    (void)AppearanceRuntimeSystem3D::Build(&appearance, 1U, appearance_scratch);

    std::array<Geometry3D, 2U> components{};
    std::array<Transform3D, 2U> transforms{};
    for (std::uint32_t i = 0U; i < components.size(); ++i) {
        MeshSystem::Initialize(components[i]);
        MeshSystem::SetMeshRoute(components[i], 211U, 0U, 0U);
        (void)GeometrySystem3D::SetAppearanceRuntimeLink(components[i],
                                                         appearance.runtime.gpu_record_handle,
                                                         appearance.runtime.sort_key,
                                                         appearance.runtime.pipeline_key,
                                                         444ULL);

        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i),
                                                .y = 0.0F,
                                                .z = 2.0F
                                            });
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    const auto stats = RuntimeSystem3D::Build(components.data(),
                                              transforms.data(),
                                              static_cast<std::uint32_t>(components.size()),
                                              scratch,
                                              {});

    VR_REQUIRE(stats.emitted_instance_count == 2U);
    VR_CHECK(stats.emitted_batch_count == 1U);
    VR_REQUIRE(scratch.instances.size() == 2U);
    VR_REQUIRE(scratch.draw_batches.size() == 1U);

    for (const auto& instance : scratch.instances) {
        VR_CHECK(instance.visual_resource_id == 444U);
        VR_CHECK(instance.appearance_record_index == vr::ecs::invalid_appearance_index);
        VR_CHECK((instance.params & (0x1U << 7U)) != 0U);
    }
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_geometry_style_changes_still_invalidate_cache_under_linked_appearance,
             "unit;core;ecs;geometry;runtime") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using AppearanceRuntimeSystem3D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Appearance3D appearance{};
    AppearanceSystem3D::Initialize(appearance);
    AppearanceSystem3D::SetTextureBaseColorId(appearance, 41U);
    AppearanceSystem3D::SetBindingLayoutId(appearance, 5U);
    AppearanceSystem3D::SetSamplerStateId(appearance, 3U);
    AppearanceSystem3D::SetBlendMode(appearance, vr::ecs::AppearanceBlendMode::opaque);
    AppearanceSystem3D::SetAlphaMode(appearance, vr::ecs::AppearanceAlphaMode::opaque);

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim3> appearance_scratch{};
    (void)AppearanceRuntimeSystem3D::Build(&appearance, 1U, appearance_scratch);

    Geometry3D component{};
    MeshSystem::Initialize(component);
    MeshSystem::SetMeshRoute(component, 305U, 0U, 0U);
    (void)GeometrySystem3D::SetAppearanceRuntimeLink(component,
                                                     appearance.runtime.gpu_record_handle,
                                                     appearance.runtime.sort_key,
                                                     appearance.runtime.pipeline_key,
                                                     512ULL);

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 2.0F});
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    const auto stats0 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {});
    const std::uint32_t epoch0 = stats0.cache_epoch;
    const std::uint64_t signature0 = stats0.geometry_signature;
    VR_REQUIRE(stats0.emitted_instance_count == 1U);
    VR_CHECK(stats0.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(scratch.instances[0U].appearance_record_index == vr::ecs::invalid_appearance_index);
    VR_CHECK((scratch.instances[0U].params & (0x1U << 7U)) != 0U);

    MeshSystem::SetLineWidth(component, 3.5F);

    const auto stats1 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {});
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats1.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::geometry_signature_changed);
    VR_CHECK(!stats1.cache_reused);
    VR_CHECK(!stats1.cache_key_matched);
    VR_CHECK(stats1.cache_epoch != epoch0);
    VR_CHECK(stats1.geometry_signature != signature0);
    VR_CHECK(scratch.instances[0U].appearance_record_index == vr::ecs::invalid_appearance_index);
    VR_CHECK((scratch.instances[0U].params & (0x1U << 7U)) != 0U);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_transform_dirty_hint_updates_single_instance,
             "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    std::array<Geometry3D, 3U> components{};
    std::array<Transform3D, 3U> transforms{};
    for (std::uint32_t i = 0U; i < components.size(); ++i) {
        MeshSystem::Initialize(components[i]);
        MeshSystem::SetMeshRoute(components[i], 201U + i, 0U, 0U);
        GeometrySystem3D::SetVisualResourceId(components[i], 17U);

        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i),
                                                .y = 0.0F,
                                                .z = 2.0F
                                            });
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    const auto stats0 = RuntimeSystem3D::Build(components.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(components.size()),
                                               scratch,
                                               {});
    VR_REQUIRE(stats0.emitted_instance_count == 3U);
    VR_CHECK(!stats0.geometry_signature_from_hint);
    VR_CHECK(!stats0.transform_signature_from_hint);

    TransformSystem3D::SetLocalPosition(transforms[2U],
                                        vr::ecs::Float3{
                                            .x = 42.0F,
                                            .y = 0.0F,
                                            .z = 5.0F
                                        });
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    const std::uint32_t dirty_index = 2U;
    vr::ecs::Geometry3DRuntimeBuildHint hint{};
    hint.external_geometry_signature = stats0.geometry_signature;
    hint.external_transform_signature = stats0.transform_signature + 1U;
    hint.transform_dirty_component_indices = &dirty_index;
    hint.transform_dirty_component_count = 1U;
    hint.use_external_geometry_signature = 1U;
    hint.use_external_transform_signature = 1U;

    const auto stats1 = RuntimeSystem3D::Build(components.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(components.size()),
                                               scratch,
                                               {},
                                               hint);
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::hit_partial_update);
    VR_CHECK(stats1.cache_reused);
    VR_CHECK(stats1.transform_only_update);
    VR_CHECK(stats1.geometry_signature_from_hint);
    VR_CHECK(stats1.transform_signature_from_hint);
    VR_CHECK(stats1.transform_update_from_dirty_hint);
    VR_CHECK(stats1.transform_rewritten_instance_count == 1U);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_vertex_deform_partial_update_rewrites_instance_payload,
             "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Geometry3D component{};
    MeshSystem::Initialize(component);
    MeshSystem::SetMeshRoute(component, 93U, 0U, 0U);
    MeshSystem::EnableVertexDeformShader(component, true);
    GeometrySystem3D::SetVisualResourceId(component, 5U);

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 2.0F});
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    std::array<vr::ecs::VertexDeformOutputState, 1U> outputs{};
    std::array<vr::ecs::Float4, 2U> parameters{{
        {.x = 1.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F},
        {.x = 0.25F, .y = 2.0F, .z = 0.5F, .w = 0.1F},
    }};
    outputs[0U].parameters = parameters.data();
    outputs[0U].parameter_count = static_cast<std::uint32_t>(parameters.size());
    outputs[0U].sampled_parameter_count = static_cast<std::uint32_t>(parameters.size());

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    vr::ecs::Geometry3DRuntimeBuildHint hint{};
    hint.vertex_deform_outputs = outputs.data();
    hint.vertex_deform_output_count = static_cast<std::uint32_t>(outputs.size());

    const auto stats0 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {}, hint);
    VR_REQUIRE(stats0.emitted_instance_count == 1U);
    VR_CHECK(stats0.vertex_deform_animated_instance_count == 1U);
    VR_CHECK(stats0.vertex_deform_rewritten_instance_count == 1U);
    VR_CHECK(scratch.instances[0U].deform_param0_x == 1.0F);
    VR_CHECK(scratch.instances[0U].deform_param1_y == 2.0F);

    parameters[0U].x = 3.0F;
    parameters[1U].w = 0.4F;

    const auto stats1 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {}, hint);
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::hit_partial_update);
    VR_CHECK(stats1.cache_reused);
    VR_CHECK(!stats1.transform_only_update);
    VR_CHECK(stats1.vertex_deform_rewritten_instance_count == 1U);
    VR_CHECK(scratch.instances[0U].deform_param0_x == 3.0F);
    VR_CHECK(scratch.instances[0U].deform_param1_w == 0.4F);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_morph_partial_update_rewrites_instance_weights,
             "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Geometry3D component{};
    MeshSystem::Initialize(component);
    MeshSystem::SetMeshRoute(component, 95U, 0U, 0U);
    MeshSystem::EnableMorphTargets(component, true);
    GeometrySystem3D::SetVisualResourceId(component, 8U);

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 2.0F});
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    std::array<vr::ecs::MorphWeightOutputState, 1U> outputs{};
    std::array<float, 2U> weights{0.25F, 0.5F};
    outputs[0U].weights = weights.data();
    outputs[0U].weight_count = static_cast<std::uint32_t>(weights.size());
    outputs[0U].sampled_weight_count = static_cast<std::uint32_t>(weights.size());
    outputs[0U].revision = 1U;

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    vr::ecs::Geometry3DRuntimeBuildHint hint{};
    hint.morph_outputs = outputs.data();
    hint.morph_output_count = static_cast<std::uint32_t>(outputs.size());

    const auto stats0 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {}, hint);
    VR_REQUIRE(stats0.emitted_instance_count == 1U);
    VR_CHECK(stats0.morph_animated_instance_count == 1U);
    VR_CHECK(stats0.morph_rewritten_instance_count == 1U);
    VR_CHECK(scratch.instances[0U].morph_weight0 == 0.25F);
    VR_CHECK(scratch.instances[0U].morph_weight1 == 0.5F);

    weights[0U] = 0.75F;
    weights[1U] = 0.1F;
    outputs[0U].revision = 2U;

    const auto stats1 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {}, hint);
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::hit_partial_update);
    VR_CHECK(stats1.cache_reused);
    VR_CHECK(stats1.morph_rewritten_instance_count == 1U);
    VR_CHECK(!stats1.transform_only_update);
    VR_CHECK(scratch.instances[0U].morph_weight0 == 0.75F);
    VR_CHECK(scratch.instances[0U].morph_weight1 == 0.1F);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_skeletal_root_motion_updates_world_matrix,
             "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Geometry3D component{};
    MeshSystem::Initialize(component);
    MeshSystem::SetMeshRoute(component, 97U, 0U, 0U);
    MeshSystem::EnableSkeletalRootMotion(component, true);
    GeometrySystem3D::SetVisualResourceId(component, 6U);

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform, vr::ecs::Float3{.x = 10.0F, .y = 0.0F, .z = 2.0F});
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    std::array<vr::ecs::SkeletalPoseOutputState<vr::ecs::Dim3>, 1U> outputs{};
    std::array<vr::ecs::SkeletalJointPose<vr::ecs::Dim3>, 1U> joints{{
        {
            .position = vr::ecs::Float3{.x = 1.5F, .y = 0.0F, .z = 0.0F},
            .rotation = vr::ecs::Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .scale = vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
        },
    }};
    outputs[0U].joints = joints.data();
    outputs[0U].joint_count = static_cast<std::uint32_t>(joints.size());
    outputs[0U].sampled_joint_count = static_cast<std::uint32_t>(joints.size());
    outputs[0U].revision = 1U;

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    vr::ecs::Geometry3DRuntimeBuildHint hint{};
    hint.skeletal_outputs = outputs.data();
    hint.skeletal_output_count = static_cast<std::uint32_t>(outputs.size());

    const auto stats0 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {}, hint);
    VR_REQUIRE(stats0.emitted_instance_count == 1U);
    VR_CHECK(stats0.skeletal_animated_instance_count == 1U);
    VR_CHECK(stats0.skeletal_rewritten_instance_count == 1U);
    VR_CHECK(scratch.instances[0U].world_m30 == 11.5F);

    joints[0U].position.x = 4.0F;
    outputs[0U].revision = 2U;

    const auto stats1 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {}, hint);
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::hit_partial_update);
    VR_CHECK(stats1.cache_reused);
    VR_CHECK(stats1.skeletal_rewritten_instance_count == 1U);
    VR_CHECK(stats1.transform_only_update);
    VR_CHECK(scratch.instances[0U].world_m30 == 14.0F);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_frame_sequence_signature_rebuilds_submesh_batches,
             "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Geometry3D component{};
    MeshSystem::Initialize(component);
    MeshSystem::SetMeshRoute(component, 109U, 2U, 0U);
    MeshSystem::EnableFrameSequenceSubmeshAnimation(component, true);
    GeometrySystem3D::SetVisualResourceId(component, 9U);

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform, vr::ecs::Float3{.x = 1.0F, .y = 0.0F, .z = 3.0F});
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    std::array<vr::ecs::FrameSequenceOutputState, 1U> outputs{};
    outputs[0U].frame_index_a = 1U;
    outputs[0U].frame_index_b = 2U;
    outputs[0U].frame_count = 8U;
    outputs[0U].blend_alpha = 0.25F;

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    vr::ecs::Geometry3DRuntimeBuildHint hint{};
    hint.frame_sequence_outputs = outputs.data();
    hint.frame_sequence_output_count = static_cast<std::uint32_t>(outputs.size());

    const auto stats0 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {}, hint);
    VR_REQUIRE(stats0.emitted_instance_count == 1U);
    VR_CHECK(stats0.frame_sequence_animated_instance_count == 1U);
    VR_CHECK(scratch.instances[0U].submesh_index == 3U);
    VR_REQUIRE(!scratch.draw_batches.empty());
    VR_CHECK(scratch.draw_batches[0U].submesh_index == 3U);

    outputs[0U].frame_index_a = 4U;
    outputs[0U].frame_index_b = 5U;
    outputs[0U].blend_alpha = 0.75F;

    const auto stats1 = RuntimeSystem3D::Build(&component, &transform, 1U, scratch, {}, hint);
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats1.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::animation_signature_changed);
    VR_CHECK(scratch.instances[0U].submesh_index == 6U);
    VR_CHECK(scratch.draw_batches[0U].submesh_index == 6U);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_reports_cache_miss_reasons_and_epoch_progress,
             "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Geometry3D component{};
    MeshSystem::Initialize(component);
    MeshSystem::SetMeshRoute(component, 120U, 0U, 0U);
    GeometrySystem3D::SetVisualResourceId(component, 12U);

    Transform3D transform_a{};
    TransformSystem3D::Initialize(transform_a);
    TransformSystem3D::SetLocalPosition(transform_a,
                                        vr::ecs::Float3{
                                            .x = 0.0F,
                                            .y = 0.0F,
                                            .z = 2.0F
                                        });
    TransformSystem3D::UpdateHierarchy(&transform_a, 1U);

    Transform3D transform_b = transform_a;

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    vr::ecs::Geometry3DRuntimeBuildConfig config{};
    const auto stats0 = RuntimeSystem3D::Build(&component, &transform_a, 1U, scratch, config);
    const std::uint32_t epoch0 = stats0.cache_epoch;
    VR_CHECK(stats0.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats0.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::cold_start);

    vr::ecs::Geometry3DRuntimeBuildConfig config_changed{};
    config_changed.build_ordered_indices = false;
    const auto stats1 = RuntimeSystem3D::Build(&component, &transform_a, 1U, scratch, config_changed);
    const std::uint32_t epoch1 = stats1.cache_epoch;
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats1.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::build_config_changed);
    VR_CHECK(!stats1.cache_key_matched);
    VR_CHECK(epoch1 > epoch0);

    const auto stats2 = RuntimeSystem3D::Build(&component, &transform_b, 1U, scratch, config_changed);
    const std::uint32_t epoch2 = stats2.cache_epoch;
    VR_CHECK(stats2.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats2.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::transforms_pointer_changed);
    VR_CHECK(!stats2.cache_key_matched);
    VR_CHECK(epoch2 > epoch1);

    GeometrySystem3D::SetVisualResourceId(component, 99U);
    const auto stats3 = RuntimeSystem3D::Build(&component, &transform_b, 1U, scratch, config_changed);
    VR_CHECK(stats3.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats3.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::geometry_signature_changed);
    VR_CHECK(!stats3.cache_key_matched);
    VR_CHECK(stats3.cache_epoch > epoch2);
}

VR_TEST_CASE(EcsGeometryRuntimeSystem_dim3_candidate_visibility_hint_controls_build_scope,
             "unit;core;ecs;geometry;runtime") {
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using MeshSystem = vr::ecs::GeometryMeshSystem;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    std::array<Geometry3D, 3U> components{};
    std::array<Transform3D, 3U> transforms{};

    for (std::uint32_t i = 0U; i < components.size(); ++i) {
        MeshSystem::Initialize(components[i]);
        MeshSystem::SetMeshRoute(components[i], 77U, 0U, 0U);
        GeometrySystem3D::SetVisualResourceId(components[i], 5U);

        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 2.0F});
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Geometry3DRuntimeScratch scratch{};

    const std::uint32_t visible_indices_a[2U] = {0U, 2U};
    vr::ecs::Geometry3DRuntimeBuildHint hint_a{};
    hint_a.visible_component_indices = visible_indices_a;
    hint_a.visible_component_count = 2U;
    hint_a.use_visible_component_indices = 1U;

    const auto stats0 = RuntimeSystem3D::Build(components.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(components.size()),
                                               scratch,
                                               {},
                                               hint_a);
    const std::uint32_t epoch0 = stats0.cache_epoch;
    VR_CHECK(stats0.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats0.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::cold_start);
    VR_CHECK(stats0.batch.scanned_count == 2U);
    VR_CHECK(stats0.batch.used_candidate_indices == 1U);
    VR_CHECK(stats0.candidate_component_count == 2U);
    VR_CHECK(stats0.used_visible_component_indices);
    VR_CHECK(stats0.emitted_instance_count == 2U);

    // Move component #1 (not in current visible candidates), runtime should fully reuse cache.
    TransformSystem3D::SetLocalPosition(transforms[1U],
                                        vr::ecs::Float3{.x = 9.0F, .y = 1.0F, .z = 2.0F});
    TransformSystem3D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));
    hint_a.external_visible_set_signature = stats0.visible_set_signature;
    hint_a.use_external_visible_set_signature = 1U;

    const auto stats1 = RuntimeSystem3D::Build(components.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(components.size()),
                                               scratch,
                                               {},
                                               hint_a);
    VR_CHECK(stats1.cache_status == vr::ecs::GeometryRuntimeCacheStatus::hit_reused);
    VR_CHECK(stats1.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::none);
    VR_CHECK(stats1.cache_reused);
    VR_CHECK(!stats1.transform_only_update);
    VR_CHECK(stats1.transform_rewritten_instance_count == 0U);
    VR_CHECK(stats1.cache_epoch == epoch0);

    // Switch candidate set (same count), should trigger visibility-signature miss.
    const std::uint32_t visible_indices_b[2U] = {1U, 2U};
    vr::ecs::Geometry3DRuntimeBuildHint hint_b{};
    hint_b.visible_component_indices = visible_indices_b;
    hint_b.visible_component_count = 2U;
    hint_b.use_visible_component_indices = 1U;
    hint_b.external_visible_set_signature = stats0.visible_set_signature + 1U;
    hint_b.use_external_visible_set_signature = 1U;

    const auto stats2 = RuntimeSystem3D::Build(components.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(components.size()),
                                               scratch,
                                               {},
                                               hint_b);
    VR_CHECK(stats2.cache_status == vr::ecs::GeometryRuntimeCacheStatus::miss);
    VR_CHECK(stats2.cache_miss_reason == vr::ecs::GeometryRuntimeCacheMissReason::visibility_signature_changed);
    VR_CHECK(stats2.cache_epoch > epoch0);
    VR_CHECK(stats2.batch.scanned_count == 2U);
    VR_CHECK(stats2.emitted_instance_count == 2U);
    VR_CHECK(stats2.visible_set_signature_from_hint);
}

} // namespace

