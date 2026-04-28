#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/appearance_system.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <limits>

namespace vr::ecs {

template<typename T>
using AppearanceRuntimeMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct AppearancePipelinePolicy final {
    std::uint16_t pipeline_domain_id;
    std::uint16_t pass_id;
    std::uint8_t queue_id;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

struct AppearanceSortPolicy final {
    std::uint8_t queue_bucket;
    std::uint8_t reserved0;
    std::uint16_t default_depth_bucket;
    std::uint16_t tie_breaker_seed;
    std::uint16_t pipeline_bucket_override;
};

struct AppearanceRuntimeBuildConfig final {
    bool force_full_rebuild;
    bool rebuild_keys_even_if_clean;
    std::uint32_t merge_gap;
};

struct AppearanceRuntimeBuildHint final {
    const std::uint32_t* dirty_component_indices;
    std::uint32_t dirty_component_count;
    std::uint8_t use_dirty_component_indices;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

struct AppearanceUploadRange final {
    std::uint32_t begin_index;
    std::uint32_t count;
};

struct AppearanceRuntimeBuildStats final {
    std::uint32_t component_count;
    std::uint32_t scanned_count;
    std::uint32_t visible_count;
    std::uint32_t updated_record_count;
    std::uint32_t updated_key_count;
    std::uint32_t upload_range_count;
    std::uint32_t out_of_range_dirty_count;
    std::uint32_t cache_epoch;
    std::uint8_t full_rebuild;
    std::uint8_t used_dirty_indices;
    std::uint16_t reserved0;
};

template<DimensionTag DimensionT>
struct AppearanceGpuRecord;

template<>
struct alignas(16) AppearanceGpuRecord<Dim2> final {
    std::array<float, 4U> fill_rgba;
    std::array<float, 4U> stroke_rgba;
    std::array<float, 4U> gradient_line;
    std::array<float, 4U> params;
    std::array<std::uint32_t, 4U> misc_u32;
    std::array<std::uint32_t, 4U> textures_u32;
};

template<>
struct alignas(16) AppearanceGpuRecord<Dim3> final {
    std::array<float, 4U> base_rgba;
    std::array<float, 4U> emissive_rgba;
    std::array<float, 4U> material_params;
    std::array<float, 4U> extras;
    std::array<std::uint32_t, 4U> flags_u32;
    std::array<std::uint32_t, 4U> textures0_u32;
    std::array<std::uint32_t, 4U> textures1_u32;
};

static_assert(alignof(AppearanceGpuRecord<Dim2>) == 16U);
static_assert(alignof(AppearanceGpuRecord<Dim3>) == 16U);
static_assert((sizeof(AppearanceGpuRecord<Dim2>) % 16U) == 0U);
static_assert((sizeof(AppearanceGpuRecord<Dim3>) % 16U) == 0U);
static_assert(PurePodAppearanceComponent<AppearanceGpuRecord<Dim2>>);
static_assert(PurePodAppearanceComponent<AppearanceGpuRecord<Dim3>>);
static_assert(PurePodAppearanceComponent<AppearanceUploadRange>);
static_assert(PurePodAppearanceComponent<AppearanceRuntimeBuildStats>);

template<DimensionTag DimensionT>
struct AppearanceRuntimeCache final {
    AppearancePipelinePolicy pipeline_policy;
    AppearanceSortPolicy sort_policy;
    std::uint32_t component_count;
    std::uint32_t epoch;
    bool valid;
};

template<DimensionTag DimensionT>
struct AppearanceRuntimeScratch final {
    AppearanceRuntimeMcVector<AppearanceGpuRecord<DimensionT>> gpu_records{};
    AppearanceRuntimeMcVector<AppearanceUploadRange> upload_ranges{};
    AppearanceRuntimeMcVector<std::uint32_t> dirty_component_indices{};
    AppearanceRuntimeMcVector<std::uint32_t> handle_generations{};
    AppearanceRuntimeCache<DimensionT> cache{};
};

[[nodiscard]] constexpr std::uint32_t NextAppearanceRuntimeEpoch(std::uint32_t current_epoch_) noexcept {
    return (current_epoch_ == (std::numeric_limits<std::uint32_t>::max)())
               ? 1U
               : (current_epoch_ + 1U);
}

template<DimensionTag DimensionT>
class AppearanceRuntimeSystem final {
public:
    using AppearanceType = Appearance<DimensionT>;
    using AppearanceSystemType = AppearanceSystem<DimensionT>;
    using ScratchType = AppearanceRuntimeScratch<DimensionT>;
    using GpuRecordType = AppearanceGpuRecord<DimensionT>;

    // 64-bit sort key layout (MSB -> LSB):
    // [queue:4][layer:16][pipeline_bucket:16][depth:16][tie_breaker:12]
    static constexpr std::uint32_t sort_key_tie_bits = 12U;
    static constexpr std::uint32_t sort_key_depth_bits = 16U;
    static constexpr std::uint32_t sort_key_pipeline_bucket_bits = 16U;
    static constexpr std::uint32_t sort_key_layer_bits = 16U;
    static constexpr std::uint32_t sort_key_queue_bits = 4U;

    static constexpr std::uint32_t sort_key_tie_shift = 0U;
    static constexpr std::uint32_t sort_key_depth_shift = sort_key_tie_shift + sort_key_tie_bits;
    static constexpr std::uint32_t sort_key_pipeline_bucket_shift =
        sort_key_depth_shift + sort_key_depth_bits;
    static constexpr std::uint32_t sort_key_layer_shift =
        sort_key_pipeline_bucket_shift + sort_key_pipeline_bucket_bits;
    static constexpr std::uint32_t sort_key_queue_shift =
        sort_key_layer_shift + sort_key_layer_bits;

    static constexpr std::uint64_t sort_key_tie_mask =
        (std::uint64_t{1U} << sort_key_tie_bits) - 1U;
    static constexpr std::uint64_t sort_key_depth_mask =
        (std::uint64_t{1U} << sort_key_depth_bits) - 1U;
    static constexpr std::uint64_t sort_key_pipeline_bucket_mask =
        (std::uint64_t{1U} << sort_key_pipeline_bucket_bits) - 1U;
    static constexpr std::uint64_t sort_key_layer_mask =
        (std::uint64_t{1U} << sort_key_layer_bits) - 1U;
    static constexpr std::uint64_t sort_key_queue_mask =
        (std::uint64_t{1U} << sort_key_queue_bits) - 1U;

    static_assert(sort_key_queue_bits + sort_key_layer_bits + sort_key_pipeline_bucket_bits +
                      sort_key_depth_bits + sort_key_tie_bits == 64U,
                  "AppearanceRuntimeSystem sort-key bit layout must be exactly 64 bits");

    [[nodiscard]] static constexpr AppearancePipelinePolicy DefaultPipelinePolicy() noexcept {
        return AppearancePipelinePolicy{
            .pipeline_domain_id = 0U,
            .pass_id = 0U,
            .queue_id = 0U,
            .reserved0 = 0U,
            .reserved1 = 0U
        };
    }

    [[nodiscard]] static constexpr AppearanceSortPolicy DefaultSortPolicy() noexcept {
        return AppearanceSortPolicy{
            .queue_bucket = 0U,
            .reserved0 = 0U,
            .default_depth_bucket = 0U,
            .tie_breaker_seed = 0U,
            .pipeline_bucket_override = 0xFFFFU
        };
    }

    [[nodiscard]] static constexpr AppearanceRuntimeBuildConfig DefaultBuildConfig() noexcept {
        return AppearanceRuntimeBuildConfig{
            .force_full_rebuild = false,
            .rebuild_keys_even_if_clean = false,
            .merge_gap = 0U
        };
    }

    [[nodiscard]] static constexpr AppearanceRuntimeBuildHint DefaultBuildHint() noexcept {
        return AppearanceRuntimeBuildHint{
            .dirty_component_indices = nullptr,
            .dirty_component_count = 0U,
            .use_dirty_component_indices = 0U,
            .reserved0 = 0U,
            .reserved1 = 0U
        };
    }

    static void Reserve(ScratchType& scratch_, std::uint32_t component_count_) {
        const std::size_t reserve_count = static_cast<std::size_t>(component_count_);
        if (scratch_.gpu_records.capacity() < reserve_count) {
            scratch_.gpu_records.reserve(reserve_count);
        }
        if (scratch_.dirty_component_indices.capacity() < reserve_count) {
            scratch_.dirty_component_indices.reserve(reserve_count);
        }
        if (scratch_.handle_generations.capacity() < reserve_count) {
            scratch_.handle_generations.reserve(reserve_count);
        }
        if (scratch_.upload_ranges.capacity() < reserve_count) {
            scratch_.upload_ranges.reserve(reserve_count);
        }
    }

    [[nodiscard]] static AppearanceRuntimeBuildStats Build(
        AppearanceType* components_,
        std::uint32_t component_count_,
        ScratchType& scratch_,
        const AppearancePipelinePolicy& pipeline_policy_ = DefaultPipelinePolicy(),
        const AppearanceSortPolicy& sort_policy_ = DefaultSortPolicy(),
        const AppearanceRuntimeBuildConfig& build_config_ = DefaultBuildConfig(),
        const AppearanceRuntimeBuildHint& build_hint_ = DefaultBuildHint()) {
        AppearanceRuntimeBuildStats stats{};
        stats.component_count = component_count_;
        stats.used_dirty_indices = build_hint_.use_dirty_component_indices;

        scratch_.upload_ranges.clear();
        scratch_.dirty_component_indices.clear();

        if (components_ == nullptr || component_count_ == 0U) {
            scratch_.gpu_records.clear();
            scratch_.handle_generations.clear();
            scratch_.cache.valid = false;
            return stats;
        }

        Reserve(scratch_, component_count_);

        const bool policy_changed = !IsPolicyEqual(scratch_.cache.pipeline_policy, pipeline_policy_) ||
                                    !IsSortPolicyEqual(scratch_.cache.sort_policy, sort_policy_);
        const bool component_count_changed = scratch_.cache.component_count != component_count_;
        const bool record_size_changed = scratch_.gpu_records.size() != static_cast<std::size_t>(component_count_);
        const bool force_full_rebuild = build_config_.force_full_rebuild ||
                                        !scratch_.cache.valid ||
                                        policy_changed ||
                                        component_count_changed ||
                                        record_size_changed;

        if (force_full_rebuild) {
            stats.full_rebuild = 1U;
            scratch_.gpu_records.resize(component_count_);
            EnsureHandleGenerations(scratch_.handle_generations, component_count_);
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                UpdateComponentRuntimeAndRecord(components_[i],
                                               i,
                                               scratch_.handle_generations[i],
                                               pipeline_policy_,
                                               sort_policy_,
                                               scratch_.gpu_records[i],
                                               stats);
            }
            stats.scanned_count = component_count_;
            if (component_count_ > 0U) {
                scratch_.upload_ranges.push_back(AppearanceUploadRange{
                    .begin_index = 0U,
                    .count = component_count_
                });
            }
            CommitCache(scratch_.cache, pipeline_policy_, sort_policy_, component_count_);
            stats.cache_epoch = scratch_.cache.epoch;
            stats.upload_range_count = static_cast<std::uint32_t>(scratch_.upload_ranges.size());
            stats.visible_count = CountVisibleComponents(components_, component_count_);
            return stats;
        }

        CollectDirtyIndices(components_,
                            component_count_,
                            build_config_,
                            build_hint_,
                            scratch_,
                            stats);

        stats.scanned_count = static_cast<std::uint32_t>(scratch_.dirty_component_indices.size());
        for (const std::uint32_t component_index : scratch_.dirty_component_indices) {
            if (component_index >= component_count_) {
                ++stats.out_of_range_dirty_count;
                continue;
            }
            UpdateComponentRuntimeAndRecord(components_[component_index],
                                           component_index,
                                           scratch_.handle_generations[component_index],
                                           pipeline_policy_,
                                           sort_policy_,
                                           scratch_.gpu_records[component_index],
                                           stats);
        }

        BuildUploadRangesFromDirtyIndices(scratch_.dirty_component_indices,
                                          build_config_.merge_gap,
                                          scratch_.upload_ranges);

        CommitCache(scratch_.cache, pipeline_policy_, sort_policy_, component_count_);
        stats.cache_epoch = scratch_.cache.epoch;
        stats.upload_range_count = static_cast<std::uint32_t>(scratch_.upload_ranges.size());
        stats.visible_count = CountVisibleComponents(components_, component_count_);
        return stats;
    }

    [[nodiscard]] static const GpuRecordType* GpuRecords(const ScratchType& scratch_) noexcept {
        return scratch_.gpu_records.data();
    }

    [[nodiscard]] static std::uint32_t GpuRecordCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.gpu_records.size());
    }

