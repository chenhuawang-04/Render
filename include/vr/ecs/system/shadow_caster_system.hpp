#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/bounds_component.hpp"
#include "vr/ecs/system/shadow_runtime_system.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace vr::ecs {

template<typename T>
using ShadowCasterMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ShadowCasterHeader final {
    std::uint32_t offset;
    std::uint16_t count;
    std::uint16_t flags;
    std::uint32_t shadow_component_index;
    std::uint32_t view_index;
};

struct ShadowCasterBuildConfig final {
    std::uint16_t max_casters_per_view = 2048U;
    std::uint8_t stable_sort = 1U;
    std::uint8_t include_hidden_shadows = 0U;
};

struct ShadowCasterBuildStats final {
    std::uint32_t shadow_view_count = 0U;
    std::uint32_t caster_input_count = 0U;
    std::uint32_t candidate_caster_count = 0U;
    std::uint32_t accepted_caster_count = 0U;
    std::uint32_t culled_by_mask_count = 0U;
    std::uint32_t culled_by_frustum_count = 0U;
    std::uint32_t overflow_assignment_count = 0U;
    std::uint32_t emitted_index_count = 0U;
    std::uint32_t sorted_header_count = 0U;
    std::uint64_t visible_signature = 0U;
};

template<DimensionTag DimensionT>
struct ShadowCasterScratch final {
    ShadowCasterMcVector<ShadowCasterHeader> headers{};
    ShadowCasterMcVector<std::uint32_t> caster_indices{};
    ShadowCasterMcVector<std::uint32_t> view_counts{};
    ShadowCasterMcVector<std::uint32_t> view_write_offsets{};
    ShadowCasterMcVector<std::uint64_t> caster_sort_keys{};
};


template<DimensionTag DimensionT>
class ShadowCasterSystem final {
public:
    using ShadowType = Shadow<DimensionT>;
    using BoundsType = Bounds<DimensionT>;
    using ScratchType = ShadowCasterScratch<DimensionT>;

    static void Reserve(ScratchType& scratch_,
                        std::uint32_t shadow_view_count_,
                        std::uint32_t caster_count_) {
        const std::size_t view_reserve = static_cast<std::size_t>(shadow_view_count_);
        const std::size_t caster_reserve = static_cast<std::size_t>(caster_count_);
        if (scratch_.headers.capacity() < view_reserve) {
            scratch_.headers.reserve(view_reserve);
        }
        if (scratch_.view_counts.capacity() < view_reserve) {
            scratch_.view_counts.reserve(view_reserve);
        }
        if (scratch_.view_write_offsets.capacity() < view_reserve) {
            scratch_.view_write_offsets.reserve(view_reserve);
        }
        if (scratch_.caster_sort_keys.capacity() < caster_reserve) {
            scratch_.caster_sort_keys.reserve(caster_reserve);
        }
        if (scratch_.caster_indices.capacity() < caster_reserve) {
            scratch_.caster_indices.reserve(caster_reserve);
        }
    }

