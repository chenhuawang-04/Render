#include "support/test_framework.hpp"
#include "vr/ecs/system/appearance_runtime_system.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/surface_batch_system.hpp"
#include "vr/ecs/system/surface_runtime_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>
#include <cstdint>

namespace {

void ApplySurface2DAppearance(vr::ecs::Surface<vr::ecs::Dim2>& surface_,
                              std::int16_t layer_,
                              vr::ecs::AppearanceBlendMode blend_mode_ = vr::ecs::AppearanceBlendMode::alpha,
                              bool premultiplied_alpha_ = false,
                              vr::ecs::Rgba8 tint_color_ = vr::ecs::Rgba8{255U, 255U, 255U, 255U},
                              float opacity_ = 1.0F) {
    vr::ecs::Appearance<vr::ecs::Dim2> appearance{};
    vr::ecs::AppearanceSystem<vr::ecs::Dim2>::Initialize(appearance);
    vr::ecs::AppearanceSystem<vr::ecs::Dim2>::SetLayer(appearance, layer_);
    vr::ecs::AppearanceSystem<vr::ecs::Dim2>::SetBlendMode(appearance, blend_mode_);
    vr::ecs::AppearanceSystem<vr::ecs::Dim2>::SetPremultipliedAlpha(appearance, premultiplied_alpha_);
    vr::ecs::AppearanceSystem<vr::ecs::Dim2>::SetFillColor(appearance, tint_color_);
    vr::ecs::AppearanceSystem<vr::ecs::Dim2>::SetOpacity(appearance, opacity_);
    (void)vr::ecs::SurfaceSystem<vr::ecs::Dim2>::ApplyAppearanceRuntimeState(surface_, appearance.style);
}

using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;

VR_TEST_CASE(EcsSurfaceBatchSystem_dim2_build_sort_and_group, "unit;core;ecs;surface;batch") {
    using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
    using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;
    using BatchSystem2D = vr::ecs::SurfaceBatchSystem<vr::ecs::Dim2>;

    std::array<Surface2D, 6U> components{};
    for (auto& component : components) {
        SurfaceSystem2D::Initialize(component);
    }

    SurfaceSystem2D::SetSource(components[0U], vr::ecs::SurfaceImageSourceDesc{.surface_id = 100U, .atlas_page_id = 1U});
    SurfaceSystem2D::SetVisualResourceId(components[0U], 5U);
    SurfaceSystem2D::SetBatchTag(components[0U], 1U);
    ApplySurface2DAppearance(components[0U], 0);

    SurfaceSystem2D::SetSource(components[1U], vr::ecs::SurfaceImageSourceDesc{.surface_id = 99U, .atlas_page_id = 1U});
    SurfaceSystem2D::SetVisualResourceId(components[1U], 4U);
    SurfaceSystem2D::SetBatchTag(components[1U], 1U);
    ApplySurface2DAppearance(components[1U], 0);

    // 2: keep missing source to test filtering

    SurfaceSystem2D::SetSource(components[3U], vr::ecs::SurfaceSpriteSourceDesc{.surface_id = 200U, .atlas_page_id = 2U});
    SurfaceSystem2D::SetVisible(components[3U], false);

    SurfaceSystem2D::SetSource(components[4U], vr::ecs::SurfaceSpriteSourceDesc{.surface_id = 200U, .atlas_page_id = 2U});
    SurfaceSystem2D::SetVisualResourceId(components[4U], 4U);
    SurfaceSystem2D::SetBatchTag(components[4U], 0U);
    ApplySurface2DAppearance(components[4U], -1);

    SurfaceSystem2D::SetSource(components[5U], vr::ecs::SurfaceSpriteSourceDesc{.surface_id = 200U, .atlas_page_id = 2U});
    SurfaceSystem2D::SetVisualResourceId(components[5U], 4U);
    SurfaceSystem2D::SetBatchTag(components[5U], 0U);
    ApplySurface2DAppearance(components[5U], 2);

    vr::ecs::SurfaceBatchScratch<vr::ecs::Dim2> scratch{};
    const auto stats = BatchSystem2D::BuildAndSort(components.data(),
                                                    static_cast<std::uint32_t>(components.size()),
                                                    scratch,
                                                    true);

    VR_CHECK(stats.total_count == static_cast<std::uint32_t>(components.size()));
    VR_CHECK(stats.scanned_count == static_cast<std::uint32_t>(components.size()));
    VR_CHECK(stats.visible_count == 4U);
    VR_CHECK(stats.hidden_count == 1U);
    VR_CHECK(stats.missing_source_count == 1U);
    VR_CHECK(stats.out_of_range_candidate_count == 0U);
    VR_CHECK(stats.used_candidate_indices == 0U);

    VR_CHECK(BatchSystem2D::OrderedIndexCount(scratch) == 4U);
    const std::uint32_t* indices = BatchSystem2D::OrderedIndices(scratch);
    VR_REQUIRE(indices != nullptr);
    VR_CHECK(indices[0U] == 1U);
    VR_CHECK(indices[1U] == 4U);
    VR_CHECK(indices[2U] == 5U);
    VR_CHECK(indices[3U] == 0U);

    const vr::ecs::SurfaceBatchItem* items = BatchSystem2D::SortedItems(scratch);
    VR_REQUIRE(items != nullptr);
    for (std::uint32_t i = 1U; i < stats.visible_count; ++i) {
        VR_CHECK(items[i - 1U].sort_key <= items[i].sort_key);
    }

    std::array<std::uint32_t, 4U> binding_group_counts{};
    std::uint32_t binding_group_total = 0U;
    std::uint32_t binding_group_used = 0U;
    BatchSystem2D::ForEachBindingGroup(scratch,
                                       [&](std::uint32_t begin_,
                                           std::uint32_t count_,
                                           std::uint64_t binding_key_) {
                                           (void)begin_;
                                           (void)binding_key_;
                                           if (binding_group_used < binding_group_counts.size()) {
                                               binding_group_counts[binding_group_used] = count_;
                                           }
                                           ++binding_group_used;
                                           binding_group_total += count_;
                                       });

    VR_CHECK(binding_group_used == 3U);
    VR_CHECK(binding_group_counts[0U] == 1U);
    VR_CHECK(binding_group_counts[1U] == 2U);
    VR_CHECK(binding_group_counts[2U] == 1U);
    VR_CHECK(binding_group_total == stats.visible_count);
}

VR_TEST_CASE(EcsSurfaceBatchSystem_dim3_binding_key_ignores_depth_and_batch, "unit;core;ecs;surface;batch") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
    using BatchSystem3D = vr::ecs::SurfaceBatchSystem<vr::ecs::Dim3>;

