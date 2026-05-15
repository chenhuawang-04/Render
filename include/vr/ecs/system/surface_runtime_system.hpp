#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/ecs/system/surface_batch_system.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace vr::ecs {

template<typename T>
using SurfaceRuntimeMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

enum class SurfaceRuntimeCacheStatus : std::uint8_t {
    miss = 0U,
    hit_reused = 1U,
    hit_partial_update = 2U,
};

enum class SurfaceRuntimeCacheMissReason : std::uint8_t {
    none = 0U,
    invalid_input = 1U,
    cold_start = 2U,
    components_pointer_changed = 3U,
    transforms_pointer_changed = 4U,
    component_count_changed = 5U,
    surface_signature_changed = 6U,
    visibility_signature_changed = 7U,
    transform_signature_changed = 8U,
    build_config_changed = 9U,
};

[[nodiscard]] constexpr std::uint32_t NextSurfaceRuntimeCacheEpoch(std::uint32_t current_epoch_) noexcept {
    return (current_epoch_ == std::numeric_limits<std::uint32_t>::max())
               ? 1U
               : (current_epoch_ + 1U);
}

struct Surface2DGpuInstance final {
    float world_m00;
    float world_m01;
    float world_m02;
    float world_m10;
    float world_m11;
    float world_m12;

    float size_x;
    float size_y;
    float pivot_x;
    float pivot_y;

    float uv_u0;
    float uv_v0;
    float uv_u1;
    float uv_v1;

    float opacity;
    std::uint32_t tint_rgba8;
    std::uint32_t params;
    std::uint32_t image_slot;
    std::uint32_t sampler_slot;
    std::uint32_t component_index;
};

struct Surface2DDrawBatch final {
    std::uint64_t sort_key;
    std::uint32_t instance_begin;
    std::uint32_t instance_count;
    std::uint32_t surface_id;
    std::uint32_t effective_visual_resource_id;
    std::uint32_t atlas_page_id;
    std::uint32_t first_component_index;
    std::uint32_t params;
};

struct Surface2DRuntimeBuildConfig final {
    bool build_ordered_indices = true;
};

struct Surface2DRuntimeBuildHint final {
    std::uint64_t external_surface_signature = 0U;
    std::uint64_t external_transform_signature = 0U;
    std::uint64_t external_visible_set_signature = 0U;
    const std::uint32_t* transform_dirty_component_indices = nullptr;
    const std::uint32_t* visible_component_indices = nullptr;
    std::uint32_t transform_dirty_component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint8_t use_external_surface_signature = 0U;
    std::uint8_t use_external_transform_signature = 0U;
    std::uint8_t use_visible_component_indices = 0U;
    std::uint8_t use_external_visible_set_signature = 0U;
};

struct Surface2DRuntimeBuildStats final {
    SurfaceBatchBuildStats batch{};
    std::uint32_t candidate_component_count = 0U;
    std::uint32_t emitted_instance_count = 0U;
    std::uint32_t emitted_batch_count = 0U;
    std::uint32_t transform_rewritten_instance_count = 0U;
    std::uint32_t image_source_instance_count = 0U;
    std::uint32_t sprite_source_instance_count = 0U;
    std::uint32_t cache_epoch = 0U;
    std::uint64_t surface_signature = 0U;
    std::uint64_t transform_signature = 0U;
    std::uint64_t visible_set_signature = 0U;
    SurfaceRuntimeCacheStatus cache_status = SurfaceRuntimeCacheStatus::miss;
    SurfaceRuntimeCacheMissReason cache_miss_reason = SurfaceRuntimeCacheMissReason::none;
    bool cache_reused = false;
    bool transform_only_update = false;
    bool used_visible_component_indices = false;
    bool cache_valid_before_build = false;
    bool cache_key_matched = false;
    bool surface_signature_from_hint = false;
    bool transform_signature_from_hint = false;
    bool visible_set_signature_from_hint = false;
    bool transform_update_from_dirty_hint = false;
};

struct Surface2DRuntimeCache final {
    const Surface<Dim2>* components = nullptr;
    const Transform<Dim2>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    std::uint32_t candidate_component_count = 0U;
    std::uint64_t surface_signature = 0U;
    std::uint64_t transform_signature = 0U;
    std::uint64_t visible_set_signature = 0U;
    std::uint32_t epoch = 0U;
    SurfaceRuntimeMcVector<std::uint32_t> instance_world_revisions{};
    SurfaceRuntimeMcVector<std::uint32_t> component_to_instance_index{};
    Surface2DRuntimeBuildConfig build_config{};
    Surface2DRuntimeBuildStats last_stats{};
    bool valid = false;
};

struct Surface2DRuntimeScratch final {
    SurfaceBatchScratch<Dim2> batch_scratch{};
    SurfaceRuntimeMcVector<Surface2DGpuInstance> instances{};
    SurfaceRuntimeMcVector<Surface2DDrawBatch> draw_batches{};
    Surface2DRuntimeCache cache{};
};

struct Surface3DGpuInstance final {
    float world_m00;
    float world_m01;
    float world_m02;
    float world_m03;

    float world_m10;
    float world_m11;
    float world_m12;
    float world_m13;

    float world_m20;
    float world_m21;
    float world_m22;
    float world_m23;

    float world_m30;
    float world_m31;
    float world_m32;
    float world_m33;

    float uv_scale_u;
    float uv_scale_v;
    float uv_bias_u;
    float uv_bias_v;

    std::uint32_t params;
    std::uint32_t effective_visual_resource_id;
    std::uint32_t appearance_record_index;
    std::uint32_t component_index;
};

struct Surface3DDrawBatch final {
    std::uint64_t sort_key;
    std::uint32_t instance_begin;
    std::uint32_t instance_count;
    std::uint32_t effective_visual_resource_id;
    std::uint32_t first_component_index;
    std::uint32_t params;
};

struct Surface3DRuntimeBuildConfig final {
    bool build_ordered_indices = true;
};

struct Surface3DRuntimeBuildHint final {
    std::uint64_t external_surface_signature = 0U;
    std::uint64_t external_transform_signature = 0U;
    std::uint64_t external_visible_set_signature = 0U;
    const std::uint32_t* transform_dirty_component_indices = nullptr;
    const std::uint32_t* visible_component_indices = nullptr;
    std::uint32_t transform_dirty_component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint8_t use_external_surface_signature = 0U;
    std::uint8_t use_external_transform_signature = 0U;
    std::uint8_t use_visible_component_indices = 0U;
    std::uint8_t use_external_visible_set_signature = 0U;
};

struct Surface3DRuntimeBuildStats final {
    SurfaceBatchBuildStats batch{};
    std::uint32_t candidate_component_count = 0U;
    std::uint32_t emitted_instance_count = 0U;
    std::uint32_t emitted_batch_count = 0U;
    std::uint32_t transform_rewritten_instance_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
    std::uint32_t cache_epoch = 0U;
    std::uint64_t surface_signature = 0U;
    std::uint64_t transform_signature = 0U;
    std::uint64_t visible_set_signature = 0U;
    SurfaceRuntimeCacheStatus cache_status = SurfaceRuntimeCacheStatus::miss;
    SurfaceRuntimeCacheMissReason cache_miss_reason = SurfaceRuntimeCacheMissReason::none;
    bool cache_reused = false;
    bool transform_only_update = false;
    bool used_visible_component_indices = false;
    bool cache_valid_before_build = false;
    bool cache_key_matched = false;
    bool surface_signature_from_hint = false;
    bool transform_signature_from_hint = false;
    bool visible_set_signature_from_hint = false;
    bool transform_update_from_dirty_hint = false;
};

struct Surface3DRuntimeCache final {
    const Surface<Dim3>* components = nullptr;
    const Transform<Dim3>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    std::uint32_t candidate_component_count = 0U;
    std::uint64_t surface_signature = 0U;
    std::uint64_t transform_signature = 0U;
    std::uint64_t visible_set_signature = 0U;
    std::uint32_t epoch = 0U;
    SurfaceRuntimeMcVector<std::uint32_t> instance_world_revisions{};
    SurfaceRuntimeMcVector<std::uint32_t> component_to_instance_index{};
    Surface3DRuntimeBuildConfig build_config{};
    Surface3DRuntimeBuildStats last_stats{};
    bool valid = false;
};

struct Surface3DRuntimeScratch final {
    SurfaceBatchScratch<Dim3> batch_scratch{};
    SurfaceRuntimeMcVector<Surface3DGpuInstance> instances{};
    SurfaceRuntimeMcVector<Surface3DDrawBatch> draw_batches{};
    Surface3DRuntimeCache cache{};
};

