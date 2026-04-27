#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/surface_runtime_system.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace vr::ecs {

template<typename T>
using SurfaceUploadPlanMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SurfaceUploadPatchRange final {
    std::uint32_t instance_begin;
    std::uint32_t instance_count;
};

struct SurfaceUploadPlanStats final {
    std::uint32_t requested_component_count;
    std::uint32_t resolved_instance_count;
    std::uint32_t dropped_component_count;
    std::uint32_t range_count;
    std::uint32_t covered_instance_count;
    std::uint32_t merged_adjacent_count;
    std::uint32_t merged_gap_instance_count;
    bool used_dense_path;
};

struct SurfaceUploadPlanBuildOptions final {
    std::uint32_t merge_gap_instances;
    std::uint32_t dense_path_min_dirty_count;
    std::uint32_t dense_path_min_coverage_percent;
};

template<DimensionTag DimensionT>
struct SurfaceUploadPlanScratch final {
    SurfaceUploadPlanMcVector<std::uint32_t> instance_indices{};
    SurfaceUploadPlanMcVector<SurfaceUploadPatchRange> ranges{};
    SurfaceUploadPlanMcVector<std::uint8_t> dense_marks{};
};

static_assert(PurePodSurfaceComponent<SurfaceUploadPatchRange>);
static_assert(PurePodSurfaceComponent<SurfaceUploadPlanStats>);

template<DimensionTag DimensionT>
struct SurfaceUploadPlanTraits;

template<>
struct SurfaceUploadPlanTraits<Dim2> final {
    using RuntimeScratchType = Surface2DRuntimeScratch;
};

template<>
struct SurfaceUploadPlanTraits<Dim3> final {
    using RuntimeScratchType = Surface3DRuntimeScratch;
};

template<DimensionTag DimensionT>
class SurfaceUploadPlanSystem final {
public:
    using ScratchType = SurfaceUploadPlanScratch<DimensionT>;
    using TraitsType = SurfaceUploadPlanTraits<DimensionT>;
    using RuntimeScratchType = typename TraitsType::RuntimeScratchType;

    static constexpr std::uint32_t invalid_instance_index = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint32_t max_coverage_percent = 100U;

    [[nodiscard]] static constexpr SurfaceUploadPlanBuildOptions DefaultBuildOptions() noexcept {
        return SurfaceUploadPlanBuildOptions{
            .merge_gap_instances = 0U,
            .dense_path_min_dirty_count = 64U,
            .dense_path_min_coverage_percent = 25U
        };
    }

    static void Reserve(ScratchType& scratch_,
                        std::uint32_t reserve_dirty_component_count_,
                        std::uint32_t reserve_instance_count_ = 0U) {
        const std::size_t dirty_reserve = static_cast<std::size_t>(reserve_dirty_component_count_);
        const std::size_t instance_reserve = static_cast<std::size_t>(reserve_instance_count_);

        if (scratch_.instance_indices.capacity() < dirty_reserve) {
            scratch_.instance_indices.reserve(dirty_reserve);
        }
        if (scratch_.ranges.capacity() < dirty_reserve) {
            scratch_.ranges.reserve(dirty_reserve);
        }
        if (instance_reserve > 0U && scratch_.dense_marks.capacity() < instance_reserve) {
            scratch_.dense_marks.reserve(instance_reserve);
        }
    }

    [[nodiscard]] static SurfaceUploadPlanStats BuildRangesFromDirtyComponents(
        const RuntimeScratchType& runtime_scratch_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        ScratchType& scratch_) {
        return BuildRangesFromDirtyComponents(runtime_scratch_,
                                              dirty_component_indices_,
                                              dirty_component_count_,
                                              DefaultBuildOptions(),
                                              scratch_);
    }