    std::array<Surface3D, 3U> components{};
    for (auto& component : components) {
        SurfaceSystem3D::Initialize(component);
        SurfaceSystem3D::SetSource(component, vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 9U, .sampler_id = 5U, .uv_set = 0U, .flags = 0U});
        SurfaceSystem3D::SetVisualResourceId(component, 33U);
    }

    SurfaceSystem3D::SetDepthBin(components[0U], 2U);
    SurfaceSystem3D::SetBatchTag(components[0U], 9U);

    SurfaceSystem3D::SetDepthBin(components[1U], 0U);
    SurfaceSystem3D::SetBatchTag(components[1U], 1U);

    SurfaceSystem3D::SetDepthBin(components[2U], 1U);
    SurfaceSystem3D::SetBatchTag(components[2U], 3U);

    vr::ecs::SurfaceBatchScratch<vr::ecs::Dim3> scratch{};
    const auto stats = BatchSystem3D::BuildAndSort(components.data(),
                                                    static_cast<std::uint32_t>(components.size()),
                                                    scratch,
                                                    true);
    VR_CHECK(stats.visible_count == 3U);

    const std::uint32_t* indices = BatchSystem3D::OrderedIndices(scratch);
    VR_REQUIRE(indices != nullptr);
    VR_CHECK(indices[0U] == 1U);
    VR_CHECK(indices[1U] == 2U);
    VR_CHECK(indices[2U] == 0U);

    std::uint32_t binding_group_count = 0U;
    std::uint32_t grouped_items = 0U;
    BatchSystem3D::ForEachBindingGroup(scratch,
                                       [&](std::uint32_t begin_,
                                           std::uint32_t count_,
                                           std::uint64_t binding_key_) {
                                           (void)begin_;
                                           (void)binding_key_;
                                           ++binding_group_count;
                                           grouped_items += count_;
                                       });

    VR_CHECK(binding_group_count == 1U);
    VR_CHECK(grouped_items == 3U);
}