static_assert(PurePodSurfaceComponent<Surface2DGpuInstance>);
static_assert(PurePodSurfaceComponent<Surface2DDrawBatch>);
static_assert(PurePodSurfaceComponent<Surface3DGpuInstance>);
static_assert(PurePodSurfaceComponent<Surface3DDrawBatch>);
static_assert(alignof(Surface2DGpuInstance) == 4U);
static_assert(alignof(Surface3DGpuInstance) == 4U);

template<DimensionTag DimensionT>
class SurfaceRuntimeSystem;

template<>
class SurfaceRuntimeSystem<Dim2> final {
public:
    using SurfaceType = Surface<Dim2>;
    using TransformType = Transform<Dim2>;
    using BatchSystemType = SurfaceBatchSystem<Dim2>;
    using ScratchType = Surface2DRuntimeScratch;

    static void Reserve(ScratchType& scratch_,
                        std::uint32_t component_count_,
                        std::uint32_t instance_capacity_hint_ = 0U) {
        BatchSystemType::Reserve(scratch_.batch_scratch, component_count_);

        const std::size_t instance_reserve = static_cast<std::size_t>(
            instance_capacity_hint_ > 0U ? instance_capacity_hint_ : component_count_);
        if (scratch_.instances.capacity() < instance_reserve) {
            scratch_.instances.reserve(instance_reserve);
        }
        if (scratch_.draw_batches.capacity() < instance_reserve) {
            scratch_.draw_batches.reserve(instance_reserve);
        }
        if (scratch_.cache.instance_world_revisions.capacity() < instance_reserve) {
            scratch_.cache.instance_world_revisions.reserve(instance_reserve);
        }
        if (scratch_.cache.component_to_instance_index.capacity() < component_count_) {
            scratch_.cache.component_to_instance_index.reserve(component_count_);
        }
    }

    [[nodiscard]] static Surface2DRuntimeBuildStats Build(const SurfaceType* components_,
                                                          const TransformType* transforms_,
                                                          std::uint32_t component_count_,
                                                          ScratchType& scratch_,
                                                          const Surface2DRuntimeBuildConfig& build_config_ = {},
                                                          const Surface2DRuntimeBuildHint& build_hint_ = {}) {
        Surface2DRuntimeBuildStats stats{};
        stats.cache_valid_before_build = scratch_.cache.valid;

        if (components_ == nullptr || component_count_ == 0U) {
            scratch_.instances.clear();
            scratch_.draw_batches.clear();
            InvalidateCache(scratch_.cache);

            stats.cache_status = SurfaceRuntimeCacheStatus::miss;
            stats.cache_miss_reason = SurfaceRuntimeCacheMissReason::invalid_input;
            stats.cache_epoch = scratch_.cache.epoch;
            return stats;
        }

        const bool use_external_surface_signature = build_hint_.use_external_surface_signature != 0U;
        const bool use_external_transform_signature = build_hint_.use_external_transform_signature != 0U;
        const bool use_visible_component_indices = build_hint_.use_visible_component_indices != 0U;
        const bool use_external_visible_set_signature =
            build_hint_.use_external_visible_set_signature != 0U;
        const std::uint32_t* candidate_component_indices = use_visible_component_indices
            ? build_hint_.visible_component_indices
            : nullptr;
        const std::uint32_t candidate_component_count = use_visible_component_indices
            ? build_hint_.visible_component_count
            : component_count_;
        const std::uint64_t visible_set_signature = use_external_visible_set_signature
            ? build_hint_.external_visible_set_signature
            : ComputeVisibleSetSignature(candidate_component_indices,
                                         candidate_component_count,
                                         use_visible_component_indices);
        const std::uint64_t surface_signature = use_external_surface_signature
            ? build_hint_.external_surface_signature
            : ComputeSurfaceSignature(components_,
                                      component_count_,
                                      candidate_component_indices,
                                      candidate_component_count,
                                      use_visible_component_indices);
        const std::uint64_t transform_signature = use_external_transform_signature
            ? build_hint_.external_transform_signature
            : ComputeTransformSignature(components_,
                                        transforms_,
                                        component_count_,
                                        candidate_component_indices,
                                        candidate_component_count,
                                        use_visible_component_indices);

        stats.candidate_component_count = candidate_component_count;
        stats.surface_signature = surface_signature;
        stats.transform_signature = transform_signature;
        stats.visible_set_signature = visible_set_signature;
        stats.used_visible_component_indices = use_visible_component_indices;
        stats.surface_signature_from_hint = use_external_surface_signature;
        stats.transform_signature_from_hint = use_external_transform_signature;
        stats.visible_set_signature_from_hint = use_external_visible_set_signature;

        const CacheProbeResult cache_probe = ProbeCache(scratch_.cache,
                                                        components_,
                                                        transforms_,
                                                        component_count_,
                                                        surface_signature,
                                                        visible_set_signature,
                                                        candidate_component_count,
                                                        build_config_);
        stats.cache_key_matched = cache_probe.key_matched;

        if (cache_probe.key_matched) {
            if (scratch_.cache.transform_signature == transform_signature) {
                stats = scratch_.cache.last_stats;
                stats.candidate_component_count = candidate_component_count;
                stats.surface_signature = surface_signature;
                stats.transform_signature = transform_signature;
                stats.visible_set_signature = visible_set_signature;
                stats.cache_status = SurfaceRuntimeCacheStatus::hit_reused;
                stats.cache_miss_reason = SurfaceRuntimeCacheMissReason::none;
                stats.cache_reused = true;
                stats.transform_only_update = false;
                stats.transform_rewritten_instance_count = 0U;
                stats.used_visible_component_indices = use_visible_component_indices;
                stats.cache_valid_before_build = true;
                stats.cache_key_matched = true;
                stats.surface_signature_from_hint = use_external_surface_signature;
                stats.transform_signature_from_hint = use_external_transform_signature;
                stats.visible_set_signature_from_hint = use_external_visible_set_signature;
                stats.transform_update_from_dirty_hint = false;
                stats.cache_epoch = scratch_.cache.epoch;
                return stats;
            }

            stats = scratch_.cache.last_stats;
            bool used_dirty_hint = false;
            stats.transform_rewritten_instance_count =
                UpdateInstanceWorldMatrices(scratch_.instances,
                                            scratch_.cache.instance_world_revisions,
                                            scratch_.cache.component_to_instance_index,
                                            transforms_,
                                            component_count_,
                                            build_hint_.transform_dirty_component_indices,
                                            build_hint_.transform_dirty_component_count,
                                            used_dirty_hint);
            scratch_.cache.transform_signature = transform_signature;
            stats.candidate_component_count = candidate_component_count;
            stats.surface_signature = surface_signature;
            stats.transform_signature = transform_signature;
            stats.visible_set_signature = visible_set_signature;
            stats.cache_status = SurfaceRuntimeCacheStatus::hit_partial_update;
            stats.cache_miss_reason = SurfaceRuntimeCacheMissReason::none;
            stats.cache_reused = true;
            stats.transform_only_update = true;
            stats.used_visible_component_indices = use_visible_component_indices;
            stats.cache_valid_before_build = true;
            stats.cache_key_matched = true;
            stats.surface_signature_from_hint = use_external_surface_signature;
            stats.transform_signature_from_hint = use_external_transform_signature;
            stats.visible_set_signature_from_hint = use_external_visible_set_signature;
            stats.transform_update_from_dirty_hint = used_dirty_hint;
            stats.cache_epoch = scratch_.cache.epoch;
            scratch_.cache.last_stats = stats;
            return stats;
        }

        scratch_.instances.clear();
        scratch_.draw_batches.clear();

        if (use_visible_component_indices) {
            stats.batch = BatchSystemType::BuildAndSortFromCandidates(components_,
                                                                       component_count_,
                                                                       candidate_component_indices,
                                                                       candidate_component_count,
                                                                       scratch_.batch_scratch,
                                                                       build_config_.build_ordered_indices);
        } else {
            stats.batch = BatchSystemType::BuildAndSort(components_,
                                                        component_count_,
                                                        scratch_.batch_scratch,
                                                        build_config_.build_ordered_indices);
        }
        InitializeComponentToInstanceMap(scratch_.cache.component_to_instance_index, component_count_);

        if (stats.batch.visible_count == 0U) {
            stats.cache_status = SurfaceRuntimeCacheStatus::miss;
            stats.cache_miss_reason = cache_probe.miss_reason;
            stats.cache_reused = false;
            stats.transform_only_update = false;
            stats.candidate_component_count = candidate_component_count;
            stats.surface_signature = surface_signature;
            stats.transform_signature = transform_signature;
            stats.visible_set_signature = visible_set_signature;
            stats.used_visible_component_indices = use_visible_component_indices;
            stats.cache_valid_before_build = scratch_.cache.valid;
            stats.cache_key_matched = false;
            stats.surface_signature_from_hint = use_external_surface_signature;
            stats.transform_signature_from_hint = use_external_transform_signature;
            stats.visible_set_signature_from_hint = use_external_visible_set_signature;
            stats.transform_update_from_dirty_hint = false;
            CommitMissCache(scratch_.cache,
                            components_,
                            transforms_,
                            component_count_,
                            surface_signature,
                            transform_signature,
                            visible_set_signature,
                            candidate_component_count,
                            build_config_,
                            stats);
            return stats;
        }

        const SurfaceBatchItem* sorted_items = BatchSystemType::SortedItems(scratch_.batch_scratch);
        const std::uint32_t sorted_count = BatchSystemType::VisibleCount(scratch_.batch_scratch);
        scratch_.instances.resize(sorted_count);

        const Affine2x3 identity = spatial_math::IdentityAffine2x3();
        for (std::uint32_t i = 0U; i < sorted_count; ++i) {
            const SurfaceBatchItem& item = sorted_items[i];
            const SurfaceType& component = components_[item.component_index];

            const Affine2x3& world_matrix = (transforms_ != nullptr && item.component_index < component_count_)
                ? transforms_[item.component_index].runtime.world_matrix
                : identity;

            Surface2DGpuInstance instance{};
            WriteWorldToInstance(instance, world_matrix);
            instance.size_x = component.runtime.size.x;
            instance.size_y = component.runtime.size.y;
            instance.pivot_x = component.runtime.pivot.x;
            instance.pivot_y = component.runtime.pivot.y;
            instance.uv_u0 = component.style.uv_u0;
            instance.uv_v0 = component.style.uv_v0;
            instance.uv_u1 = component.style.uv_u1;
            instance.uv_v1 = component.style.uv_v1;
            const AppearanceRuntimeBridge2D appearance_bridge =
                ReadAppearanceRuntimeBridge2D(component.runtime);
            instance.opacity = appearance_bridge.opacity;
            instance.tint_rgba8 = PackRgba8(appearance_bridge.fill_color);
            instance.params = PackParams(component);
            instance.image_slot = component.runtime.route.surface_id;
            instance.sampler_slot = 0U;
            instance.component_index = item.component_index;
            scratch_.instances[i] = instance;

            if (item.component_index < scratch_.cache.component_to_instance_index.size()) {
                scratch_.cache.component_to_instance_index[item.component_index] = i;
            }

            if (component.runtime.source.source_kind == Surface2DSourceKind::image) {
                ++stats.image_source_instance_count;
            } else if (component.runtime.source.source_kind == Surface2DSourceKind::sprite) {
                ++stats.sprite_source_instance_count;
            }

            AppendOrMergeBatch(component,
                               item.sort_key,
                               item.component_index,
                               i,
                               scratch_);
        }

        InitializeInstanceWorldRevisionCache(scratch_.cache.instance_world_revisions,
                                             scratch_.instances,
                                             transforms_,
                                             component_count_);

        stats.emitted_instance_count = static_cast<std::uint32_t>(scratch_.instances.size());
        stats.emitted_batch_count = static_cast<std::uint32_t>(scratch_.draw_batches.size());
        stats.transform_rewritten_instance_count = stats.emitted_instance_count;
        stats.cache_status = SurfaceRuntimeCacheStatus::miss;
        stats.cache_miss_reason = cache_probe.miss_reason;
        stats.cache_reused = false;
        stats.transform_only_update = false;
        stats.candidate_component_count = candidate_component_count;
        stats.surface_signature = surface_signature;
        stats.transform_signature = transform_signature;
        stats.visible_set_signature = visible_set_signature;
        stats.used_visible_component_indices = use_visible_component_indices;
        stats.cache_valid_before_build = scratch_.cache.valid;
        stats.cache_key_matched = false;
        stats.surface_signature_from_hint = use_external_surface_signature;
        stats.transform_signature_from_hint = use_external_transform_signature;
        stats.visible_set_signature_from_hint = use_external_visible_set_signature;
        stats.transform_update_from_dirty_hint = false;

        CommitMissCache(scratch_.cache,
                        components_,
                        transforms_,
                        component_count_,
                        surface_signature,
                        transform_signature,
                        visible_set_signature,
                        candidate_component_count,
                        build_config_,
                        stats);
        return stats;
    }

private:
    struct CacheProbeResult final {
        bool key_matched = false;
        SurfaceRuntimeCacheMissReason miss_reason = SurfaceRuntimeCacheMissReason::none;
    };

