#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/surface_system.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace vr::ecs {

template<typename T>
using SurfaceBatchMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SurfaceBatchItem final {
    std::uint64_t sort_key;
    std::uint32_t component_index;
    std::uint32_t reserved0;
};

struct SurfaceBatchBuildStats final {
    std::uint32_t total_count;
    std::uint32_t scanned_count;
    std::uint32_t visible_count;
    std::uint32_t hidden_count;
    std::uint32_t missing_source_count;
    std::uint32_t out_of_range_candidate_count;
    std::uint8_t used_candidate_indices;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

static_assert(PurePodSurfaceComponent<SurfaceBatchItem>);
static_assert(PurePodSurfaceComponent<SurfaceBatchBuildStats>);

template<DimensionTag DimensionT>
struct SurfaceBatchScratch final {
    SurfaceBatchMcVector<SurfaceBatchItem> visible_items{};
    SurfaceBatchMcVector<SurfaceBatchItem> radix_scratch{};
    SurfaceBatchMcVector<std::uint32_t> ordered_indices{};
};

template<DimensionTag DimensionT>
class SurfaceBatchSystem final {
public:
    using SurfaceType = Surface<DimensionT>;
    using SurfaceSystemType = SurfaceSystem<DimensionT>;
    using ScratchType = SurfaceBatchScratch<DimensionT>;

    static constexpr std::uint32_t radix_bits_per_pass = 8U;
    static constexpr std::uint32_t radix_bucket_count = 1U << radix_bits_per_pass;
    static constexpr std::uint32_t radix_bucket_mask = radix_bucket_count - 1U;
    static constexpr std::uint32_t radix_pass_count = 64U / radix_bits_per_pass;

    static_assert((64U % radix_bits_per_pass) == 0U,
                  "SurfaceBatchSystem radix_bits_per_pass must divide 64");

    static void Reserve(ScratchType& scratch_, std::uint32_t max_component_count_) {
        const auto reserve_count = static_cast<std::size_t>(max_component_count_);
        if (scratch_.visible_items.capacity() < reserve_count) {
            scratch_.visible_items.reserve(reserve_count);
        }
        if (scratch_.radix_scratch.capacity() < reserve_count) {
            scratch_.radix_scratch.reserve(reserve_count);
        }
        if (scratch_.ordered_indices.capacity() < reserve_count) {
            scratch_.ordered_indices.reserve(reserve_count);
        }
    }

    [[nodiscard]] static SurfaceBatchBuildStats BuildVisibleItems(const SurfaceType* components_,
                                                                  std::uint32_t component_count_,
                                                                  ScratchType& scratch_,
                                                                  bool build_ordered_indices_ = false) {
        return BuildVisibleItemsInternal(components_,
                                         component_count_,
                                         nullptr,
                                         0U,
                                         false,
                                         scratch_,
                                         build_ordered_indices_);
    }

    [[nodiscard]] static SurfaceBatchBuildStats BuildVisibleItemsFromCandidates(
        const SurfaceType* components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_count_,
        ScratchType& scratch_,
        bool build_ordered_indices_ = false) {
        return BuildVisibleItemsInternal(components_,
                                         component_count_,
                                         candidate_component_indices_,
                                         candidate_count_,
                                         true,
                                         scratch_,
                                         build_ordered_indices_);
    }

    [[nodiscard]] static SurfaceBatchBuildStats BuildAndSort(const SurfaceType* components_,
                                                             std::uint32_t component_count_,
                                                             ScratchType& scratch_,
                                                             bool build_ordered_indices_ = true) {
        SurfaceBatchBuildStats stats = BuildVisibleItemsInternal(components_,
                                                                 component_count_,
                                                                 nullptr,
                                                                 0U,
                                                                 false,
                                                                 scratch_,
                                                                 false);
        if (stats.visible_count > 1U) {
            SortVisibleItemsBySortKey(scratch_);
        }

        if (build_ordered_indices_) {
            BuildOrderedIndices(scratch_);
        }
        return stats;
    }