VR_TEST_CASE(EcsSurfaceBatchSystem_dim3_candidate_indices_filter_and_oob, "unit;core;ecs;surface;batch") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
    using BatchSystem3D = vr::ecs::SurfaceBatchSystem<vr::ecs::Dim3>;

    std::array<Surface3D, 5U> components{};
    for (auto& component : components) {
        SurfaceSystem3D::Initialize(component);
        SurfaceSystem3D::SetSource(component, vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 10U, .sampler_id = 2U, .uv_set = 0U, .flags = 0U});
        SurfaceSystem3D::SetVisualResourceId(component, 4U);
    }
    SurfaceSystem3D::SetVisible(components[1U], false);
    SurfaceSystem3D::SetSource(components[3U], vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 0U});

    const std::array<std::uint32_t, 6U> candidate_indices{
        4U, 1U, 99U, 3U, 0U, 2U
    };

    vr::ecs::SurfaceBatchScratch<vr::ecs::Dim3> scratch{};
    const auto stats = BatchSystem3D::BuildAndSortFromCandidates(components.data(),
                                                                  static_cast<std::uint32_t>(components.size()),
                                                                  candidate_indices.data(),
                                                                  static_cast<std::uint32_t>(candidate_indices.size()),
                                                                  scratch,
                                                                  true);

    VR_CHECK(stats.total_count == static_cast<std::uint32_t>(components.size()));
    VR_CHECK(stats.scanned_count == static_cast<std::uint32_t>(candidate_indices.size()));
    VR_CHECK(stats.visible_count == 3U);
    VR_CHECK(stats.hidden_count == 1U);
    VR_CHECK(stats.missing_source_count == 1U);
    VR_CHECK(stats.out_of_range_candidate_count == 1U);
    VR_CHECK(stats.used_candidate_indices == 1U);
    VR_CHECK(BatchSystem3D::OrderedIndexCount(scratch) == 3U);
}

VR_TEST_CASE(EcsSurfaceRuntimeSystem_dim2_build_and_transform_update, "unit;core;ecs;surface;runtime") {
    using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
    using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::SurfaceRuntimeSystem<vr::ecs::Dim2>;
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

    std::array<Surface2D, 2U> components{};
    std::array<Transform2D, 2U> transforms{};

    for (std::uint32_t i = 0U; i < components.size(); ++i) {
        SurfaceSystem2D::Initialize(components[i]);
        TransformSystem2D::Initialize(transforms[i]);
    }

    SurfaceSystem2D::SetSource(components[0U], vr::ecs::SurfaceImageSourceDesc{.surface_id = 41U, .atlas_page_id = 3U});
    SurfaceSystem2D::SetVisualResourceId(components[0U], 7U);
    SurfaceSystem2D::SetSize(components[0U], vr::ecs::Float2{.x = 120.0F, .y = 64.0F});
    SurfaceSystem2D::SetPivot(components[0U], vr::ecs::Float2{.x = 0.5F, .y = 0.5F});

    SurfaceSystem2D::SetSource(components[1U], vr::ecs::SurfaceSpriteSourceDesc{.surface_id = 61U, .atlas_page_id = 4U});
    SurfaceSystem2D::SetVisualResourceId(components[1U], 7U);
    SurfaceSystem2D::SetSize(components[1U], vr::ecs::Float2{.x = 90.0F, .y = 48.0F});
    SurfaceSystem2D::SetPivot(components[1U], vr::ecs::Float2{.x = 0.0F, .y = 1.0F});

    TransformSystem2D::SetLocalPosition(transforms[0U], 2.0F, 4.0F);
    TransformSystem2D::SetLocalRotationRadians(transforms[0U], 0.0F);
    TransformSystem2D::SetLocalPosition(transforms[1U], -3.0F, 1.0F);
    TransformSystem2D::SetLocalRotationRadians(transforms[1U], 0.1F);
    TransformSystem2D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Surface2DRuntimeScratch scratch{};
    const auto stats0 = RuntimeSystem2D::Build(components.data(),
                                                transforms.data(),
                                                static_cast<std::uint32_t>(components.size()),
                                                scratch,
                                                {});
    const std::uint32_t epoch0 = stats0.cache_epoch;
    VR_CHECK(stats0.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::miss);
    VR_CHECK(stats0.cache_miss_reason == vr::ecs::SurfaceRuntimeCacheMissReason::cold_start);
    VR_CHECK(stats0.emitted_instance_count == 2U);
    VR_CHECK(stats0.image_source_instance_count == 1U);
    VR_CHECK(stats0.sprite_source_instance_count == 1U);
    VR_CHECK(scratch.instances.size() == 2U);
    const float x_before = scratch.instances[1U].world_m02;

    TransformSystem2D::SetLocalPosition(transforms[1U], -7.0F, 2.5F);
    TransformSystem2D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    const auto stats1 = RuntimeSystem2D::Build(components.data(),
                                                transforms.data(),
                                                static_cast<std::uint32_t>(components.size()),
                                                scratch,
                                                {});
    VR_CHECK(stats1.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::hit_partial_update);
    VR_CHECK(stats1.cache_reused);
    VR_CHECK(stats1.transform_only_update);
    VR_CHECK(stats1.transform_rewritten_instance_count == 1U);
    VR_CHECK(stats1.cache_epoch == epoch0);
    VR_CHECK(scratch.instances[1U].world_m02 != x_before);

    const auto stats2 = RuntimeSystem2D::Build(components.data(),
                                                transforms.data(),
                                                static_cast<std::uint32_t>(components.size()),
                                                scratch,
                                                {});
    VR_CHECK(stats2.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::hit_reused);
    VR_CHECK(stats2.cache_reused);
    VR_CHECK(!stats2.transform_only_update);
    VR_CHECK(stats2.transform_rewritten_instance_count == 0U);
    VR_CHECK(stats2.cache_epoch == epoch0);
}