    [[nodiscard]] static ShadowCasterBuildStats Build(
        const ShadowType* shadow_components_,
        std::uint32_t shadow_component_count_,
        const ShadowViewGpuRecord* shadow_views_,
        std::uint32_t shadow_view_count_,
        const BoundsType* caster_bounds_,
        std::uint32_t caster_count_,
        ScratchType& scratch_,
        const ShadowCasterBuildConfig& build_config_ = {}) {
        ShadowCasterBuildStats stats{};
        stats.shadow_view_count = shadow_view_count_;
        stats.caster_input_count = caster_count_;

        EnsureSizedScratch(scratch_, shadow_view_count_, caster_count_);
        ResetViewBuffers(scratch_, shadow_view_count_);
        scratch_.caster_indices.clear();

        if (shadow_views_ == nullptr || shadow_view_count_ == 0U ||
            caster_bounds_ == nullptr || caster_count_ == 0U ||
            shadow_components_ == nullptr || shadow_component_count_ == 0U) {
            return stats;
        }

        std::uint64_t visible_signature = k_hash_offset_basis;

        for (std::uint32_t caster_index = 0U; caster_index < caster_count_; ++caster_index) {
            const BoundsType& bounds = caster_bounds_[caster_index];
            if (!HasValidBounds(bounds)) {
                ++stats.culled_by_frustum_count;
                continue;
            }
            ++stats.candidate_caster_count;
            scratch_.caster_sort_keys[caster_index] = BuildCasterSortKey(bounds, caster_index);

            for (std::uint32_t view_index = 0U; view_index < shadow_view_count_; ++view_index) {
                const ShadowViewGpuRecord& view_record = shadow_views_[view_index];
                if (view_record.shadow_component_index >= shadow_component_count_) {
                    continue;
                }
                const ShadowType& shadow_component = shadow_components_[view_record.shadow_component_index];
                if (build_config_.include_hidden_shadows == 0U && shadow_component.visibility.visible == 0U) {
                    continue;
                }
                if ((bounds.runtime.visibility_mask & shadow_component.visibility.caster_mask) == 0U) {
                    ++stats.culled_by_mask_count;
                    continue;
                }
                if (!IntersectsView(bounds, view_record)) {
                    ++stats.culled_by_frustum_count;
                    continue;
                }

                if (scratch_.view_counts[view_index] >= build_config_.max_casters_per_view) {
                    ++stats.overflow_assignment_count;
                    continue;
                }
                ++scratch_.view_counts[view_index];
                ++stats.accepted_caster_count;
                HashCombine(visible_signature,
                            (static_cast<std::uint64_t>(view_index) << 32U) |
                                static_cast<std::uint64_t>(caster_index));
            }
        }

        std::uint32_t running_offset = 0U;
        for (std::uint32_t view_index = 0U; view_index < shadow_view_count_; ++view_index) {
            const ShadowViewGpuRecord& view_record = shadow_views_[view_index];
            const std::uint32_t count = scratch_.view_counts[view_index];
            scratch_.headers[view_index].offset = running_offset;
            scratch_.headers[view_index].count = static_cast<std::uint16_t>(
                std::min<std::uint32_t>(count, (std::numeric_limits<std::uint16_t>::max)()));
            scratch_.headers[view_index].flags = 0U;
            scratch_.headers[view_index].shadow_component_index = view_record.shadow_component_index;
            scratch_.headers[view_index].view_index = view_index;
            scratch_.view_write_offsets[view_index] = running_offset;
            running_offset += count;
        }

        scratch_.caster_indices.resize(static_cast<std::size_t>(running_offset));
        if (running_offset == 0U) {
            stats.visible_signature = visible_signature;
            return stats;
        }

        for (std::uint32_t caster_index = 0U; caster_index < caster_count_; ++caster_index) {
            const BoundsType& bounds = caster_bounds_[caster_index];
            if (!HasValidBounds(bounds)) {
                continue;
            }
            for (std::uint32_t view_index = 0U; view_index < shadow_view_count_; ++view_index) {
                const ShadowViewGpuRecord& view_record = shadow_views_[view_index];
                if (view_record.shadow_component_index >= shadow_component_count_) {
                    continue;
                }
                const ShadowType& shadow_component = shadow_components_[view_record.shadow_component_index];
                if (build_config_.include_hidden_shadows == 0U && shadow_component.visibility.visible == 0U) {
                    continue;
                }
                if ((bounds.runtime.visibility_mask & shadow_component.visibility.caster_mask) == 0U) {
                    continue;
                }
                if (!IntersectsView(bounds, view_record)) {
                    continue;
                }

                const std::uint32_t header_offset = scratch_.headers[view_index].offset;
                const std::uint32_t header_count = scratch_.headers[view_index].count;
                const std::uint32_t write_offset = scratch_.view_write_offsets[view_index];
                if (write_offset >= header_offset + header_count) {
                    continue;
                }
                scratch_.caster_indices[write_offset] = caster_index;
                scratch_.view_write_offsets[view_index] = write_offset + 1U;
            }
        }

        if (build_config_.stable_sort != 0U) {
            for (std::uint32_t view_index = 0U; view_index < shadow_view_count_; ++view_index) {
                const ShadowCasterHeader& header = scratch_.headers[view_index];
                if (header.count <= 1U) {
                    continue;
                }
                ++stats.sorted_header_count;
                const std::uint32_t begin_index = header.offset;
                const std::uint32_t end_index = begin_index + static_cast<std::uint32_t>(header.count);
                std::sort(scratch_.caster_indices.begin() + begin_index,
                          scratch_.caster_indices.begin() + end_index,
                          [&scratch_](std::uint32_t lhs_, std::uint32_t rhs_) noexcept {
                              const std::uint64_t lhs_key = scratch_.caster_sort_keys[lhs_];
                              const std::uint64_t rhs_key = scratch_.caster_sort_keys[rhs_];
                              if (lhs_key != rhs_key) {
                                  return lhs_key < rhs_key;
                              }
                              return lhs_ < rhs_;
                          });
            }
        }

        stats.emitted_index_count = running_offset;
        stats.visible_signature = visible_signature;
        return stats;
    }

    [[nodiscard]] static const ShadowCasterHeader* Headers(const ScratchType& scratch_) noexcept {
        return scratch_.headers.data();
    }

    [[nodiscard]] static std::uint32_t HeaderCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.headers.size());
    }

    [[nodiscard]] static const std::uint32_t* CasterIndices(const ScratchType& scratch_) noexcept {
        return scratch_.caster_indices.data();
    }

    [[nodiscard]] static std::uint32_t CasterIndexCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.caster_indices.size());
    }