    static constexpr std::uint32_t invalid_instance_index = std::numeric_limits<std::uint32_t>::max();

    static void InitializeComponentToInstanceMap(
        SurfaceRuntimeMcVector<std::uint32_t>& component_to_instance_index_,
        std::uint32_t component_count_) {
        component_to_instance_index_.resize(static_cast<std::size_t>(component_count_));
        std::fill(component_to_instance_index_.begin(),
                  component_to_instance_index_.end(),
                  invalid_instance_index);
    }

    static void InvalidateCache(Surface2DRuntimeCache& cache_) noexcept {
        cache_.components = nullptr;
        cache_.transforms = nullptr;
        cache_.component_count = 0U;
        cache_.candidate_component_count = 0U;
        cache_.surface_signature = 0U;
        cache_.transform_signature = 0U;
        cache_.visible_set_signature = 0U;
        cache_.epoch = 0U;
        cache_.instance_world_revisions.clear();
        cache_.component_to_instance_index.clear();
        cache_.valid = false;
    }

    static void CommitMissCache(Surface2DRuntimeCache& cache_,
                                const SurfaceType* components_,
                                const TransformType* transforms_,
                                std::uint32_t component_count_,
                                std::uint64_t surface_signature_,
                                std::uint64_t transform_signature_,
                                std::uint64_t visible_set_signature_,
                                std::uint32_t candidate_component_count_,
                                const Surface2DRuntimeBuildConfig& build_config_,
                                Surface2DRuntimeBuildStats& stats_) {
        cache_.components = components_;
        cache_.transforms = transforms_;
        cache_.component_count = component_count_;
        cache_.candidate_component_count = candidate_component_count_;
        cache_.surface_signature = surface_signature_;
        cache_.transform_signature = transform_signature_;
        cache_.visible_set_signature = visible_set_signature_;
        cache_.build_config = build_config_;
        cache_.epoch = NextSurfaceRuntimeCacheEpoch(cache_.epoch);
        stats_.cache_epoch = cache_.epoch;
        cache_.last_stats = stats_;
        cache_.valid = true;
    }

    [[nodiscard]] static std::uint32_t PackRgba8(const Rgba8& color_) noexcept {
        return static_cast<std::uint32_t>(color_.r) |
               (static_cast<std::uint32_t>(color_.g) << 8U) |
               (static_cast<std::uint32_t>(color_.b) << 16U) |
               (static_cast<std::uint32_t>(color_.a) << 24U);
    }

    [[nodiscard]] static std::uint32_t PackParams(const SurfaceType& component_) noexcept {
        const AppearanceRuntimeBridge2D appearance_bridge =
            ReadAppearanceRuntimeBridge2D(component_.runtime);
        RuntimeBlendPreset blend_preset = ResolveRuntimeBlendPreset(appearance_bridge);
        if (component_.runtime.route.appearance_handle.index != invalid_appearance_handle.index &&
            component_.runtime.route.appearance_handle.generation != 0U) {
            blend_preset = ResolveRuntimeBlendPreset(component_.runtime.route.appearance_pipeline_bucket);
        }

        std::uint32_t params = 0U;
        params |= (component_.style.flip_x != 0U) ? 0x4U : 0U;
        params |= (component_.style.flip_y != 0U) ? 0x8U : 0U;
        switch (blend_preset) {
        case RuntimeBlendPreset::additive:
            params |= 0x1U;
            break;
        case RuntimeBlendPreset::multiply:
            params |= 0x2U;
            break;
        case RuntimeBlendPreset::screen:
            params |= 0x3U;
            break;
        case RuntimeBlendPreset::premultiplied_alpha:
            params |= 0x10U;
            break;
        case RuntimeBlendPreset::alpha:
        case RuntimeBlendPreset::opaque:
        default:
            break;
        }
        params |= (static_cast<std::uint32_t>(component_.runtime.source.source_kind) & 0x3U) << 5U;
        params |= EncodeRuntimeBlendPresetBits(blend_preset, surface2d_runtime_blend_shift);
        return params;
    }

