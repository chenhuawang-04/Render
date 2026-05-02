#include "support/test_framework.hpp"
#include "vr/ecs/system/appearance_runtime_system.hpp"

#include <cstdint>

namespace {

template<typename T>
using AppearanceTestMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

VR_TEST_CASE(EcsAppearanceRuntimeSystem_dim2_build_keys_and_records, "unit;core;ecs;appearance;runtime") {
    using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
    using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim2>;

    AppearanceTestMcVector<Appearance2D> components{};
    components.resize(3U);
    for (std::uint32_t i = 0U; i < 3U; ++i) {
        AppearanceSystem2D::Initialize(components[i]);
        AppearanceSystem2D::SetLayer(components[i], static_cast<std::int16_t>(i * 3));
        AppearanceSystem2D::SetTextureBaseId(components[i], 100U + i);
        AppearanceSystem2D::SetBindingLayoutId(components[i], 2U);
        AppearanceSystem2D::SetSamplerStateId(components[i], 7U);
    }

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim2> scratch{};
    vr::ecs::AppearancePipelinePolicy pipeline_policy{};
    pipeline_policy.pipeline_domain_id = 1U;
    pipeline_policy.pass_id = 3U;
    pipeline_policy.queue_id = 2U;

    vr::ecs::AppearanceSortPolicy sort_policy{};
    sort_policy.queue_bucket = 5U;
    sort_policy.default_depth_bucket = 42U;
    sort_policy.tie_breaker_seed = 11U;
    sort_policy.pipeline_bucket_override = 0xFFFFU;

    const vr::ecs::AppearanceRuntimeBuildStats stats = RuntimeSystem2D::Build(components.data(),
                                                                               3U,
                                                                               scratch,
                                                                               pipeline_policy,
                                                                               sort_policy);
    VR_CHECK(stats.full_rebuild == 1U);
    VR_CHECK(stats.updated_record_count == 3U);
    VR_CHECK(stats.upload_range_count == 1U);
    VR_CHECK(RuntimeSystem2D::GpuRecordCount(scratch) == 3U);

    for (std::uint32_t i = 0U; i < 3U; ++i) {
        const Appearance2D& component = components[i];
        VR_CHECK(component.runtime.pipeline_key != 0U);
        VR_CHECK(component.runtime.resource_key != 0U);
        VR_CHECK(component.runtime.sort_key != 0U);
        VR_CHECK(component.runtime.gpu_record_handle.index == i);
        VR_CHECK(component.runtime.gpu_record_handle.generation >= 1U);
        VR_CHECK(RuntimeSystem2D::ExtractSortQueue(component.runtime.sort_key) == 5U);
        VR_CHECK(RuntimeSystem2D::ExtractSortDepth(component.runtime.sort_key) == 42U);
    }

    VR_CHECK(components[0U].runtime.resource_key != components[1U].runtime.resource_key);
}

VR_TEST_CASE(EcsAppearanceRuntimeSystem_dim3_incremental_dirty_update, "unit;core;ecs;appearance;runtime") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim3>;

    AppearanceTestMcVector<Appearance3D> components{};
    components.resize(4U);
    for (std::uint32_t i = 0U; i < 4U; ++i) {
        AppearanceSystem3D::Initialize(components[i]);
        AppearanceSystem3D::SetTextureBaseColorId(components[i], 10U + i);
        AppearanceSystem3D::SetTextureNormalId(components[i], 20U + i);
        AppearanceSystem3D::SetTextureMetalRoughId(components[i], 30U + i);
        AppearanceSystem3D::SetTextureOcclusionId(components[i], 40U + i);
        AppearanceSystem3D::SetTextureEmissiveId(components[i], 50U + i);
        AppearanceSystem3D::SetBindingLayoutId(components[i], 4U);
        AppearanceSystem3D::SetSamplerStateId(components[i], 5U);
    }

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim3> scratch{};
    (void)RuntimeSystem3D::Build(components.data(), 4U, scratch);
    const std::uint64_t pipeline_key_before = components[1U].runtime.pipeline_key;