VR_TEST_CASE(EcsSurfaceRuntimeSystem_dim2_linked_appearance_overrides_effective_blend,
             "unit;core;ecs;surface;runtime") {
    using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
    using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
    using AppearanceRuntimeSystem2D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim2>;
    using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
    using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::SurfaceRuntimeSystem<vr::ecs::Dim2>;
    using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

    Appearance2D appearance{};
    AppearanceSystem2D::Initialize(appearance);
    AppearanceSystem2D::SetTextureBaseId(appearance, 21U);
    AppearanceSystem2D::SetBindingLayoutId(appearance, 2U);
    AppearanceSystem2D::SetSamplerStateId(appearance, 7U);
    AppearanceSystem2D::SetBlendMode(appearance, vr::ecs::AppearanceBlendMode::premultiplied);
    AppearanceSystem2D::SetAlphaMode(appearance, vr::ecs::AppearanceAlphaMode::blend);

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim2> appearance_scratch{};
    (void)AppearanceRuntimeSystem2D::Build(&appearance, 1U, appearance_scratch);

    Surface2D surface{};
    Transform2D transform{};
    SurfaceSystem2D::Initialize(surface);
    TransformSystem2D::Initialize(transform);
    SurfaceSystem2D::SetSource(surface, vr::ecs::SurfaceImageSourceDesc{.surface_id = 41U, .atlas_page_id = 3U});
    SurfaceSystem2D::SetVisualResourceId(surface, 7U);
    ApplySurface2DAppearance(surface,
                             0,
                             vr::ecs::AppearanceBlendMode::additive,
                             false);
    (void)SurfaceSystem2D::SetAppearanceRuntimeLink(surface,
                                                    appearance.runtime.gpu_record_handle,
                                                    appearance.runtime.sort_key,
                                                    appearance.runtime.pipeline_key,
                                                    appearance.runtime.resource_key);

    TransformSystem2D::UpdateHierarchy(&transform, 1U);

    vr::ecs::Surface2DRuntimeScratch scratch{};
    const auto stats = RuntimeSystem2D::Build(&surface, &transform, 1U, scratch, {});

    VR_REQUIRE(stats.emitted_instance_count == 1U);
    VR_REQUIRE(!scratch.instances.empty());
    const std::uint32_t params = scratch.instances[0U].params;
    VR_CHECK(vr::ecs::DecodeRuntimeBlendPresetBits(params, vr::ecs::surface2d_runtime_blend_shift) ==
             vr::ecs::RuntimeBlendPreset::premultiplied_alpha);
    VR_CHECK((params & 0x10U) != 0U);
    VR_CHECK((params & 0x3U) == 0U);
}