    [[nodiscard]] static std::uint32_t FloatBits(float value_) noexcept {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value_, sizeof(bits));
        return bits;
    }

    static constexpr std::uint64_t k_hash_prime = 1099511628211ULL;

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= k_hash_prime;
    }

    [[nodiscard]] static bool IsBuildConfigEqual(const Surface2DRuntimeBuildConfig& lhs_,
                                                 const Surface2DRuntimeBuildConfig& rhs_) noexcept {
        return lhs_.build_ordered_indices == rhs_.build_ordered_indices;
    }

    [[nodiscard]] static CacheProbeResult ProbeCache(const Surface2DRuntimeCache& cache_,
                                                     const SurfaceType* components_,
                                                     const TransformType* transforms_,
                                                     std::uint32_t component_count_,
                                                     std::uint64_t surface_signature_,
                                                     std::uint64_t visible_set_signature_,
                                                     std::uint32_t candidate_component_count_,
                                                     const Surface2DRuntimeBuildConfig& build_config_) noexcept {
        if (!cache_.valid) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::cold_start
            };
        }
        if (cache_.components != components_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::components_pointer_changed
            };
        }
        if (cache_.transforms != transforms_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::transforms_pointer_changed
            };
        }
        if (cache_.component_count != component_count_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::component_count_changed
            };
        }
        if (cache_.surface_signature != surface_signature_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::surface_signature_changed
            };
        }
        if (cache_.visible_set_signature != visible_set_signature_ ||
            cache_.candidate_component_count != candidate_component_count_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::visibility_signature_changed
            };
        }
        if (!IsBuildConfigEqual(cache_.build_config, build_config_)) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::build_config_changed
            };
        }
        return CacheProbeResult{
            .key_matched = true,
            .miss_reason = SurfaceRuntimeCacheMissReason::none
        };
    }

    [[nodiscard]] static std::uint64_t ComputeVisibleSetSignature(
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_component_count_,
        bool use_candidate_indices_) noexcept {
        if (!use_candidate_indices_) {
            return 0U;
        }

        std::uint64_t hash = 0xa69d6f2c3e4b5871ULL;
        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            HashCombine(hash, 0xffffffffffffffffULL);
            return hash;
        }

        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            HashCombine(hash, static_cast<std::uint64_t>(candidate_component_indices_[i]));
        }
        return hash;
    }

    static void HashSurfaceComponent(std::uint64_t& hash_,
                                     const SurfaceType& component_) noexcept {
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.route.visible));
        if (component_.runtime.route.visible == 0U) {
            return;
        }

        HashCombine(hash_, component_.runtime.route.sort_key);
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.route.surface_id));
        HashCombine(hash_,
                    static_cast<std::uint64_t>(ResolveEffectiveVisualResourceId(component_.runtime.route)));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.route.user_data));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.source.source_kind));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.source.surface_id));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.source.atlas_page_id));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.source_revision));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.runtime.size.x)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.runtime.size.y)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.runtime.pivot.x)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.runtime.pivot.y)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.style.uv_u0)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.style.uv_v0)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.style.uv_u1)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.style.uv_v1)));
        const AppearanceRuntimeBridge2D appearance_bridge =
            ReadAppearanceRuntimeBridge2D(component_.runtime);
        HashCombine(hash_, static_cast<std::uint64_t>(PackRgba8(appearance_bridge.fill_color)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(appearance_bridge.opacity)));
        HashCombine(hash_, static_cast<std::uint64_t>(appearance_bridge.layer));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.style.flip_x));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.style.flip_y));
        HashCombine(hash_, static_cast<std::uint64_t>(PackParams(component_)));
    }

    [[nodiscard]] static std::uint64_t ComputeSurfaceSignature(const SurfaceType* components_,
                                                               std::uint32_t component_count_,
                                                               const std::uint32_t* candidate_component_indices_,
                                                               std::uint32_t candidate_component_count_,
                                                               bool use_candidate_indices_) noexcept {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        if (components_ == nullptr || component_count_ == 0U) {
            return hash;
        }

        if (!use_candidate_indices_) {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                HashSurfaceComponent(hash, components_[i]);
            }
            return hash;
        }

        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            return hash;
        }

        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            const std::uint32_t component_index = candidate_component_indices_[i];
            if (component_index >= component_count_) {
                continue;
            }
            HashSurfaceComponent(hash, components_[component_index]);
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeTransformSignature(const SurfaceType* components_,
                                                                 const TransformType* transforms_,
                                                                 std::uint32_t component_count_,
                                                                 const std::uint32_t* candidate_component_indices_,
                                                                 std::uint32_t candidate_component_count_,
                                                                 bool use_candidate_indices_) noexcept {
        if (transforms_ == nullptr || components_ == nullptr) {
            return 0U;
        }

        std::uint64_t hash = 0xcbf29ce484222325ULL;
        if (!use_candidate_indices_) {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                if (components_[i].runtime.route.visible == 0U) {
                    continue;
                }
                HashCombine(hash, static_cast<std::uint64_t>(transforms_[i].runtime.world_revision));
            }
            return hash;
        }

        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            return hash;
        }

        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            const std::uint32_t component_index = candidate_component_indices_[i];
            if (component_index >= component_count_) {
                continue;
            }
            if (components_[component_index].runtime.route.visible == 0U) {
                continue;
            }
            HashCombine(hash, static_cast<std::uint64_t>(transforms_[component_index].runtime.world_revision));
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeSurfaceSignature(const SurfaceType* components_,
                                                               std::uint32_t component_count_) noexcept {
        return ComputeSurfaceSignature(components_,
                                       component_count_,
                                       nullptr,
                                       component_count_,
                                       false);
    }

    [[nodiscard]] static std::uint64_t ComputeTransformSignature(const SurfaceType* components_,
                                                                 const TransformType* transforms_,
                                                                 std::uint32_t component_count_) noexcept {
        return ComputeTransformSignature(components_,
                                         transforms_,
                                         component_count_,
                                         nullptr,
                                         component_count_,
                                         false);
    }

    static void WriteWorldToInstance(Surface2DGpuInstance& instance_,
                                     const Affine2x3& world_matrix_) noexcept {
        instance_.world_m00 = world_matrix_.m00;
        instance_.world_m01 = world_matrix_.m01;
        instance_.world_m02 = world_matrix_.m02;
        instance_.world_m10 = world_matrix_.m10;
        instance_.world_m11 = world_matrix_.m11;
        instance_.world_m12 = world_matrix_.m12;
    }

    [[nodiscard]] static std::uint32_t ReadWorldRevision(const TransformType* transforms_,
                                                         std::uint32_t component_count_,
                                                         std::uint32_t component_index_) noexcept {
        if (transforms_ == nullptr || component_index_ >= component_count_) {
            return 0U;
        }
        return transforms_[component_index_].runtime.world_revision;
    }

    static void InitializeInstanceWorldRevisionCache(SurfaceRuntimeMcVector<std::uint32_t>& revisions_,
                                                     const SurfaceRuntimeMcVector<Surface2DGpuInstance>& instances_,
                                                     const TransformType* transforms_,
                                                     std::uint32_t component_count_) {
        revisions_.resize(instances_.size());
        if (transforms_ == nullptr) {
            std::fill(revisions_.begin(), revisions_.end(), 0U);
            return;
        }
        for (std::size_t i = 0U; i < instances_.size(); ++i) {
            const std::uint32_t component_index = instances_[i].component_index;
            revisions_[i] = (component_index < component_count_)
                ? transforms_[component_index].runtime.world_revision
                : 0U;
        }
    }

    [[nodiscard]] static std::uint32_t UpdateInstanceWorldMatrices(
        SurfaceRuntimeMcVector<Surface2DGpuInstance>& instances_,
        SurfaceRuntimeMcVector<std::uint32_t>& cached_revisions_,
        const SurfaceRuntimeMcVector<std::uint32_t>& component_to_instance_index_,
        const TransformType* transforms_,
        std::uint32_t component_count_,
        const std::uint32_t* transform_dirty_component_indices_,
        std::uint32_t transform_dirty_component_count_,
        bool& used_dirty_hint_) {
        used_dirty_hint_ = false;
        if (cached_revisions_.size() != instances_.size()) {
            InitializeInstanceWorldRevisionCache(cached_revisions_,
                                                 instances_,
                                                 transforms_,
                                                 component_count_);
        }

        const Affine2x3 identity = spatial_math::IdentityAffine2x3();
        const bool has_transforms = transforms_ != nullptr;
        std::uint32_t rewritten_count = 0U;
        if (transform_dirty_component_indices_ != nullptr &&
            transform_dirty_component_count_ > 0U &&
            component_to_instance_index_.size() == static_cast<std::size_t>(component_count_)) {
            used_dirty_hint_ = true;
            if (transform_dirty_component_count_ == 1U) {
                const std::uint32_t component_index = transform_dirty_component_indices_[0U];
                if (component_index >= component_count_) {
                    return 0U;
                }
                const std::uint32_t instance_index = component_to_instance_index_[component_index];
                if (instance_index == invalid_instance_index || instance_index >= instances_.size()) {
                    return 0U;
                }

                const std::uint32_t current_revision = has_transforms
                    ? transforms_[component_index].runtime.world_revision
                    : 0U;
                if (cached_revisions_[instance_index] == current_revision) {
                    return 0U;
                }

                Surface2DGpuInstance& instance = instances_[instance_index];
                if (has_transforms) {
                    WriteWorldToInstance(instance,
                                         transforms_[component_index].runtime.world_matrix);
                } else {
                    WriteWorldToInstance(instance, identity);
                }
                cached_revisions_[instance_index] = current_revision;
                return 1U;
            }
            for (std::uint32_t i = 0U; i < transform_dirty_component_count_; ++i) {
                const std::uint32_t component_index = transform_dirty_component_indices_[i];
                if (component_index >= component_count_) {
                    continue;
                }

                const std::uint32_t instance_index = component_to_instance_index_[component_index];
                if (instance_index == invalid_instance_index || instance_index >= instances_.size()) {
                    continue;
                }

                Surface2DGpuInstance& instance = instances_[instance_index];
                const std::uint32_t current_revision = has_transforms
                    ? transforms_[component_index].runtime.world_revision
                    : 0U;
                if (cached_revisions_[instance_index] == current_revision) {
                    continue;
                }

                if (has_transforms) {
                    WriteWorldToInstance(instance,
                                         transforms_[component_index].runtime.world_matrix);
                } else {
                    WriteWorldToInstance(instance, identity);
                }
                cached_revisions_[instance_index] = current_revision;
                ++rewritten_count;
            }
            return rewritten_count;
        }

        if (has_transforms) {
            for (std::size_t index = 0U; index < instances_.size(); ++index) {
                Surface2DGpuInstance& instance = instances_[index];
                const std::uint32_t component_index = instance.component_index;
                const bool valid_component_index = component_index < component_count_;
                const std::uint32_t current_revision = valid_component_index
                    ? transforms_[component_index].runtime.world_revision
                    : 0U;
                if (cached_revisions_[index] == current_revision) {
                    continue;
                }

                if (valid_component_index) {
                    WriteWorldToInstance(instance,
                                         transforms_[component_index].runtime.world_matrix);
                } else {
                    WriteWorldToInstance(instance, identity);
                }
                cached_revisions_[index] = current_revision;
                ++rewritten_count;
            }
            return rewritten_count;
        }

        for (std::size_t index = 0U; index < instances_.size(); ++index) {
            if (cached_revisions_[index] == 0U) {
                continue;
            }
            WriteWorldToInstance(instances_[index], identity);
            cached_revisions_[index] = 0U;
            ++rewritten_count;
        }
        return rewritten_count;
    }

    static void AppendOrMergeBatch(const SurfaceType& component_,
                                   std::uint64_t sort_key_,
                                   std::uint32_t component_index_,
                                   std::uint32_t instance_index_,
                                   ScratchType& scratch_) {
        const std::uint32_t params = PackParams(component_);
        const std::uint32_t effective_visual_resource_id =
            ResolveEffectiveVisualResourceId(component_.runtime.route);
        if (!scratch_.draw_batches.empty()) {
            Surface2DDrawBatch& last = scratch_.draw_batches.back();
            if (last.sort_key == sort_key_ &&
                last.surface_id == component_.runtime.route.surface_id &&
                last.effective_visual_resource_id == effective_visual_resource_id &&
                last.atlas_page_id == component_.runtime.source.atlas_page_id &&
                last.params == params &&
                last.instance_begin + last.instance_count == instance_index_) {
                ++last.instance_count;
                return;
            }
        }

        Surface2DDrawBatch batch{};
        batch.sort_key = sort_key_;
        batch.instance_begin = instance_index_;
        batch.instance_count = 1U;
        batch.surface_id = component_.runtime.route.surface_id;
        batch.effective_visual_resource_id = effective_visual_resource_id;
        batch.atlas_page_id = component_.runtime.source.atlas_page_id;
        batch.first_component_index = component_index_;
        batch.params = params;
        scratch_.draw_batches.push_back(batch);
    }
};