    AppearanceSystem3D::SetRoughness(components[1U], 0.3F);
    const std::uint32_t dirty_index = 1U;
    vr::ecs::AppearanceRuntimeBuildHint hint{};
    hint.use_dirty_component_indices = 1U;
    hint.dirty_component_indices = &dirty_index;
    hint.dirty_component_count = 1U;

    const vr::ecs::AppearanceRuntimeBuildStats stats = RuntimeSystem3D::Build(components.data(),
                                                                               4U,
                                                                               scratch,
                                                                               RuntimeSystem3D::DefaultPipelinePolicy(),
                                                                               RuntimeSystem3D::DefaultSortPolicy(),
                                                                               RuntimeSystem3D::DefaultBuildConfig(),
                                                                               hint);
    VR_CHECK(stats.full_rebuild == 0U);
    VR_CHECK(stats.used_dirty_indices == 1U);
    VR_CHECK(stats.updated_record_count == 1U);
    VR_CHECK(stats.upload_range_count == 1U);
    VR_CHECK(RuntimeSystem3D::UploadRanges(scratch)[0U].begin_index == 1U);
    VR_CHECK(RuntimeSystem3D::UploadRanges(scratch)[0U].count == 1U);
    VR_CHECK(!AppearanceSystem3D::HasDirtyFlags(components[1U],
                                                vr::ecs::appearance_dirty_style_flag |
                                                    vr::ecs::appearance_dirty_binding_flag));
    VR_CHECK(components[1U].runtime.pipeline_key == pipeline_key_before);
}

VR_TEST_CASE(EcsAppearanceRuntimeSystem_auto_sort_policy_uses_transparency_defaults,
             "unit;core;ecs;appearance;runtime") {
    using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
    using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim2>;

    Appearance2D component{};
    AppearanceSystem2D::Initialize(component);
    AppearanceSystem2D::SetTextureBaseId(component, 9U);
    AppearanceSystem2D::SetBindingLayoutId(component, 1U);
    AppearanceSystem2D::SetSamplerStateId(component, 2U);

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim2> scratch{};
    const vr::ecs::AppearanceRuntimeBuildStats stats =
        RuntimeSystem2D::Build(&component, 1U, scratch);

    VR_CHECK(stats.updated_key_count == 1U);
    VR_CHECK(RuntimeSystem2D::ExtractSortQueue(component.runtime.sort_key) == 2U);
    VR_CHECK(RuntimeSystem2D::ExtractSortPipelineBucket(component.runtime.sort_key) ==
             vr::ecs::FoldPipelineSortBucket(component.runtime.pipeline_key));
}

VR_TEST_CASE(EcsAppearanceRuntimeSystem_alpha_mode_blend_falls_back_to_alpha_runtime_blend,
             "unit;core;ecs;appearance;runtime") {
    using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
    using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim2>;

    Appearance2D component{};
    AppearanceSystem2D::Initialize(component);
    AppearanceSystem2D::SetTextureBaseId(component, 11U);
    AppearanceSystem2D::SetBindingLayoutId(component, 3U);
    AppearanceSystem2D::SetSamplerStateId(component, 5U);
    AppearanceSystem2D::SetBlendMode(component, vr::ecs::AppearanceBlendMode::opaque);
    AppearanceSystem2D::SetAlphaMode(component, vr::ecs::AppearanceAlphaMode::blend);

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim2> scratch{};
    const auto stats = RuntimeSystem2D::Build(&component, 1U, scratch);

    VR_CHECK(stats.updated_key_count == 1U);
    VR_CHECK(vr::ecs::ResolveRuntimeBlendPreset(
                 static_cast<std::uint32_t>(component.runtime.pipeline_key)) ==
             vr::ecs::RuntimeBlendPreset::alpha);
}

} // namespace