    [[nodiscard]] static SurfaceBatchBuildStats BuildAndSortFromCandidates(
        const SurfaceType* components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_count_,
        ScratchType& scratch_,
        bool build_ordered_indices_ = true) {
        SurfaceBatchBuildStats stats = BuildVisibleItemsInternal(components_,
                                                                 component_count_,
                                                                 candidate_component_indices_,
                                                                 candidate_count_,
                                                                 true,
                                                                 scratch_,
                                                                 false);
        if (stats.visible_count > 1U) {
            SortVisibleItemsBySortKey(scratch_);
        }

        if (build_ordered_indices_) {
            BuildOrderedIndices(scratch_);
        }
        return stats;
    }

    [[nodiscard]] static std::uint32_t VisibleCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.visible_items.size());
    }

    [[nodiscard]] static const SurfaceBatchItem* SortedItems(const ScratchType& scratch_) noexcept {
        return scratch_.visible_items.data();
    }

    [[nodiscard]] static const std::uint32_t* OrderedIndices(const ScratchType& scratch_) noexcept {
        return scratch_.ordered_indices.data();
    }

    [[nodiscard]] static std::uint32_t OrderedIndexCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.ordered_indices.size());
    }

    template<typename FnT>
    static void ForEachSortedItem(const SurfaceType* components_,
                                  const ScratchType& scratch_,
                                  FnT&& function_) {
        if (components_ == nullptr) {
            return;
        }

        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        for (std::uint32_t i = 0U; i < count; ++i) {
            const SurfaceBatchItem& item = scratch_.visible_items[i];
            function_(item, components_[item.component_index]);
        }
    }

    template<typename FnT>
    static void ForEachSortKeyGroup(const ScratchType& scratch_, FnT&& function_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count == 0U) {
            return;
        }

        std::uint32_t begin = 0U;
        std::uint64_t current_key = scratch_.visible_items[0U].sort_key;

        for (std::uint32_t i = 1U; i < count; ++i) {
            const std::uint64_t key = scratch_.visible_items[i].sort_key;
            if (key == current_key) {
                continue;
            }

            function_(begin, i - begin, current_key);
            begin = i;
            current_key = key;
        }

        function_(begin, count - begin, current_key);
    }

    template<typename FnT>
    static void ForEachBindingGroup(const ScratchType& scratch_, FnT&& function_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count == 0U) {
            return;
        }

        std::uint32_t begin = 0U;
        std::uint64_t current_key = SurfaceSystemType::BindingSortKey(scratch_.visible_items[0U].sort_key);

        for (std::uint32_t i = 1U; i < count; ++i) {
            const std::uint64_t key = SurfaceSystemType::BindingSortKey(scratch_.visible_items[i].sort_key);
            if (key == current_key) {
                continue;
            }

            function_(begin, i - begin, current_key);
            begin = i;
            current_key = key;
        }

        function_(begin, count - begin, current_key);
    }