template<>
class SurfaceRuntimeSystem<Dim3> final {
public:
    using SurfaceType = Surface<Dim3>;
    using TransformType = Transform<Dim3>;
    using BatchSystemType = SurfaceBatchSystem<Dim3>;
    using ScratchType = Surface3DRuntimeScratch;

    static void Reserve(ScratchType& scratch_,
                        std::uint32_t component_count_,
                        std::uint32_t instance_capacity_hint_ = 0U) {
        BatchSystemType::Reserve(scratch_.batch_scratch, component_count_);

        const std::size_t instance_reserve = static_cast<std::size_t>(
            instance_capacity_hint_ > 0U ? instance_capacity_hint_ : component_count_);
        if (scratch_.instances.capacity() < instance_reserve) {
            scratch_.instances.reserve(instance_reserve);
        }
        if (scratch_.draw_batches.capacity() < instance_reserve) {
            scratch_.draw_batches.reserve(instance_reserve);
        }
        if (scratch_.cache.instance_world_revisions.capacity() < instance_reserve) {
            scratch_.cache.instance_world_revisions.reserve(instance_reserve);
        }
        if (scratch_.cache.component_to_instance_index.capacity() < component_count_) {
            scratch_.cache.component_to_instance_index.reserve(component_count_);
        }
    }