    [[nodiscard]] static SurfaceUploadPlanStats BuildRangesFromDirtyComponents(
        const RuntimeScratchType& runtime_scratch_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        const SurfaceUploadPlanBuildOptions& build_options_,
        ScratchType& scratch_) {
        SurfaceUploadPlanStats stats{};
        stats.requested_component_count = dirty_component_count_;

        scratch_.instance_indices.clear();
        scratch_.ranges.clear();

        if (dirty_component_indices_ == nullptr || dirty_component_count_ == 0U) {
            return stats;
        }

        const auto& component_to_instance = runtime_scratch_.cache.component_to_instance_index;
        const std::uint32_t instance_count =
            static_cast<std::uint32_t>(runtime_scratch_.instances.size());
        if (component_to_instance.empty() || instance_count == 0U) {
            stats.dropped_component_count = dirty_component_count_;
            return stats;
        }

        Reserve(scratch_, dirty_component_count_, instance_count);

        const SurfaceUploadPlanBuildOptions normalized_options =
            NormalizeBuildOptions(build_options_);
        const bool prefer_dense_path =
            (dirty_component_count_ >= normalized_options.dense_path_min_dirty_count) &&
            (static_cast<std::uint64_t>(dirty_component_count_) *
             static_cast<std::uint64_t>(max_coverage_percent) >=
             static_cast<std::uint64_t>(instance_count) *
             static_cast<std::uint64_t>(normalized_options.dense_path_min_coverage_percent));

        if (prefer_dense_path) {
            BuildDenseRanges(component_to_instance,
                             instance_count,
                             dirty_component_indices_,
                             dirty_component_count_,
                             normalized_options.merge_gap_instances,
                             scratch_,
                             stats);
            stats.used_dense_path = true;
            FinalizeStats(scratch_, stats);
            return stats;
        }

        BuildSparseRanges(component_to_instance,
                          instance_count,
                          dirty_component_indices_,
                          dirty_component_count_,
                          normalized_options.merge_gap_instances,
                          scratch_,
                          stats);
        FinalizeStats(scratch_, stats);
        return stats;
    }

    [[nodiscard]] static const SurfaceUploadPatchRange* Ranges(const ScratchType& scratch_) noexcept {
        return scratch_.ranges.data();
    }

    [[nodiscard]] static std::uint32_t RangeCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.ranges.size());
    }