VR_TEST_CASE(EcsSurfaceRuntimeSystem_dim3_transform_dirty_hint_path, "unit;core;ecs;surface;runtime") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::SurfaceRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    std::array<Surface3D, 3U> components{};
    std::array<Appearance3D, 3U> appearances{};
    std::array<Transform3D, 3U> transforms{};
    for (std::uint32_t i = 0U; i < components.size(); ++i) {
        SurfaceSystem3D::Initialize(components[i]);
        SurfaceSystem3D::SetSource(components[i], vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 101U + (i % 2U), .sampler_id = 7U, .uv_set = 0U, .flags = 0U});
        SurfaceSystem3D::SetVisualResourceId(components[i], 22U);
        AppearanceSystem3D::Initialize(appearances[i]);

        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i),
                                                .y = 0.0F,
                                                .z = 2.0F});
    }
    AppearanceSystem3D::SetDepthWrite(appearances[1U], true);
    (void)SurfaceSystem3D::ApplyAppearanceRuntimeState(components[1U], appearances[1U].style);
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Surface3DRuntimeScratch scratch{};
    const auto stats0 = RuntimeSystem3D::Build(components.data(),
                                                transforms.data(),
                                                static_cast<std::uint32_t>(components.size()),
                                                scratch,
                                                {});
    VR_REQUIRE(stats0.emitted_instance_count == 3U);
    VR_CHECK(stats0.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::miss);
    VR_CHECK(stats0.depth_test_batch_count > 0U);
    VR_CHECK(stats0.depth_write_batch_count > 0U);

    TransformSystem3D::SetLocalPosition(transforms[2U],
                                        vr::ecs::Float3{
                                            .x = 5.0F,
                                            .y = -2.0F,
                                            .z = 3.5F});
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    const std::uint32_t dirty_index = 2U;
    vr::ecs::Surface3DRuntimeBuildHint hint{};
    hint.external_surface_signature = stats0.surface_signature;
    hint.external_transform_signature = stats0.transform_signature + 1U;
    hint.transform_dirty_component_indices = &dirty_index;
    hint.transform_dirty_component_count = 1U;
    hint.use_external_surface_signature = 1U;
    hint.use_external_transform_signature = 1U;

    const auto stats1 = RuntimeSystem3D::Build(components.data(),
                                                transforms.data(),
                                                static_cast<std::uint32_t>(components.size()),
                                                scratch,
                                                {},
                                                hint);
    VR_CHECK(stats1.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::hit_partial_update);
    VR_CHECK(stats1.cache_reused);
    VR_CHECK(stats1.transform_only_update);
    VR_CHECK(stats1.surface_signature_from_hint);
    VR_CHECK(stats1.transform_signature_from_hint);
    VR_CHECK(stats1.transform_update_from_dirty_hint);
    VR_CHECK(stats1.transform_rewritten_instance_count == 1U);
}

VR_TEST_CASE(EcsSurfaceRuntimeSystem_dim3_visibility_signature_drives_cache_key, "unit;core;ecs;surface;runtime") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::SurfaceRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    std::array<Surface3D, 4U> components{};
    std::array<Transform3D, 4U> transforms{};
    for (std::uint32_t i = 0U; i < components.size(); ++i) {
        SurfaceSystem3D::Initialize(components[i]);
        SurfaceSystem3D::SetSource(components[i], vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 200U + i, .sampler_id = 9U, .uv_set = 0U, .flags = 0U});
        SurfaceSystem3D::SetVisualResourceId(components[i], 3U);
        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i),
                                                .y = 0.0F,
                                                .z = -1.0F});
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Surface3DRuntimeScratch scratch{};
    const std::array<std::uint32_t, 2U> visible_a{0U, 1U};
    vr::ecs::Surface3DRuntimeBuildHint hint_a{};
    hint_a.visible_component_indices = visible_a.data();
    hint_a.visible_component_count = static_cast<std::uint32_t>(visible_a.size());
    hint_a.use_visible_component_indices = 1U;

    const auto stats0 = RuntimeSystem3D::Build(components.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(components.size()),
                                               scratch,
                                               {},
                                               hint_a);
    VR_CHECK(stats0.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::miss);
    VR_CHECK(stats0.cache_miss_reason == vr::ecs::SurfaceRuntimeCacheMissReason::cold_start);
    VR_CHECK(stats0.used_visible_component_indices);
    VR_CHECK(stats0.candidate_component_count == 2U);

    hint_a.external_surface_signature = stats0.surface_signature;
    hint_a.external_transform_signature = stats0.transform_signature;
    hint_a.external_visible_set_signature = stats0.visible_set_signature;
    hint_a.use_external_surface_signature = 1U;
    hint_a.use_external_transform_signature = 1U;
    hint_a.use_external_visible_set_signature = 1U;

    const auto stats1 = RuntimeSystem3D::Build(components.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(components.size()),
                                               scratch,
                                               {},
                                               hint_a);
    VR_CHECK(stats1.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::hit_reused);
    VR_CHECK(stats1.cache_reused);
    VR_CHECK(stats1.cache_key_matched);
    VR_CHECK(stats1.visible_set_signature_from_hint);

    const std::array<std::uint32_t, 2U> visible_b{1U, 2U};
    vr::ecs::Surface3DRuntimeBuildHint hint_b = hint_a;
    hint_b.visible_component_indices = visible_b.data();
    hint_b.visible_component_count = static_cast<std::uint32_t>(visible_b.size());
    hint_b.external_visible_set_signature = stats0.visible_set_signature + 1U;
    hint_b.use_external_visible_set_signature = 1U;

    const auto stats2 = RuntimeSystem3D::Build(components.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(components.size()),
                                               scratch,
                                               {},
                                               hint_b);
    VR_CHECK(stats2.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::miss);
    VR_CHECK(stats2.cache_miss_reason == vr::ecs::SurfaceRuntimeCacheMissReason::visibility_signature_changed);
    VR_CHECK(stats2.used_visible_component_indices);
    VR_CHECK(stats2.candidate_component_count == 2U);
    VR_CHECK(stats2.visible_set_signature_from_hint);
}

