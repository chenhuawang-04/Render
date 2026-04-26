#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/text_system.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace vr::ecs {

template<typename T>
using EcsMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct TextBatchItem final {
    std::uint64_t sort_key;
    std::uint32_t component_index;
    std::uint32_t reserved0;
};

struct TextBatchBuildStats final {
    std::uint32_t total_count;
    std::uint32_t visible_count;
    std::uint32_t hidden_count;
    std::uint32_t empty_count;
};

static_assert(PurePodComponent<TextBatchItem>);
static_assert(PurePodComponent<TextBatchBuildStats>);

template<DimensionTag DimensionT>
struct TextBatchScratch final {
    EcsMcVector<TextBatchItem> visible_items{};
    EcsMcVector<TextBatchItem> radix_scratch{};
    EcsMcVector<std::uint32_t> ordered_indices{};
};

template<DimensionTag DimensionT>
class TextBatchSystem final {
public:
    using TextType = Text<DimensionT>;
    using TextSystemType = TextSystem<DimensionT>;
    using ScratchType = TextBatchScratch<DimensionT>;

    static constexpr std::uint32_t radix_bits_per_pass = 8U;
    static constexpr std::uint32_t radix_bucket_count = 1U << radix_bits_per_pass;
    static constexpr std::uint32_t radix_bucket_mask = radix_bucket_count - 1U;
    static constexpr std::uint32_t radix_pass_count = 64U / radix_bits_per_pass;

    static_assert((64U % radix_bits_per_pass) == 0U,
                  "TextBatchSystem radix_bits_per_pass must divide 64");

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

    [[nodiscard]] static TextBatchBuildStats BuildVisibleItems(const TextType* components_,
                                                               std::uint32_t component_count_,
                                                               ScratchType& scratch_,
                                                               bool build_ordered_indices_ = false) {
        TextBatchBuildStats stats{};
        stats.total_count = component_count_;
        stats.visible_count = 0U;
        stats.hidden_count = 0U;
        stats.empty_count = 0U;

        scratch_.visible_items.clear();
        scratch_.ordered_indices.clear();

        if (components_ == nullptr || component_count_ == 0U) {
            return stats;
        }

        Reserve(scratch_, component_count_);

        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            const TextType& component = components_[i];
            if (component.text.size_bytes == 0U) {
                ++stats.empty_count;
                continue;
            }
            if (component.runtime.visible == 0U) {
                ++stats.hidden_count;
                continue;
            }

            scratch_.visible_items.emplace_back(TextBatchItem{
                .sort_key = component.runtime.sort_key,
                .component_index = i,
                .reserved0 = 0U
            });
        }

        stats.visible_count = static_cast<std::uint32_t>(scratch_.visible_items.size());

        if (build_ordered_indices_) {
            BuildOrderedIndices(scratch_);
        }
        return stats;
    }

    [[nodiscard]] static TextBatchBuildStats BuildAndSort(const TextType* components_,
                                                          std::uint32_t component_count_,
                                                          ScratchType& scratch_,
                                                          bool build_ordered_indices_ = true) {
        TextBatchBuildStats stats = BuildVisibleItems(components_,
                                                      component_count_,
                                                      scratch_,
                                                      false);
        if (stats.visible_count > 1U) {
            RadixSortBySortKey(scratch_);
        }

        if (build_ordered_indices_) {
            BuildOrderedIndices(scratch_);
        }
        return stats;
    }

    [[nodiscard]] static std::uint32_t VisibleCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.visible_items.size());
    }

    [[nodiscard]] static const TextBatchItem* SortedItems(const ScratchType& scratch_) noexcept {
        return scratch_.visible_items.data();
    }

    [[nodiscard]] static const std::uint32_t* OrderedIndices(const ScratchType& scratch_) noexcept {
        return scratch_.ordered_indices.data();
    }

    [[nodiscard]] static std::uint32_t OrderedIndexCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.ordered_indices.size());
    }

    template<typename FnT>
    static void ForEachSortedItem(const TextType* components_,
                                  const ScratchType& scratch_,
                                  FnT&& function_) {
        if (components_ == nullptr) {
            return;
        }

        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        for (std::uint32_t i = 0U; i < count; ++i) {
            const TextBatchItem& item = scratch_.visible_items[i];
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
        std::uint64_t current_key = TextSystemType::BindingSortKey(scratch_.visible_items[0U].sort_key);

        for (std::uint32_t i = 1U; i < count; ++i) {
            const std::uint64_t key = TextSystemType::BindingSortKey(scratch_.visible_items[i].sort_key);
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
    static void BuildOrderedIndices(ScratchType& scratch_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        scratch_.ordered_indices.resize(count);

        for (std::uint32_t i = 0U; i < count; ++i) {
            scratch_.ordered_indices[i] = scratch_.visible_items[i].component_index;
        }
    }

    static void RadixSortBySortKey(ScratchType& scratch_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count <= 1U) {
            return;
        }

        scratch_.radix_scratch.resize(count);

        TextBatchItem* src = scratch_.visible_items.data();
        TextBatchItem* dst = scratch_.radix_scratch.data();
        bool src_is_primary = true;

        std::array<std::uint32_t, radix_bucket_count> histogram{};
        std::array<std::uint32_t, radix_bucket_count> offsets{};

        for (std::uint32_t pass_index = 0U; pass_index < radix_pass_count; ++pass_index) {
            histogram.fill(0U);
            const std::uint32_t shift = pass_index * radix_bits_per_pass;

            for (std::uint32_t i = 0U; i < count; ++i) {
                const std::uint32_t bucket = static_cast<std::uint32_t>((src[i].sort_key >> shift) & radix_bucket_mask);
                ++histogram[bucket];
            }

            std::uint32_t prefix = 0U;
            for (std::uint32_t bucket = 0U; bucket < radix_bucket_count; ++bucket) {
                offsets[bucket] = prefix;
                prefix += histogram[bucket];
            }

            for (std::uint32_t i = 0U; i < count; ++i) {
                const std::uint32_t bucket = static_cast<std::uint32_t>((src[i].sort_key >> shift) & radix_bucket_mask);
                dst[offsets[bucket]++] = src[i];
            }

            std::swap(src, dst);
            src_is_primary = !src_is_primary;
        }

        if (!src_is_primary) {
            std::memcpy(scratch_.visible_items.data(),
                        src,
                        static_cast<std::size_t>(count) * sizeof(TextBatchItem));
        }
    }
};

} // namespace vr::ecs