    [[nodiscard]] static Surface3DRuntimeBuildStats Build(const SurfaceType* components_,
                                                          const TransformType* transforms_,
                                                          std::uint32_t component_count_,
                                                          ScratchType& scratch_,
                                                          const Surface3DRuntimeBuildConfig& build_config_ = {},
                                                          const Surface3DRuntimeBuildHint& build_hint_ = {}) {
        Surface3DRuntimeBuildStats stats{};
        stats.cache_valid_before_build = scratch_.cache.valid;

        if (components_ == nullptr || component_count_ == 0U) {
            scratch_.instances.clear();
            scratch_.draw_batches.clear();
            InvalidateCache(scratch_.cache);

            stats.cache_status = SurfaceRuntimeCacheStatus::miss;
            stats.cache_miss_reason = SurfaceRuntimeCacheMissReason::invalid_input;
            stats.cache_epoch = scratch_.cache.epoch;
            return stats;
        }

        const bool use_external_surface_signature = build_hint_.use_external_surface_signature != 0U;
        const bool use_external_transform_signature = build_hint_.use_external_transform_signature != 0U;
        const bool use_visible_component_indices = build_hint_.use_visible_component_indices != 0U;
        const bool use_external_visible_set_signature =
            build_hint_.use_external_visible_set_signature != 0U;
        const std::uint32_t* candidate_component_indices = use_visible_component_indices
            ? build_hint_.visible_component_indices
            : nullptr;
        const std::uint32_t candidate_component_count = use_visible_component_indices
            ? build_hint_.visible_component_count
            : component_count_;
        const std::uint64_t visible_set_signature = use_external_visible_set_signature
            ? build_hint_.external_visible_set_signature
            : ComputeVisibleSetSignature(candidate_component_indices,
                                         candidate_component_count,
                                         use_visible_component_indices);
        const std::uint64_t surface_signature = use_external_surface_signature
            ? build_hint_.external_surface_signature
            : ComputeSurfaceSignature(components_,
                                      component_count_,
                                      candidate_component_indices,
                                      candidate_component_count,
                                      use_visible_component_indices);
        const std::uint64_t transform_signature = use_external_transform_signature
            ? build_hint_.external_transform_signature
            : ComputeTransformSignature(components_,
                                        transforms_,
                                        component_count_,
                                        candidate_component_indices,
                                        candidate_component_count,
                                        use_visible_component_indices);

        stats.candidate_component_count = candidate_component_count;
        stats.surface_signature = surface_signature;
        stats.transform_signature = transform_signature;
        stats.visible_set_signature = visible_set_signature;
        stats.used_visible_component_indices = use_visible_component_indices;
        stats.surface_signature_from_hint = use_external_surface_signature;
        stats.transform_signature_from_hint = use_external_transform_signature;
        stats.visible_set_signature_from_hint = use_external_visible_set_signature;

        const CacheProbeResult cache_probe = ProbeCache(scratch_.cache,
                                                        components_,
                                                        transforms_,
                                                        component_count_,
                                                        surface_signature,
                                                        visible_set_signature,
                                                        candidate_component_count,
                                                        build_config_);
        stats.cache_key_matched = cache_probe.key_matched;

        if (cache_probe.key_matched) {
            if (scratch_.cache.transform_signature == transform_signature) {
                stats = scratch_.cache.last_stats;
                stats.candidate_component_count = candidate_component_count;
                stats.surface_signature = surface_signature;
                stats.transform_signature = transform_signature;
                stats.visible_set_signature = visible_set_signature;
                stats.cache_status = SurfaceRuntimeCacheStatus::hit_reused;
                stats.cache_miss_reason = SurfaceRuntimeCacheMissReason::none;
                stats.cache_reused = true;
                stats.transform_only_update = false;
                stats.transform_rewritten_instance_count = 0U;
                stats.used_visible_component_indices = use_visible_component_indices;
                stats.cache_valid_before_build = true;
                stats.cache_key_matched = true;
                stats.surface_signature_from_hint = use_external_surface_signature;
                stats.transform_signature_from_hint = use_external_transform_signature;
                stats.visible_set_signature_from_hint = use_external_visible_set_signature;
                stats.transform_update_from_dirty_hint = false;
                stats.cache_epoch = scratch_.cache.epoch;
                return stats;
            }

            stats = scratch_.cache.last_stats;
            bool used_dirty_hint = false;
            stats.transform_rewritten_instance_count =
                UpdateInstanceWorldMatrices(scratch_.instances,
                                            scratch_.cache.instance_world_revisions,
                                            scratch_.cache.component_to_instance_index,
                                            transforms_,
                                            component_count_,
                                            build_hint_.transform_dirty_component_indices,
                                            build_hint_.transform_dirty_component_count,
                                            used_dirty_hint);
            scratch_.cache.transform_signature = transform_signature;
            stats.candidate_component_count = candidate_component_count;
            stats.surface_signature = surface_signature;
            stats.transform_signature = transform_signature;
            stats.visible_set_signature = visible_set_signature;
            stats.cache_status = SurfaceRuntimeCacheStatus::hit_partial_update;
            stats.cache_miss_reason = SurfaceRuntimeCacheMissReason::none;
            stats.cache_reused = true;
            stats.transform_only_update = true;
            stats.used_visible_component_indices = use_visible_component_indices;
            stats.cache_valid_before_build = true;
            stats.cache_key_matched = true;
            stats.surface_signature_from_hint = use_external_surface_signature;
            stats.transform_signature_from_hint = use_external_transform_signature;
            stats.visible_set_signature_from_hint = use_external_visible_set_signature;
            stats.transform_update_from_dirty_hint = used_dirty_hint;
            stats.cache_epoch = scratch_.cache.epoch;
            scratch_.cache.last_stats = stats;
            return stats;
        }

        scratch_.instances.clear();
        scratch_.draw_batches.clear();

        if (use_visible_component_indices) {
            stats.batch = BatchSystemType::BuildAndSortFromCandidates(components_,
                                                                       component_count_,
                                                                       candidate_component_indices,
                                                                       candidate_component_count,
                                                                       scratch_.batch_scratch,
                                                                       build_config_.build_ordered_indices);
        } else {
            stats.batch = BatchSystemType::BuildAndSort(components_,
                                                        component_count_,
                                                        scratch_.batch_scratch,
                                                        build_config_.build_ordered_indices);
        }
        InitializeComponentToInstanceMap(scratch_.cache.component_to_instance_index, component_count_);

        if (stats.batch.visible_count == 0U) {
            stats.cache_status = SurfaceRuntimeCacheStatus::miss;
            stats.cache_miss_reason = cache_probe.miss_reason;
            stats.cache_reused = false;
            stats.transform_only_update = false;
            stats.candidate_component_count = candidate_component_count;
            stats.surface_signature = surface_signature;
            stats.transform_signature = transform_signature;
            stats.visible_set_signature = visible_set_signature;
            stats.used_visible_component_indices = use_visible_component_indices;
            stats.cache_valid_before_build = scratch_.cache.valid;
            stats.cache_key_matched = false;
            stats.surface_signature_from_hint = use_external_surface_signature;
            stats.transform_signature_from_hint = use_external_transform_signature;
            stats.visible_set_signature_from_hint = use_external_visible_set_signature;
            stats.transform_update_from_dirty_hint = false;
            CommitMissCache(scratch_.cache,
                            components_,
                            transforms_,
                            component_count_,
                            surface_signature,
                            transform_signature,
                            visible_set_signature,
                            candidate_component_count,
                            build_config_,
                            stats);
            return stats;
        }

        const SurfaceBatchItem* sorted_items = BatchSystemType::SortedItems(scratch_.batch_scratch);
        const std::uint32_t sorted_count = BatchSystemType::VisibleCount(scratch_.batch_scratch);
        scratch_.instances.resize(sorted_count);

        const Matrix4x4 identity = spatial_math::IdentityMatrix4x4();
        for (std::uint32_t i = 0U; i < sorted_count; ++i) {
            const SurfaceBatchItem& item = sorted_items[i];
            const SurfaceType& component = components_[item.component_index];

            const Matrix4x4& world_matrix = (transforms_ != nullptr && item.component_index < component_count_)
                ? transforms_[item.component_index].runtime.world_matrix
                : identity;
            Surface3DGpuInstance instance{};
            WriteWorldToInstance(instance, world_matrix);
            instance.uv_scale_u = component.style.uv_scale_u;
            instance.uv_scale_v = component.style.uv_scale_v;
            instance.uv_bias_u = component.style.uv_bias_u;
            instance.uv_bias_v = component.style.uv_bias_v;
            instance.params = PackParams(component);
            instance.effective_visual_resource_id =
                ResolveEffectiveVisualResourceId(component.runtime.route);
            instance.appearance_record_index = invalid_appearance_handle.index;
            instance.component_index = item.component_index;
            scratch_.instances[i] = instance;

            if (item.component_index < scratch_.cache.component_to_instance_index.size()) {
                scratch_.cache.component_to_instance_index[item.component_index] = i;
            }

            AppendOrMergeBatch(component,
                               item.sort_key,
                               item.component_index,
                               i,
                               scratch_);
        }

        InitializeInstanceWorldRevisionCache(scratch_.cache.instance_world_revisions,
                                             scratch_.instances,
                                             transforms_,
                                             component_count_);

        for (const Surface3DDrawBatch& batch : scratch_.draw_batches) {
            if ((batch.params & 0x1U) != 0U) {
                ++stats.depth_test_batch_count;
            }
            if ((batch.params & 0x2U) != 0U) {
                ++stats.depth_write_batch_count;
            }
        }

        stats.emitted_instance_count = static_cast<std::uint32_t>(scratch_.instances.size());
        stats.emitted_batch_count = static_cast<std::uint32_t>(scratch_.draw_batches.size());
        stats.transform_rewritten_instance_count = stats.emitted_instance_count;
        stats.cache_status = SurfaceRuntimeCacheStatus::miss;
        stats.cache_miss_reason = cache_probe.miss_reason;
        stats.cache_reused = false;
        stats.transform_only_update = false;
        stats.candidate_component_count = candidate_component_count;
        stats.surface_signature = surface_signature;
        stats.transform_signature = transform_signature;
        stats.visible_set_signature = visible_set_signature;
        stats.used_visible_component_indices = use_visible_component_indices;
        stats.cache_valid_before_build = scratch_.cache.valid;
        stats.cache_key_matched = false;
        stats.surface_signature_from_hint = use_external_surface_signature;
        stats.transform_signature_from_hint = use_external_transform_signature;
        stats.visible_set_signature_from_hint = use_external_visible_set_signature;
        stats.transform_update_from_dirty_hint = false;

        CommitMissCache(scratch_.cache,
                        components_,
                        transforms_,
                        component_count_,
                        surface_signature,
                        transform_signature,
                        visible_set_signature,
                        candidate_component_count,
                        build_config_,
                        stats);
        return stats;
    }

private:
    struct CacheProbeResult final {
        bool key_matched = false;
        SurfaceRuntimeCacheMissReason miss_reason = SurfaceRuntimeCacheMissReason::none;
    };

    static constexpr std::uint32_t invalid_instance_index = std::numeric_limits<std::uint32_t>::max();

    static void InitializeComponentToInstanceMap(
        SurfaceRuntimeMcVector<std::uint32_t>& component_to_instance_index_,
        std::uint32_t component_count_) {
        component_to_instance_index_.resize(static_cast<std::size_t>(component_count_));
        std::fill(component_to_instance_index_.begin(),
                  component_to_instance_index_.end(),
                  invalid_instance_index);
    }