VR_TEST_CASE(EcsSurfaceRuntimeSystem_dim3_batching_ignores_texture_route_when_render_state_matches,
             "unit;core;ecs;surface;runtime") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::SurfaceRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    std::array<Surface3D, 2U> components{};
    std::array<Transform3D, 2U> transforms{};
    for (std::uint32_t i = 0U; i < components.size(); ++i) {
        SurfaceSystem3D::Initialize(components[i]);
        SurfaceSystem3D::SetSource(components[i], vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 6101U + i, .sampler_id = 7U + i, .uv_set = 0U, .flags = 0U});
        SurfaceSystem3D::SetVisualResourceId(components[i], 55U);
        (void)SurfaceSystem3D::SetAppearanceRuntimeLink(components[i],
                                                        vr::ecs::AppearanceHandle{.index = 9U, .generation = 1U},
                                                        101ULL,
                                                        202ULL,
                                                        777ULL);

        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i),
                                                .y = 0.0F,
                                                .z = 2.0F
                                            });
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    vr::ecs::Surface3DRuntimeScratch scratch{};
    const auto stats = RuntimeSystem3D::Build(components.data(),
                                              transforms.data(),
                                              static_cast<std::uint32_t>(components.size()),
                                              scratch,
                                              {});

    VR_REQUIRE(stats.emitted_instance_count == 2U);
    VR_CHECK(stats.emitted_batch_count == 1U);
    VR_REQUIRE(scratch.draw_batches.size() == 1U);
    VR_CHECK(scratch.draw_batches[0U].visual_resource_id == 777U);
    VR_CHECK(scratch.draw_batches[0U].instance_count == 2U);
}

VR_TEST_CASE(EcsSurfaceRuntimeSystem_dim3_linked_appearance_uses_effective_visual_resource_id,
             "unit;core;ecs;surface;runtime") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::SurfaceRuntimeSystem<vr::ecs::Dim3>;
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

    Surface3D surface{};
    SurfaceSystem3D::Initialize(surface);
    SurfaceSystem3D::SetSource(surface, vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 111U, .sampler_id = 3U, .uv_set = 0U, .flags = 0U});
    SurfaceSystem3D::SetVisualResourceId(surface, 19U);
    (void)SurfaceSystem3D::SetAppearanceRuntimeLink(surface,
                                                    vr::ecs::AppearanceHandle{.index = 2U, .generation = 1U},
                                                    0ULL,
                                                    0ULL,
                                                    650ULL);

    Transform3D transform{};
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::SetLocalPosition(transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 2.0F});
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    vr::ecs::Surface3DRuntimeScratch scratch{};
    const auto stats = RuntimeSystem3D::Build(&surface, &transform, 1U, scratch, {});
    VR_REQUIRE(stats.emitted_instance_count == 1U);
    VR_REQUIRE(!scratch.instances.empty());
    VR_REQUIRE(!scratch.draw_batches.empty());
    VR_CHECK(surface.runtime.route.visual_resource_id == 19U);
    VR_CHECK(scratch.instances[0U].visual_resource_id == 650U);
    VR_CHECK(scratch.draw_batches[0U].visual_resource_id == 650U);
}

} // namespace