private:
    static constexpr std::uint64_t k_hash_offset_basis = 14695981039346656037ULL;
    static constexpr std::uint64_t k_hash_prime = 1099511628211ULL;
    static constexpr float k_epsilon = 1e-6F;

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= k_hash_prime;
    }

    static void EnsureSizedScratch(ScratchType& scratch_,
                                   std::uint32_t shadow_view_count_,
                                   std::uint32_t caster_count_) {
        Reserve(scratch_, shadow_view_count_, caster_count_);
        scratch_.headers.resize(shadow_view_count_);
        scratch_.view_counts.resize(shadow_view_count_);
        scratch_.view_write_offsets.resize(shadow_view_count_);
        scratch_.caster_sort_keys.resize(caster_count_);
    }

    static void ResetViewBuffers(ScratchType& scratch_, std::uint32_t shadow_view_count_) {
        for (std::uint32_t i = 0U; i < shadow_view_count_; ++i) {
            scratch_.headers[i] = ShadowCasterHeader{};
            scratch_.view_counts[i] = 0U;
            scratch_.view_write_offsets[i] = 0U;
        }
    }

    [[nodiscard]] static bool HasValidBounds(const BoundsType& bounds_) noexcept {
        const bool finite_min = std::isfinite(bounds_.runtime.world_min.x) &&
                                std::isfinite(bounds_.runtime.world_min.y);
        const bool finite_max = std::isfinite(bounds_.runtime.world_max.x) &&
                                std::isfinite(bounds_.runtime.world_max.y);
        if constexpr (std::same_as<DimensionT, Dim3>) {
            if (!std::isfinite(bounds_.runtime.world_min.z) ||
                !std::isfinite(bounds_.runtime.world_max.z)) {
                return false;
            }
        }
        if (!finite_min || !finite_max) {
            return false;
        }
        return bounds_.runtime.world_max.x >= bounds_.runtime.world_min.x &&
               bounds_.runtime.world_max.y >= bounds_.runtime.world_min.y;
    }

    struct Float4Value final {
        float x;
        float y;
        float z;
        float w;
    };

    [[nodiscard]] static Float4Value MultiplyPoint(const Matrix4x4& matrix_,
                                                   float x_,
                                                   float y_,
                                                   float z_,
                                                   float w_ = 1.0F) noexcept {
        return Float4Value{
            .x = matrix_.m[0] * x_ + matrix_.m[4] * y_ + matrix_.m[8] * z_ + matrix_.m[12] * w_,
            .y = matrix_.m[1] * x_ + matrix_.m[5] * y_ + matrix_.m[9] * z_ + matrix_.m[13] * w_,
            .z = matrix_.m[2] * x_ + matrix_.m[6] * y_ + matrix_.m[10] * z_ + matrix_.m[14] * w_,
            .w = matrix_.m[3] * x_ + matrix_.m[7] * y_ + matrix_.m[11] * z_ + matrix_.m[15] * w_,
        };
    }

    [[nodiscard]] static bool IntersectsView(const BoundsType& bounds_,
                                             const ShadowViewGpuRecord& view_record_) noexcept {
        const float center_x = bounds_.runtime.world_center.x;
        const float center_y = bounds_.runtime.world_center.y;
        float center_z = 0.0F;
        if constexpr (std::same_as<DimensionT, Dim3>) {
            center_z = bounds_.runtime.world_center.z;
        }
        const float radius = std::max(0.0F, bounds_.runtime.world_radius);
        const Float4Value clip = MultiplyPoint(view_record_.view_projection_matrix,
                                               center_x,
                                               center_y,
                                               center_z,
                                               1.0F);
        if (std::abs(clip.w) <= k_epsilon) {
            return false;
        }

        const float inv_w = 1.0F / clip.w;
        const float ndc_x = clip.x * inv_w;
        const float ndc_y = clip.y * inv_w;
        const float ndc_z = clip.z * inv_w;

        const float proj_scale_x = std::abs(view_record_.projection_matrix.m[0]);
        const float proj_scale_y = std::abs(view_record_.projection_matrix.m[5]);
        const float proj_scale_z = std::abs(view_record_.projection_matrix.m[10]);
        const float radius_ndc = radius * std::max({proj_scale_x, proj_scale_y, proj_scale_z});

        if (ndc_x + radius_ndc < -1.0F || ndc_x - radius_ndc > 1.0F) {
            return false;
        }
        if (ndc_y + radius_ndc < -1.0F || ndc_y - radius_ndc > 1.0F) {
            return false;
        }
        if (ndc_z + radius_ndc < -1.0F || ndc_z - radius_ndc > 1.0F) {
            return false;
        }
        return true;
    }

    [[nodiscard]] static std::uint64_t BuildCasterSortKey(const BoundsType& bounds_,
                                                          std::uint32_t caster_index_) noexcept {
        const float depth_hint = std::max(0.0F, bounds_.runtime.world_radius);
        const std::uint32_t depth_bucket = static_cast<std::uint32_t>(
            std::min(depth_hint * 2048.0F, static_cast<float>((1U << 20U) - 1U)));
        const std::uint64_t mask_bits = static_cast<std::uint64_t>(bounds_.runtime.visibility_mask & 0xFFFU);
        std::uint64_t key = 0U;
        key |= mask_bits << 52U;
        key |= (static_cast<std::uint64_t>(depth_bucket) & 0xFFFFFULL) << 32U;
        key |= static_cast<std::uint64_t>(caster_index_);
        return key;
    }
};

} // namespace vr::ecs