private:
    [[nodiscard]] static SurfaceUploadPlanBuildOptions NormalizeBuildOptions(
        const SurfaceUploadPlanBuildOptions& build_options_) noexcept {
        SurfaceUploadPlanBuildOptions normalized = build_options_;
        if (normalized.dense_path_min_dirty_count == 0U) {
            normalized.dense_path_min_dirty_count = 1U;
        }
        if (normalized.dense_path_min_coverage_percent == 0U) {
            normalized.dense_path_min_coverage_percent = 1U;
        } else if (normalized.dense_path_min_coverage_percent > max_coverage_percent) {
            normalized.dense_path_min_coverage_percent = max_coverage_percent;
        }
        return normalized;
    }

    static void AppendRangeWithGapMerge(
        SurfaceUploadPlanMcVector<SurfaceUploadPatchRange>& ranges_,
        std::uint32_t begin_,
        std::uint32_t count_,
        std::uint32_t merge_gap_instances_) {
        if (count_ == 0U) {
            return;
        }

        const std::uint64_t begin64 = static_cast<std::uint64_t>(begin_);
        const std::uint64_t end64 = begin64 + static_cast<std::uint64_t>(count_);

        if (!ranges_.empty()) {
            SurfaceUploadPatchRange& last = ranges_.back();
            const std::uint64_t last_begin64 = static_cast<std::uint64_t>(last.instance_begin);
            const std::uint64_t last_end64 = last_begin64 +
                                             static_cast<std::uint64_t>(last.instance_count);
            const std::uint64_t merge_limit = last_end64 +
                                              static_cast<std::uint64_t>(merge_gap_instances_);
            if (begin64 <= merge_limit) {
                const std::uint64_t merged_end64 = (end64 > last_end64) ? end64 : last_end64;
                const std::uint64_t merged_count64 = merged_end64 - last_begin64;
                last.instance_count = static_cast<std::uint32_t>(merged_count64);
                return;
            }
        }

        ranges_.push_back(SurfaceUploadPatchRange{
            .instance_begin = begin_,
            .instance_count = count_
        });
    }

    static void FinalizeStats(const ScratchType& scratch_,
                              SurfaceUploadPlanStats& stats_) noexcept {
        stats_.range_count = static_cast<std::uint32_t>(scratch_.ranges.size());
        stats_.merged_adjacent_count = (stats_.resolved_instance_count > stats_.range_count)
            ? (stats_.resolved_instance_count - stats_.range_count)
            : 0U;

        std::uint64_t covered64 = 0U;
        for (const SurfaceUploadPatchRange& range : scratch_.ranges) {
            covered64 += static_cast<std::uint64_t>(range.instance_count);
        }
        stats_.covered_instance_count = static_cast<std::uint32_t>(covered64);
        stats_.merged_gap_instance_count = (stats_.covered_instance_count > stats_.resolved_instance_count)
            ? (stats_.covered_instance_count - stats_.resolved_instance_count)
            : 0U;
    }

    static void BuildDenseRanges(
        const SurfaceRuntimeMcVector<std::uint32_t>& component_to_instance_,
        std::uint32_t instance_count_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        std::uint32_t merge_gap_instances_,
        ScratchType& scratch_,
        SurfaceUploadPlanStats& stats_) {
        scratch_.dense_marks.resize(static_cast<std::size_t>(instance_count_));
        std::fill(scratch_.dense_marks.begin(), scratch_.dense_marks.end(), static_cast<std::uint8_t>(0U));

        for (std::uint32_t i = 0U; i < dirty_component_count_; ++i) {
            const std::uint32_t component_index = dirty_component_indices_[i];
            if (component_index >= component_to_instance_.size()) {
                ++stats_.dropped_component_count;
                continue;
            }

            const std::uint32_t instance_index = component_to_instance_[component_index];
            if (instance_index == invalid_instance_index || instance_index >= instance_count_) {
                ++stats_.dropped_component_count;
                continue;
            }

            if (scratch_.dense_marks[instance_index] == 0U) {
                scratch_.dense_marks[instance_index] = 1U;
                ++stats_.resolved_instance_count;
            }
        }

        if (stats_.resolved_instance_count == 0U) {
            return;
        }

        bool in_range = false;
        std::uint32_t begin = 0U;
        std::uint32_t count = 0U;

        for (std::uint32_t instance_index = 0U; instance_index < instance_count_; ++instance_index) {
            const bool marked = scratch_.dense_marks[instance_index] != 0U;
            if (marked) {
                if (!in_range) {
                    begin = instance_index;
                    count = 1U;
                    in_range = true;
                } else {
                    ++count;
                }
                continue;
            }

            if (!in_range) {
                continue;
            }

            AppendRangeWithGapMerge(scratch_.ranges,
                                    begin,
                                    count,
                                    merge_gap_instances_);
            in_range = false;
        }

        if (in_range) {
            AppendRangeWithGapMerge(scratch_.ranges,
                                    begin,
                                    count,
                                    merge_gap_instances_);
        }
    }

    static void BuildSparseRanges(
        const SurfaceRuntimeMcVector<std::uint32_t>& component_to_instance_,
        std::uint32_t instance_count_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        std::uint32_t merge_gap_instances_,
        ScratchType& scratch_,
        SurfaceUploadPlanStats& stats_) {
        for (std::uint32_t i = 0U; i < dirty_component_count_; ++i) {
            const std::uint32_t component_index = dirty_component_indices_[i];
            if (component_index >= component_to_instance_.size()) {
                ++stats_.dropped_component_count;
                continue;
            }

            const std::uint32_t instance_index = component_to_instance_[component_index];
            if (instance_index == invalid_instance_index || instance_index >= instance_count_) {
                ++stats_.dropped_component_count;
                continue;
            }

            scratch_.instance_indices.push_back(instance_index);
        }

        if (scratch_.instance_indices.empty()) {
            return;
        }

        std::sort(scratch_.instance_indices.begin(), scratch_.instance_indices.end());

        std::size_t unique_size = 0U;
        std::uint32_t previous = invalid_instance_index;
        for (std::size_t i = 0U; i < scratch_.instance_indices.size(); ++i) {
            const std::uint32_t value = scratch_.instance_indices[i];
            if (unique_size == 0U || value != previous) {
                scratch_.instance_indices[unique_size] = value;
                previous = value;
                ++unique_size;
            }
        }
        scratch_.instance_indices.resize(unique_size);
        stats_.resolved_instance_count = static_cast<std::uint32_t>(unique_size);

        std::uint32_t begin = scratch_.instance_indices[0U];
        std::uint32_t previous_index = begin;
        std::uint32_t count = 1U;

        for (std::size_t i = 1U; i < unique_size; ++i) {
            const std::uint32_t current = scratch_.instance_indices[i];
            const std::uint64_t continuation_limit =
                static_cast<std::uint64_t>(previous_index) + 1ULL +
                static_cast<std::uint64_t>(merge_gap_instances_);
            if (static_cast<std::uint64_t>(current) <= continuation_limit) {
                const std::uint64_t new_count64 =
                    static_cast<std::uint64_t>(current) -
                    static_cast<std::uint64_t>(begin) + 1ULL;
                count = static_cast<std::uint32_t>(new_count64);
                previous_index = current;
                continue;
            }

            AppendRangeWithGapMerge(scratch_.ranges,
                                    begin,
                                    count,
                                    merge_gap_instances_);

            begin = current;
            previous_index = current;
            count = 1U;
        }

        AppendRangeWithGapMerge(scratch_.ranges,
                                begin,
                                count,
                                merge_gap_instances_);
    }
};

} // namespace vr::ecs