    static void InvalidateCache(Surface3DRuntimeCache& cache_) noexcept {
        cache_.components = nullptr;
        cache_.transforms = nullptr;
        cache_.component_count = 0U;
        cache_.candidate_component_count = 0U;
        cache_.surface_signature = 0U;
        cache_.transform_signature = 0U;
        cache_.visible_set_signature = 0U;
        cache_.epoch = 0U;
        cache_.instance_world_revisions.clear();
        cache_.component_to_instance_index.clear();
        cache_.valid = false;
    }

    static void CommitMissCache(Surface3DRuntimeCache& cache_,
                                const SurfaceType* components_,
                                const TransformType* transforms_,
                                std::uint32_t component_count_,
                                std::uint64_t surface_signature_,
                                std::uint64_t transform_signature_,
                                std::uint64_t visible_set_signature_,
                                std::uint32_t candidate_component_count_,
                                const Surface3DRuntimeBuildConfig& build_config_,
                                Surface3DRuntimeBuildStats& stats_) {
        cache_.components = components_;
        cache_.transforms = transforms_;
        cache_.component_count = component_count_;
        cache_.candidate_component_count = candidate_component_count_;
        cache_.surface_signature = surface_signature_;
        cache_.transform_signature = transform_signature_;
        cache_.visible_set_signature = visible_set_signature_;
        cache_.build_config = build_config_;
        cache_.epoch = NextSurfaceRuntimeCacheEpoch(cache_.epoch);
        stats_.cache_epoch = cache_.epoch;
        cache_.last_stats = stats_;
        cache_.valid = true;
    }

    [[nodiscard]] static std::uint32_t PackRgba8(const Rgba8& color_) noexcept {
        return static_cast<std::uint32_t>(color_.r) |
               (static_cast<std::uint32_t>(color_.g) << 8U) |
               (static_cast<std::uint32_t>(color_.b) << 16U) |
               (static_cast<std::uint32_t>(color_.a) << 24U);
    }

    [[nodiscard]] static std::uint32_t PackParams(const SurfaceType& component_) noexcept {
        const AppearanceRuntimeBridge3D appearance_bridge =
            ReadAppearanceRuntimeBridge3D(component_.runtime);
        RuntimeBlendPreset blend_preset = ResolveRuntimeBlendPreset(appearance_bridge);
        if (component_.runtime.route.appearance_handle.index != invalid_appearance_handle.index &&
            component_.runtime.route.appearance_handle.generation != 0U) {
            blend_preset = ResolveRuntimeBlendPreset(component_.runtime.route.appearance_pipeline_bucket);
        }
        const bool depth_write_enabled =
            IsAppearanceRuntimeBridge3DDepthWriteEnabled(appearance_bridge) &&
            !IsTransparentBlendPreset(blend_preset);
        std::uint32_t params = 0U;
        params |= IsAppearanceRuntimeBridge3DDepthTestEnabled(appearance_bridge)
            ? 0x1U
            : 0U;
        params |= depth_write_enabled ? 0x2U : 0U;
        params |= IsAppearanceRuntimeBridge3DDoubleSided(appearance_bridge)
            ? 0x4U
            : 0U;
        params |= (static_cast<std::uint32_t>(component_.style.filter_mode) & 0x3U) << 3U;
        params |= (static_cast<std::uint32_t>(component_.style.address_u) & 0x3U) << 5U;
        params |= (static_cast<std::uint32_t>(component_.style.address_v) & 0x3U) << 7U;
        params |= (static_cast<std::uint32_t>(component_.style.address_w) & 0x3U) << 9U;
        params |= EncodeRuntimeBlendPresetBits(blend_preset, surface3d_runtime_blend_shift);
        return params;
    }