private:
    [[nodiscard]] static SurfaceBatchBuildStats BuildVisibleItemsInternal(
        const SurfaceType* components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_count_,
        bool use_candidate_indices_,
        ScratchType& scratch_,
        bool build_ordered_indices_) {
        SurfaceBatchBuildStats stats{};
        stats.total_count = component_count_;
        stats.scanned_count = use_candidate_indices_ ? candidate_count_ : component_count_;
        stats.visible_count = 0U;
        stats.hidden_count = 0U;
        stats.missing_source_count = 0U;
        stats.out_of_range_candidate_count = 0U;
        stats.used_candidate_indices = use_candidate_indices_ ? 1U : 0U;
        stats.reserved0 = 0U;
        stats.reserved1 = 0U;

        scratch_.visible_items.clear();
        scratch_.ordered_indices.clear();

        if (components_ == nullptr || component_count_ == 0U) {
            return stats;
        }

        Reserve(scratch_, component_count_);

        if (use_candidate_indices_) {
            if (candidate_component_indices_ == nullptr) {
                stats.out_of_range_candidate_count = candidate_count_;
            } else {
                for (std::uint32_t i = 0U; i < candidate_count_; ++i) {
                    const std::uint32_t component_index = candidate_component_indices_[i];
                    if (component_index >= component_count_) {
                        ++stats.out_of_range_candidate_count;
                        continue;
                    }

                    const SurfaceType& component = components_[component_index];
                    if (component.runtime.route.visible == 0U) {
                        ++stats.hidden_count;
                        continue;
                    }
                    if (!SurfaceSystemType::IsVisibleForBatch(component)) {
                        ++stats.missing_source_count;
                        continue;
                    }

                    scratch_.visible_items.emplace_back(SurfaceBatchItem{
                        .sort_key = component.runtime.route.sort_key,
                        .component_index = component_index,
                        .reserved0 = 0U
                    });
                }
            }
        } else {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                const SurfaceType& component = components_[i];
                if (component.runtime.route.visible == 0U) {
                    ++stats.hidden_count;
                    continue;
                }
                if (!SurfaceSystemType::IsVisibleForBatch(component)) {
                    ++stats.missing_source_count;
                    continue;
                }

                scratch_.visible_items.emplace_back(SurfaceBatchItem{
                    .sort_key = component.runtime.route.sort_key,
                    .component_index = i,
                    .reserved0 = 0U
                });
            }
        }

        stats.visible_count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (build_ordered_indices_) {
            BuildOrderedIndices(scratch_);
        }
        return stats;
    }

    static void BuildOrderedIndices(ScratchType& scratch_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        scratch_.ordered_indices.resize(count);

        for (std::uint32_t i = 0U; i < count; ++i) {
            scratch_.ordered_indices[i] = scratch_.visible_items[i].component_index;
        }
    }

    static void SortVisibleItemsBySortKey(ScratchType& scratch_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count <= 1U) {
            return;
        }
        if (TryInsertionSortIfNearlySorted(scratch_)) {
            return;
        }
        RadixSortBySortKey(scratch_);
    }

    [[nodiscard]] static bool TryInsertionSortIfNearlySorted(ScratchType& scratch_) {
        constexpr std::uint32_t k_max_adjacent_descents = 8U;

        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count <= 1U) {
            return true;
        }

        SurfaceBatchItem* items = scratch_.visible_items.data();
        std::uint32_t descents = 0U;
        for (std::uint32_t i = 1U; i < count; ++i) {
            if (items[i - 1U].sort_key <= items[i].sort_key) {
                continue;
            }
            ++descents;
            if (descents > k_max_adjacent_descents) {
                return false;
            }
        }
        if (descents == 0U) {
            return true;
        }

        for (std::uint32_t i = 1U; i < count; ++i) {
            const SurfaceBatchItem item = items[i];
            if (items[i - 1U].sort_key <= item.sort_key) {
                continue;
            }

            std::uint32_t j = i;
            while (j > 0U && items[j - 1U].sort_key > item.sort_key) {
                items[j] = items[j - 1U];
                --j;
            }
            items[j] = item;
        }
        return true;
    }

    static void RadixSortBySortKey(ScratchType& scratch_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count <= 1U) {
            return;
        }

        scratch_.radix_scratch.resize(count);

        SurfaceBatchItem* src = scratch_.visible_items.data();
        SurfaceBatchItem* dst = scratch_.radix_scratch.data();
        bool src_is_primary = true;

        std::array<std::uint32_t, radix_bucket_count> histogram{};
        std::array<std::uint32_t, radix_bucket_count> offsets{};

        for (std::uint32_t pass_index = 0U; pass_index < radix_pass_count; ++pass_index) {
            histogram.fill(0U);
            const std::uint32_t shift = pass_index * radix_bits_per_pass;

            for (std::uint32_t i = 0U; i < count; ++i) {
                const std::uint32_t bucket = static_cast<std::uint32_t>((src[i].sort_key >> shift) &
                                                                         radix_bucket_mask);
                ++histogram[bucket];
            }

            std::uint32_t prefix = 0U;
            for (std::uint32_t bucket = 0U; bucket < radix_bucket_count; ++bucket) {
                offsets[bucket] = prefix;
                prefix += histogram[bucket];
            }

            for (std::uint32_t i = 0U; i < count; ++i) {
                const std::uint32_t bucket = static_cast<std::uint32_t>((src[i].sort_key >> shift) &
                                                                         radix_bucket_mask);
                dst[offsets[bucket]++] = src[i];
            }

            std::swap(src, dst);
            src_is_primary = !src_is_primary;
        }

        if (!src_is_primary) {
            std::memcpy(scratch_.visible_items.data(),
                        src,
                        static_cast<std::size_t>(count) * sizeof(SurfaceBatchItem));
        }
    }
};

} // namespace vr::ecs