    [[nodiscard]] static const AppearanceUploadRange* UploadRanges(const ScratchType& scratch_) noexcept {
        return scratch_.upload_ranges.data();
    }

    [[nodiscard]] static std::uint32_t UploadRangeCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.upload_ranges.size());
    }

    [[nodiscard]] static std::uint32_t ExtractSortQueue(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_queue_shift) & sort_key_queue_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractSortLayer(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_layer_shift) & sort_key_layer_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractSortPipelineBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_pipeline_bucket_shift) &
                                          sort_key_pipeline_bucket_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractSortDepth(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_depth_shift) & sort_key_depth_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractSortTieBreaker(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_tie_shift) & sort_key_tie_mask);
    }

private:
    static constexpr std::uint64_t k_hash_offset_basis = 0xcbf29ce484222325ULL;
    static constexpr std::uint64_t k_hash_prime = 1099511628211ULL;

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= k_hash_prime;
    }

    [[nodiscard]] static bool IsPolicyEqual(const AppearancePipelinePolicy& lhs_,
                                            const AppearancePipelinePolicy& rhs_) noexcept {
        return lhs_.pipeline_domain_id == rhs_.pipeline_domain_id &&
               lhs_.pass_id == rhs_.pass_id &&
               lhs_.queue_id == rhs_.queue_id;
    }

    [[nodiscard]] static bool IsSortPolicyEqual(const AppearanceSortPolicy& lhs_,
                                                const AppearanceSortPolicy& rhs_) noexcept {
        return lhs_.queue_bucket == rhs_.queue_bucket &&
               lhs_.default_depth_bucket == rhs_.default_depth_bucket &&
               lhs_.tie_breaker_seed == rhs_.tie_breaker_seed &&
               lhs_.pipeline_bucket_override == rhs_.pipeline_bucket_override;
    }

    static void CommitCache(AppearanceRuntimeCache<DimensionT>& cache_,
                            const AppearancePipelinePolicy& pipeline_policy_,
                            const AppearanceSortPolicy& sort_policy_,
                            std::uint32_t component_count_) {
        cache_.pipeline_policy = pipeline_policy_;
        cache_.sort_policy = sort_policy_;
        cache_.component_count = component_count_;
        cache_.epoch = NextAppearanceRuntimeEpoch(cache_.epoch);
        cache_.valid = true;
    }

    static void EnsureHandleGenerations(AppearanceRuntimeMcVector<std::uint32_t>& generations_,
                                        std::uint32_t component_count_) {
        const std::size_t old_size = generations_.size();
        generations_.resize(component_count_);
        for (std::size_t i = old_size; i < generations_.size(); ++i) {
            generations_[i] = 1U;
        }
        for (std::size_t i = 0U; i < old_size; ++i) {
            if (generations_[i] == 0U) {
                generations_[i] = 1U;
            }
        }
    }

    static void CollectDirtyIndices(const AppearanceType* components_,
                                    std::uint32_t component_count_,
                                    const AppearanceRuntimeBuildConfig& build_config_,
                                    const AppearanceRuntimeBuildHint& build_hint_,
                                    ScratchType& scratch_,
                                    AppearanceRuntimeBuildStats& stats_) {
        EnsureHandleGenerations(scratch_.handle_generations, component_count_);

        const bool use_dirty_indices = build_hint_.use_dirty_component_indices != 0U &&
                                       build_hint_.dirty_component_indices != nullptr &&
                                       build_hint_.dirty_component_count > 0U;
        if (use_dirty_indices) {
            scratch_.dirty_component_indices.reserve(build_hint_.dirty_component_count);
            for (std::uint32_t i = 0U; i < build_hint_.dirty_component_count; ++i) {
                const std::uint32_t component_index = build_hint_.dirty_component_indices[i];
                if (component_index >= component_count_) {
                    ++stats_.out_of_range_dirty_count;
                    continue;
                }
                if (!build_config_.rebuild_keys_even_if_clean &&
                    !AppearanceSystemType::HasDirtyFlags(components_[component_index],
                                                         appearance_dirty_style_flag |
                                                             appearance_dirty_binding_flag)) {
                    continue;
                }
                scratch_.dirty_component_indices.push_back(component_index);
            }

            if (!scratch_.dirty_component_indices.empty()) {
                std::sort(scratch_.dirty_component_indices.begin(),
                          scratch_.dirty_component_indices.end());
                const auto new_end = std::unique(scratch_.dirty_component_indices.begin(),
                                                 scratch_.dirty_component_indices.end());
                scratch_.dirty_component_indices.resize(
                    static_cast<std::size_t>(new_end - scratch_.dirty_component_indices.begin()));
            }
            return;
        }

        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            if (!build_config_.rebuild_keys_even_if_clean &&
                !AppearanceSystemType::HasDirtyFlags(components_[i],
                                                     appearance_dirty_style_flag |
                                                         appearance_dirty_binding_flag)) {
                continue;
            }
            scratch_.dirty_component_indices.push_back(i);
        }
    }

    static void BuildUploadRangesFromDirtyIndices(
        const AppearanceRuntimeMcVector<std::uint32_t>& dirty_component_indices_,
        std::uint32_t merge_gap_,
        AppearanceRuntimeMcVector<AppearanceUploadRange>& out_upload_ranges_) {
        out_upload_ranges_.clear();
        if (dirty_component_indices_.empty()) {
            return;
        }

        std::uint32_t range_begin = dirty_component_indices_[0U];
        std::uint32_t range_end = dirty_component_indices_[0U];

        for (std::size_t i = 1U; i < dirty_component_indices_.size(); ++i) {
            const std::uint32_t index = dirty_component_indices_[i];
            const std::uint32_t max_contiguous =
                range_end + 1U + merge_gap_;
            if (index <= max_contiguous) {
                range_end = index;
                continue;
            }

            out_upload_ranges_.push_back(AppearanceUploadRange{
                .begin_index = range_begin,
                .count = range_end - range_begin + 1U
            });
            range_begin = index;
            range_end = index;
        }

        out_upload_ranges_.push_back(AppearanceUploadRange{
            .begin_index = range_begin,
            .count = range_end - range_begin + 1U
        });
    }

    [[nodiscard]] static std::uint32_t CountVisibleComponents(const AppearanceType* components_,
                                                              std::uint32_t component_count_) noexcept {
        std::uint32_t visible_count = 0U;
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            if (AppearanceSystemType::IsVisibleForBatch(components_[i])) {
                ++visible_count;
            }
        }
        return visible_count;
    }

    static void UpdateComponentRuntimeAndRecord(AppearanceType& component_,
                                                std::uint32_t component_index_,
                                                std::uint32_t generation_,
                                                const AppearancePipelinePolicy& pipeline_policy_,
                                                const AppearanceSortPolicy& sort_policy_,
                                                GpuRecordType& out_record_,
                                                AppearanceRuntimeBuildStats& stats_) {
        const std::uint64_t pipeline_key = ComposePipelineKey(component_, pipeline_policy_);
        const std::uint64_t resource_key = ComposeResourceKey(component_.binding);
        const std::uint64_t sort_key = ComposeSortKey(component_,
                                                      component_index_,
                                                      pipeline_key,
                                                      sort_policy_);

        const bool key_changed = component_.runtime.pipeline_key != pipeline_key ||
                                 component_.runtime.resource_key != resource_key ||
                                 component_.runtime.sort_key != sort_key;
        if (key_changed) {
            ++stats_.updated_key_count;
        }

        AppearanceSystemType::SetRuntimeKeys(component_,
                                             pipeline_key,
                                             resource_key,
                                             sort_key);
        AppearanceSystemType::SetGpuRecordHandle(component_,
                                                 AppearanceHandle{
                                                     .index = component_index_,
                                                     .generation = generation_
                                                 });
        EncodeGpuRecord(component_, out_record_);
        AppearanceSystemType::MarkUploaded(component_);
        ++stats_.updated_record_count;
    }

    [[nodiscard]] static std::uint64_t ComposePipelineKey(const AppearanceType& component_,
                                                          const AppearancePipelinePolicy& pipeline_policy_) noexcept {
        std::uint64_t key = 0U;

        // [dim:1][pipeline_domain:12][pass:12][queue:8][blend:4][alpha:2][mode:4][flags:7][reserved:14]
        key |= (std::same_as<DimensionT, Dim3> ? 1ULL : 0ULL) << 63U;
        key |= (static_cast<std::uint64_t>(pipeline_policy_.pipeline_domain_id) & 0xFFFULL) << 51U;
        key |= (static_cast<std::uint64_t>(pipeline_policy_.pass_id) & 0xFFFULL) << 39U;
        key |= (static_cast<std::uint64_t>(pipeline_policy_.queue_id) & 0xFFULL) << 31U;
        key |= (static_cast<std::uint64_t>(component_.style.blend_mode) & 0xFULL) << 27U;
        key |= (static_cast<std::uint64_t>(component_.style.alpha_mode) & 0x3ULL) << 25U;

        if constexpr (std::same_as<DimensionT, Dim2>) {
            key |= (static_cast<std::uint64_t>(component_.style.paint_mode) & 0xFULL) << 21U;
            key |= (static_cast<std::uint64_t>(component_.style.antialiasing & 0x1U)) << 20U;
            key |= (static_cast<std::uint64_t>(component_.style.premultiplied_alpha & 0x1U)) << 19U;
        } else {
            key |= (static_cast<std::uint64_t>(component_.style.shading_model) & 0xFULL) << 21U;
            key |= (static_cast<std::uint64_t>(component_.style.double_sided & 0x1U)) << 20U;
            key |= (static_cast<std::uint64_t>(component_.style.cast_shadow & 0x1U)) << 19U;
            key |= (static_cast<std::uint64_t>(component_.style.receive_shadow & 0x1U)) << 18U;
        }

        return key;
    }

    [[nodiscard]] static std::uint64_t ComposeResourceKey(const AppearanceBinding2D& binding_) noexcept {
        std::uint64_t hash = k_hash_offset_basis;
        HashCombine(hash, static_cast<std::uint64_t>(SceneDimension::dim2));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.texture_base_id));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.texture_mask_id));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.texture_lut_id));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.sampler_state_id));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.binding_layout_id));
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComposeResourceKey(const AppearanceBinding3D& binding_) noexcept {
        std::uint64_t hash = k_hash_offset_basis;
        HashCombine(hash, static_cast<std::uint64_t>(SceneDimension::dim3));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.texture_base_color_id));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.texture_normal_id));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.texture_metal_rough_id));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.texture_occlusion_id));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.texture_emissive_id));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.sampler_state_id));
        HashCombine(hash, static_cast<std::uint64_t>(binding_.binding_layout_id));
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComposeSortKey(const AppearanceType& component_,
                                                      std::uint32_t component_index_,
                                                      std::uint64_t pipeline_key_,
                                                      const AppearanceSortPolicy& sort_policy_) noexcept {
        const std::uint64_t queue_bucket =
            static_cast<std::uint64_t>(sort_policy_.queue_bucket) & sort_key_queue_mask;
        const std::int32_t layer_value = static_cast<std::int32_t>(component_.style.layer);
        const std::uint64_t layer_bucket =
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                layer_value - static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()))) &
            sort_key_layer_mask;
        const std::uint64_t pipeline_bucket = (sort_policy_.pipeline_bucket_override == 0xFFFFU)
            ? (pipeline_key_ & sort_key_pipeline_bucket_mask)
            : (static_cast<std::uint64_t>(sort_policy_.pipeline_bucket_override) &
               sort_key_pipeline_bucket_mask);
        const std::uint64_t depth_bucket =
            static_cast<std::uint64_t>(sort_policy_.default_depth_bucket) & sort_key_depth_mask;
        const std::uint64_t tie_bucket =
            static_cast<std::uint64_t>(component_index_ ^
                                       static_cast<std::uint32_t>(sort_policy_.tie_breaker_seed)) &
            sort_key_tie_mask;

        std::uint64_t sort_key = 0U;
        sort_key |= queue_bucket << sort_key_queue_shift;
        sort_key |= layer_bucket << sort_key_layer_shift;
        sort_key |= pipeline_bucket << sort_key_pipeline_bucket_shift;
        sort_key |= depth_bucket << sort_key_depth_shift;
        sort_key |= tie_bucket << sort_key_tie_shift;
        return sort_key;
    }

    [[nodiscard]] static float ColorChannelToFloat(std::uint8_t channel_) noexcept {
        return static_cast<float>(channel_) * (1.0F / 255.0F);
    }

    [[nodiscard]] static std::uint32_t PackStyleFlags(const AppearanceStyle2D& style_) noexcept {
        std::uint32_t flags = 0U;
        flags |= static_cast<std::uint32_t>(style_.blend_mode) & 0x7U;
        flags |= (static_cast<std::uint32_t>(style_.alpha_mode) & 0x3U) << 3U;
        flags |= (static_cast<std::uint32_t>(style_.paint_mode) & 0x3U) << 5U;
        flags |= (static_cast<std::uint32_t>(style_.antialiasing) & 0x1U) << 7U;
        flags |= (static_cast<std::uint32_t>(style_.premultiplied_alpha) & 0x1U) << 8U;
        return flags;
    }

    [[nodiscard]] static std::uint32_t PackStyleFlags(const AppearanceStyle3D& style_) noexcept {
        std::uint32_t flags = 0U;
        flags |= static_cast<std::uint32_t>(style_.blend_mode) & 0x7U;
        flags |= (static_cast<std::uint32_t>(style_.alpha_mode) & 0x3U) << 3U;
        flags |= (static_cast<std::uint32_t>(style_.shading_model) & 0x3U) << 5U;
        flags |= (static_cast<std::uint32_t>(style_.double_sided) & 0x1U) << 7U;
        flags |= (static_cast<std::uint32_t>(style_.cast_shadow) & 0x1U) << 8U;
        flags |= (static_cast<std::uint32_t>(style_.receive_shadow) & 0x1U) << 9U;
        return flags;
    }

    static void EncodeGpuRecord(const AppearanceType& component_,
                                AppearanceGpuRecord<Dim2>& out_record_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        out_record_.fill_rgba = {
            ColorChannelToFloat(component_.style.fill_color.r),
            ColorChannelToFloat(component_.style.fill_color.g),
            ColorChannelToFloat(component_.style.fill_color.b),
            ColorChannelToFloat(component_.style.fill_color.a)
        };
        out_record_.stroke_rgba = {
            ColorChannelToFloat(component_.style.stroke_color.r),
            ColorChannelToFloat(component_.style.stroke_color.g),
            ColorChannelToFloat(component_.style.stroke_color.b),
            ColorChannelToFloat(component_.style.stroke_color.a)
        };
        out_record_.gradient_line = {
            component_.style.gradient_p0_x,
            component_.style.gradient_p0_y,
            component_.style.gradient_p1_x,
            component_.style.gradient_p1_y
        };
        out_record_.params = {
            component_.style.gradient_radius,
            component_.style.stroke_width_px,
            component_.style.opacity,
            0.0F
        };
        out_record_.misc_u32 = {
            PackStyleFlags(component_.style),
            static_cast<std::uint32_t>(static_cast<std::int32_t>(component_.style.layer)),
            static_cast<std::uint32_t>(component_.runtime.pipeline_key & 0xFFFFFFFFULL),
            static_cast<std::uint32_t>(component_.runtime.resource_key & 0xFFFFFFFFULL)
        };
        out_record_.textures_u32 = {
            component_.binding.texture_base_id,
            component_.binding.texture_mask_id,
            component_.binding.texture_lut_id,
            component_.binding.sampler_state_id
        };
    }

    static void EncodeGpuRecord(const AppearanceType& component_,
                                AppearanceGpuRecord<Dim3>& out_record_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        out_record_.base_rgba = {
            ColorChannelToFloat(component_.style.base_color.r),
            ColorChannelToFloat(component_.style.base_color.g),
            ColorChannelToFloat(component_.style.base_color.b),
            ColorChannelToFloat(component_.style.base_color.a)
        };
        out_record_.emissive_rgba = {
            ColorChannelToFloat(component_.style.emissive_color.r),
            ColorChannelToFloat(component_.style.emissive_color.g),
            ColorChannelToFloat(component_.style.emissive_color.b),
            ColorChannelToFloat(component_.style.emissive_color.a)
        };
        out_record_.material_params = {
            component_.style.metallic,
            component_.style.roughness,
            component_.style.normal_scale,
            component_.style.occlusion_strength
        };
        out_record_.extras = {
            component_.style.emissive_intensity,
            component_.style.alpha_cutoff,
            component_.style.opacity,
            0.0F
        };
        out_record_.flags_u32 = {
            PackStyleFlags(component_.style),
            static_cast<std::uint32_t>(static_cast<std::int32_t>(component_.style.layer)),
            static_cast<std::uint32_t>(component_.runtime.pipeline_key & 0xFFFFFFFFULL),
            static_cast<std::uint32_t>(component_.runtime.resource_key & 0xFFFFFFFFULL)
        };
        out_record_.textures0_u32 = {
            component_.binding.texture_base_color_id,
            component_.binding.texture_normal_id,
            component_.binding.texture_metal_rough_id,
            component_.binding.texture_occlusion_id
        };
        out_record_.textures1_u32 = {
            component_.binding.texture_emissive_id,
            component_.binding.sampler_state_id,
            component_.binding.binding_layout_id,
            0U
        };
    }
};

} // namespace vr::ecs