    [[nodiscard]] static std::uint32_t FloatBits(float value_) noexcept {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value_, sizeof(bits));
        return bits;
    }

    static constexpr std::uint64_t k_hash_prime = 1099511628211ULL;

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= k_hash_prime;
    }

    [[nodiscard]] static bool IsBuildConfigEqual(const Surface3DRuntimeBuildConfig& lhs_,
                                                 const Surface3DRuntimeBuildConfig& rhs_) noexcept {
        return lhs_.build_ordered_indices == rhs_.build_ordered_indices;
    }

    [[nodiscard]] static CacheProbeResult ProbeCache(const Surface3DRuntimeCache& cache_,
                                                     const SurfaceType* components_,
                                                     const TransformType* transforms_,
                                                     std::uint32_t component_count_,
                                                     std::uint64_t surface_signature_,
                                                     std::uint64_t visible_set_signature_,
                                                     std::uint32_t candidate_component_count_,
                                                     const Surface3DRuntimeBuildConfig& build_config_) noexcept {
        if (!cache_.valid) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::cold_start
            };
        }
        if (cache_.components != components_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::components_pointer_changed
            };
        }
        if (cache_.transforms != transforms_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::transforms_pointer_changed
            };
        }
        if (cache_.component_count != component_count_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::component_count_changed
            };
        }
        if (cache_.surface_signature != surface_signature_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::surface_signature_changed
            };
        }
        if (cache_.visible_set_signature != visible_set_signature_ ||
            cache_.candidate_component_count != candidate_component_count_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::visibility_signature_changed
            };
        }
        if (!IsBuildConfigEqual(cache_.build_config, build_config_)) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = SurfaceRuntimeCacheMissReason::build_config_changed
            };
        }
        return CacheProbeResult{
            .key_matched = true,
            .miss_reason = SurfaceRuntimeCacheMissReason::none
        };
    }

    [[nodiscard]] static std::uint64_t ComputeVisibleSetSignature(
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_component_count_,
        bool use_candidate_indices_) noexcept {
        if (!use_candidate_indices_) {
            return 0U;
        }

        std::uint64_t hash = 0xa69d6f2c3e4b5871ULL;
        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            HashCombine(hash, 0xffffffffffffffffULL);
            return hash;
        }

        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            HashCombine(hash, static_cast<std::uint64_t>(candidate_component_indices_[i]));
        }
        return hash;
    }

    static void HashSurfaceComponent(std::uint64_t& hash_,
                                     const SurfaceType& component_) noexcept {
        const AppearanceRuntimeBridge3D appearance_bridge =
            ReadAppearanceRuntimeBridge3D(component_.runtime);
        const bool has_linked_appearance =
            HasLinkedAppearanceHandle(component_.runtime.route);
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.route.visible));
        if (component_.runtime.route.visible == 0U) {
            return;
        }

        HashCombine(hash_, component_.runtime.route.sort_key);
        HashCombine(hash_,
                    static_cast<std::uint64_t>(ResolveEffectiveVisualResourceId(component_.runtime.route)));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.route.user_data));
        if (!has_linked_appearance) {
            HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.route.surface_id));
            HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.source.surface_id));
            HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.source.sampler_id));
            HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.source.uv_set));
            HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.source.flags));
            HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.source_revision));
        }
        HashCombine(hash_, static_cast<std::uint64_t>(PackRgba8(appearance_bridge.base_color)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.style.uv_scale_u)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.style.uv_scale_v)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.style.uv_bias_u)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.style.uv_bias_v)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(appearance_bridge.opacity)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(appearance_bridge.metallic)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(appearance_bridge.roughness)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(appearance_bridge.normal_scale)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(appearance_bridge.occlusion_strength)));
        HashCombine(hash_, static_cast<std::uint64_t>(appearance_bridge.state_flags));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.style.filter_mode));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.style.address_u));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.style.address_v));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.style.address_w));
    }

    [[nodiscard]] static std::uint64_t ComputeSurfaceSignature(const SurfaceType* components_,
                                                               std::uint32_t component_count_,
                                                               const std::uint32_t* candidate_component_indices_,
                                                               std::uint32_t candidate_component_count_,
                                                               bool use_candidate_indices_) noexcept {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        if (components_ == nullptr || component_count_ == 0U) {
            return hash;
        }

        if (!use_candidate_indices_) {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                HashSurfaceComponent(hash, components_[i]);
            }
            return hash;
        }

        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            return hash;
        }

        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            const std::uint32_t component_index = candidate_component_indices_[i];
            if (component_index >= component_count_) {
                continue;
            }
            HashSurfaceComponent(hash, components_[component_index]);
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeTransformSignature(const SurfaceType* components_,
                                                                 const TransformType* transforms_,
                                                                 std::uint32_t component_count_,
                                                                 const std::uint32_t* candidate_component_indices_,
                                                                 std::uint32_t candidate_component_count_,
                                                                 bool use_candidate_indices_) noexcept {
        if (transforms_ == nullptr || components_ == nullptr) {
            return 0U;
        }

        std::uint64_t hash = 0xcbf29ce484222325ULL;
        if (!use_candidate_indices_) {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                if (components_[i].runtime.route.visible == 0U) {
                    continue;
                }
                HashCombine(hash, static_cast<std::uint64_t>(transforms_[i].runtime.world_revision));
            }
            return hash;
        }

        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            return hash;
        }

        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            const std::uint32_t component_index = candidate_component_indices_[i];
            if (component_index >= component_count_) {
                continue;
            }
            if (components_[component_index].runtime.route.visible == 0U) {
                continue;
            }
            HashCombine(hash, static_cast<std::uint64_t>(transforms_[component_index].runtime.world_revision));
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeSurfaceSignature(const SurfaceType* components_,
                                                               std::uint32_t component_count_) noexcept {
        return ComputeSurfaceSignature(components_,
                                       component_count_,
                                       nullptr,
                                       component_count_,
                                       false);
    }

    [[nodiscard]] static std::uint64_t ComputeTransformSignature(const SurfaceType* components_,
                                                                 const TransformType* transforms_,
                                                                 std::uint32_t component_count_) noexcept {
        return ComputeTransformSignature(components_,
                                         transforms_,
                                         component_count_,
                                         nullptr,
                                         component_count_,
                                         false);
    }

    static void WriteWorldToInstance(Surface3DGpuInstance& instance_,
                                     const Matrix4x4& world_matrix_) noexcept {
        instance_.world_m00 = world_matrix_.m[0];
        instance_.world_m01 = world_matrix_.m[1];
        instance_.world_m02 = world_matrix_.m[2];
        instance_.world_m03 = world_matrix_.m[3];
        instance_.world_m10 = world_matrix_.m[4];
        instance_.world_m11 = world_matrix_.m[5];
        instance_.world_m12 = world_matrix_.m[6];
        instance_.world_m13 = world_matrix_.m[7];
        instance_.world_m20 = world_matrix_.m[8];
        instance_.world_m21 = world_matrix_.m[9];
        instance_.world_m22 = world_matrix_.m[10];
        instance_.world_m23 = world_matrix_.m[11];
        instance_.world_m30 = world_matrix_.m[12];
        instance_.world_m31 = world_matrix_.m[13];
        instance_.world_m32 = world_matrix_.m[14];
        instance_.world_m33 = world_matrix_.m[15];
    }

    [[nodiscard]] static std::uint32_t ReadWorldRevision(const TransformType* transforms_,
                                                         std::uint32_t component_count_,
                                                         std::uint32_t component_index_) noexcept {
        if (transforms_ == nullptr || component_index_ >= component_count_) {
            return 0U;
        }
        return transforms_[component_index_].runtime.world_revision;
    }

    static void InitializeInstanceWorldRevisionCache(SurfaceRuntimeMcVector<std::uint32_t>& revisions_,
                                                     const SurfaceRuntimeMcVector<Surface3DGpuInstance>& instances_,
                                                     const TransformType* transforms_,
                                                     std::uint32_t component_count_) {
        revisions_.resize(instances_.size());
        if (transforms_ == nullptr) {
            std::fill(revisions_.begin(), revisions_.end(), 0U);
            return;
        }
        for (std::size_t i = 0U; i < instances_.size(); ++i) {
            const std::uint32_t component_index = instances_[i].component_index;
            revisions_[i] = (component_index < component_count_)
                ? transforms_[component_index].runtime.world_revision
                : 0U;
        }
    }

    [[nodiscard]] static std::uint32_t UpdateInstanceWorldMatrices(
        SurfaceRuntimeMcVector<Surface3DGpuInstance>& instances_,
        SurfaceRuntimeMcVector<std::uint32_t>& cached_revisions_,
        const SurfaceRuntimeMcVector<std::uint32_t>& component_to_instance_index_,
        const TransformType* transforms_,
        std::uint32_t component_count_,
        const std::uint32_t* transform_dirty_component_indices_,
        std::uint32_t transform_dirty_component_count_,
        bool& used_dirty_hint_) {
        used_dirty_hint_ = false;
        if (cached_revisions_.size() != instances_.size()) {
            InitializeInstanceWorldRevisionCache(cached_revisions_,
                                                 instances_,
                                                 transforms_,
                                                 component_count_);
        }

        const Matrix4x4 identity = spatial_math::IdentityMatrix4x4();
        const bool has_transforms = transforms_ != nullptr;
        std::uint32_t rewritten_count = 0U;
        if (transform_dirty_component_indices_ != nullptr &&
            transform_dirty_component_count_ > 0U &&
            component_to_instance_index_.size() == static_cast<std::size_t>(component_count_)) {
            used_dirty_hint_ = true;
            if (transform_dirty_component_count_ == 1U) {
                const std::uint32_t component_index = transform_dirty_component_indices_[0U];
                if (component_index >= component_count_) {
                    return 0U;
                }
                const std::uint32_t instance_index = component_to_instance_index_[component_index];
                if (instance_index == invalid_instance_index || instance_index >= instances_.size()) {
                    return 0U;
                }

                const std::uint32_t current_revision = has_transforms
                    ? transforms_[component_index].runtime.world_revision
                    : 0U;
                if (cached_revisions_[instance_index] == current_revision) {
                    return 0U;
                }

                Surface3DGpuInstance& instance = instances_[instance_index];
                if (has_transforms) {
                    WriteWorldToInstance(instance,
                                         transforms_[component_index].runtime.world_matrix);
                } else {
                    WriteWorldToInstance(instance, identity);
                }
                cached_revisions_[instance_index] = current_revision;
                return 1U;
            }
            for (std::uint32_t i = 0U; i < transform_dirty_component_count_; ++i) {
                const std::uint32_t component_index = transform_dirty_component_indices_[i];
                if (component_index >= component_count_) {
                    continue;
                }

                const std::uint32_t instance_index = component_to_instance_index_[component_index];
                if (instance_index == invalid_instance_index || instance_index >= instances_.size()) {
                    continue;
                }

                Surface3DGpuInstance& instance = instances_[instance_index];
                const std::uint32_t current_revision = has_transforms
                    ? transforms_[component_index].runtime.world_revision
                    : 0U;
                if (cached_revisions_[instance_index] == current_revision) {
                    continue;
                }

                if (has_transforms) {
                    WriteWorldToInstance(instance,
                                         transforms_[component_index].runtime.world_matrix);
                } else {
                    WriteWorldToInstance(instance, identity);
                }
                cached_revisions_[instance_index] = current_revision;
                ++rewritten_count;
            }
            return rewritten_count;
        }

        if (has_transforms) {
            for (std::size_t index = 0U; index < instances_.size(); ++index) {
                Surface3DGpuInstance& instance = instances_[index];
                const std::uint32_t component_index = instance.component_index;
                const bool valid_component_index = component_index < component_count_;
                const std::uint32_t current_revision = valid_component_index
                    ? transforms_[component_index].runtime.world_revision
                    : 0U;
                if (cached_revisions_[index] == current_revision) {
                    continue;
                }

                if (valid_component_index) {
                    WriteWorldToInstance(instance,
                                         transforms_[component_index].runtime.world_matrix);
                } else {
                    WriteWorldToInstance(instance, identity);
                }
                cached_revisions_[index] = current_revision;
                ++rewritten_count;
            }
            return rewritten_count;
        }

        for (std::size_t index = 0U; index < instances_.size(); ++index) {
            if (cached_revisions_[index] == 0U) {
                continue;
            }
            WriteWorldToInstance(instances_[index], identity);
            cached_revisions_[index] = 0U;
            ++rewritten_count;
        }
        return rewritten_count;
    }

    static void AppendOrMergeBatch(const SurfaceType& component_,
                                   std::uint64_t sort_key_,
                                   std::uint32_t component_index_,
                                   std::uint32_t instance_index_,
                                   ScratchType& scratch_) {
        const std::uint32_t params = PackParams(component_);
        const std::uint32_t effective_visual_resource_id =
            ResolveEffectiveVisualResourceId(component_.runtime.route);
        if (!scratch_.draw_batches.empty()) {
            Surface3DDrawBatch& last = scratch_.draw_batches.back();
            if (last.sort_key == sort_key_ &&
                last.effective_visual_resource_id == effective_visual_resource_id &&
                last.params == params &&
                last.instance_begin + last.instance_count == instance_index_) {
                ++last.instance_count;
                return;
            }
        }

        Surface3DDrawBatch batch{};
        batch.sort_key = sort_key_;
        batch.instance_begin = instance_index_;
        batch.instance_count = 1U;
        batch.effective_visual_resource_id = effective_visual_resource_id;
        batch.first_component_index = component_index_;
        batch.params = params;
        scratch_.draw_batches.push_back(batch);
    }
};

} // namespace vr::ecs

