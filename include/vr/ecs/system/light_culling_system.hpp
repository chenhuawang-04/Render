#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/system/light_runtime_system.hpp"
#include "vr/ecs/system/spatial_math.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>

namespace vr::ecs {

template<typename T>
using LightCullingMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct LightClusterHeader final {
    std::uint32_t offset;
    std::uint16_t count;
    std::uint16_t flags;
};

struct LightCullingBuildConfig2D final {
    std::uint16_t tile_count_x;
    std::uint16_t tile_count_y;
    std::uint16_t max_lights_per_tile;
    std::uint8_t stable_sort;
    std::uint8_t include_hidden_lights;
    std::uint8_t reserve0;
    std::uint8_t reserve1;
};

struct LightCullingBuildConfig3D final {
    std::uint16_t cluster_count_x;
    std::uint16_t cluster_count_y;
    std::uint16_t cluster_count_z;
    std::uint16_t max_lights_per_cluster;
    float near_plane;
    float far_plane;
    float z_slice_scale;
    float z_slice_bias;
    std::uint8_t stable_sort;
    std::uint8_t include_hidden_lights;
    std::uint8_t reverse_z;
    std::uint8_t reserve0;
};

template<DimensionTag DimensionT>
struct LightCullingBuildConfigTraits;

template<>
struct LightCullingBuildConfigTraits<Dim2> final {
    using ConfigType = LightCullingBuildConfig2D;
};

template<>
struct LightCullingBuildConfigTraits<Dim3> final {
    using ConfigType = LightCullingBuildConfig3D;
};

template<DimensionTag DimensionT>
using LightCullingBuildConfig = typename LightCullingBuildConfigTraits<DimensionT>::ConfigType;

struct LightCullingBuildStats final {
    std::uint32_t input_light_count;
    std::uint32_t candidate_light_count;
    std::uint32_t accepted_light_count;
    std::uint32_t culled_by_visibility_count;
    std::uint32_t culled_by_frustum_count;
    std::uint32_t out_of_range_light_count;
    std::uint32_t overflow_assignment_count;
    std::uint32_t cluster_count;
    std::uint32_t emitted_index_count;
    std::uint32_t stable_sort_cluster_count;
    std::uint64_t camera_signature;
    std::uint64_t culling_config_signature;
    std::uint64_t visible_light_signature;
};

template<DimensionTag DimensionT>
struct LightClusterSpan final {
    std::uint16_t min_x;
    std::uint16_t max_x;
    std::uint16_t min_y;
    std::uint16_t max_y;
    std::uint16_t min_z;
    std::uint16_t max_z;
    std::uint16_t reserve0;
    std::uint16_t reserve1;
    std::uint8_t valid;
    std::uint8_t reserve2;
    std::uint16_t reserve3;
};

template<DimensionTag DimensionT>
struct LightCullingScratch final {
    LightCullingMcVector<LightClusterHeader> headers{};
    LightCullingMcVector<std::uint32_t> light_indices{};
    LightCullingMcVector<std::uint32_t> cluster_counts{};
    LightCullingMcVector<std::uint32_t> cluster_write_offsets{};
    LightCullingMcVector<LightClusterSpan<DimensionT>> light_spans{};
    LightCullingMcVector<std::uint64_t> light_sort_keys{};
};

static_assert(PurePodLightComponent<LightClusterHeader>);
static_assert(PurePodLightComponent<LightCullingBuildConfig2D>);
static_assert(PurePodLightComponent<LightCullingBuildConfig3D>);
static_assert(PurePodLightComponent<LightCullingBuildStats>);
static_assert(PurePodLightComponent<LightClusterSpan<Dim2>>);
static_assert(PurePodLightComponent<LightClusterSpan<Dim3>>);

template<DimensionTag DimensionT>
class LightCullingSystem final {
public:
    using LightType = Light<DimensionT>;
    using CameraType = Camera<DimensionT>;
    using GeomType = LightDerivedGeom<DimensionT>;
    using ConfigType = LightCullingBuildConfig<DimensionT>;
    using ScratchType = LightCullingScratch<DimensionT>;
    using SpanType = LightClusterSpan<DimensionT>;

    [[nodiscard]] static constexpr ConfigType DefaultBuildConfig() noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            return ConfigType{
                .tile_count_x = 16U,
                .tile_count_y = 9U,
                .max_lights_per_tile = 128U,
                .stable_sort = 1U,
                .include_hidden_lights = 0U,
                .reserve0 = 0U,
                .reserve1 = 0U,
            };
        } else {
            return ConfigType{
                .cluster_count_x = 16U,
                .cluster_count_y = 9U,
                .cluster_count_z = 24U,
                .max_lights_per_cluster = 256U,
                .near_plane = 0.05F,
                .far_plane = 2000.0F,
                .z_slice_scale = 1.0F,
                .z_slice_bias = 1.0F,
                .stable_sort = 1U,
                .include_hidden_lights = 0U,
                .reverse_z = 0U,
                .reserve0 = 0U,
            };
        }
    }

    static void Reserve(ScratchType& scratch_,
                        std::uint32_t light_count_,
                        const ConfigType& config_ = DefaultBuildConfig()) {
        const std::uint32_t cluster_count = ComputeClusterCount(config_);
        const std::size_t light_reserve = static_cast<std::size_t>(light_count_);
        const std::size_t cluster_reserve = static_cast<std::size_t>(cluster_count);
        if (scratch_.light_spans.capacity() < light_reserve) {
            scratch_.light_spans.reserve(light_reserve);
        }
        if (scratch_.light_sort_keys.capacity() < light_reserve) {
            scratch_.light_sort_keys.reserve(light_reserve);
        }
        if (scratch_.headers.capacity() < cluster_reserve) {
            scratch_.headers.reserve(cluster_reserve);
        }
        if (scratch_.cluster_counts.capacity() < cluster_reserve) {
            scratch_.cluster_counts.reserve(cluster_reserve);
        }
        if (scratch_.cluster_write_offsets.capacity() < cluster_reserve) {
            scratch_.cluster_write_offsets.reserve(cluster_reserve);
        }
        const std::size_t expected_max_index_count = cluster_reserve * 8U;
        if (scratch_.light_indices.capacity() < expected_max_index_count) {
            scratch_.light_indices.reserve(expected_max_index_count);
        }
    }

    [[nodiscard]] static LightCullingBuildStats Build(
        const LightType* light_components_,
        const GeomType* derived_geom_,
        const LightDerivedOptical* derived_optical_,
        std::uint32_t light_count_,
        const CameraType* camera_component_,
        ScratchType& scratch_,
        const ConfigType& config_ = DefaultBuildConfig()) {
        LightCullingBuildStats stats{};
        stats.input_light_count = light_count_;
        stats.culling_config_signature = ComposeConfigSignature(config_);
        stats.camera_signature = ComposeCameraSignature(camera_component_);

        const std::uint32_t cluster_count = ComputeClusterCount(config_);
        stats.cluster_count = cluster_count;
        EnsureSizedScratch(scratch_, light_count_, cluster_count);
        ResetClusterBuffers(scratch_, cluster_count);
        scratch_.light_indices.clear();

        if (light_components_ == nullptr || derived_geom_ == nullptr || derived_optical_ == nullptr ||
            light_count_ == 0U || cluster_count == 0U) {
            return stats;
        }

        const Matrix4x4 view_projection = (camera_component_ != nullptr)
            ? camera_component_->runtime.view_projection_matrix
            : spatial_math::IdentityMatrix4x4();
        const Matrix4x4 view_matrix = (camera_component_ != nullptr)
            ? camera_component_->runtime.view_matrix
            : spatial_math::IdentityMatrix4x4();
        const Matrix4x4 projection_matrix = (camera_component_ != nullptr)
            ? camera_component_->runtime.projection_matrix
            : spatial_math::IdentityMatrix4x4();

        std::uint64_t visible_signature = k_hash_offset_basis;

        // Pass 1: gather valid light spans and count per-cluster occupancy (with cap).
        for (std::uint32_t light_index = 0U; light_index < light_count_; ++light_index) {
            const LightType& light = light_components_[light_index];
            SpanType& span = scratch_.light_spans[light_index];
            span = {};

            const bool allow_hidden = config_.include_hidden_lights != 0U;
            const bool visible = allow_hidden || LightSystem<DimensionT>::IsVisibleForCulling(light);
            if (!visible) {
                ++stats.culled_by_visibility_count;
                continue;
            }

            const float radius = std::max(0.0F, derived_optical_[light_index].radius);
            if (radius <= 0.0F) {
                ++stats.culled_by_visibility_count;
                continue;
            }

            ++stats.candidate_light_count;
            if (!BuildLightSpan(light,
                                derived_geom_[light_index],
                                derived_optical_[light_index],
                                view_matrix,
                                projection_matrix,
                                view_projection,
                                config_,
                                span,
                                scratch_.light_sort_keys[light_index])) {
                ++stats.culled_by_frustum_count;
                continue;
            }
            scratch_.light_sort_keys[light_index] =
                (scratch_.light_sort_keys[light_index] & 0xFFFFFFFF00000000ULL) |
                static_cast<std::uint64_t>(light_index);

            span.valid = 1U;
            ++stats.accepted_light_count;
            HashCombine(visible_signature, static_cast<std::uint64_t>(light_index));

            AccumulateClusterCounts(span, config_, scratch_.cluster_counts, stats.overflow_assignment_count);
        }

        // Prefix sum -> cluster headers, total index count.
        std::uint32_t running_offset = 0U;
        for (std::uint32_t cluster_index = 0U; cluster_index < cluster_count; ++cluster_index) {
            const std::uint32_t count = scratch_.cluster_counts[cluster_index];
            scratch_.headers[cluster_index].offset = running_offset;
            scratch_.headers[cluster_index].count = static_cast<std::uint16_t>(
                std::min<std::uint32_t>(count, (std::numeric_limits<std::uint16_t>::max)()));
            scratch_.headers[cluster_index].flags = 0U;
            scratch_.cluster_write_offsets[cluster_index] = running_offset;
            running_offset += count;
        }

        scratch_.light_indices.resize(static_cast<std::size_t>(running_offset));
        if (running_offset == 0U) {
            stats.visible_light_signature = visible_signature;
            return stats;
        }

        // Pass 2: emit light indices.
        for (std::uint32_t light_index = 0U; light_index < light_count_; ++light_index) {
            const SpanType& span = scratch_.light_spans[light_index];
            if (span.valid == 0U) {
                continue;
            }
            EmitLightIndices(light_index,
                             span,
                             config_,
                             scratch_.headers,
                             scratch_.cluster_write_offsets,
                             scratch_.light_indices);
        }

        // Stable per-cluster sort.
        if (config_.stable_sort != 0U) {
            for (std::uint32_t cluster_index = 0U; cluster_index < cluster_count; ++cluster_index) {
                const LightClusterHeader& header = scratch_.headers[cluster_index];
                if (header.count <= 1U) {
                    continue;
                }
                ++stats.stable_sort_cluster_count;
                const std::uint32_t begin_index = header.offset;
                const std::uint32_t end_index = begin_index + static_cast<std::uint32_t>(header.count);
                std::sort(scratch_.light_indices.begin() + begin_index,
                          scratch_.light_indices.begin() + end_index,
                          [&scratch_](std::uint32_t lhs_, std::uint32_t rhs_) noexcept {
                              const std::uint64_t lhs_key = scratch_.light_sort_keys[lhs_];
                              const std::uint64_t rhs_key = scratch_.light_sort_keys[rhs_];
                              if (lhs_key != rhs_key) {
                                  return lhs_key < rhs_key;
                              }
                              return lhs_ < rhs_;
                          });
            }
        }

        stats.emitted_index_count = running_offset;
        stats.visible_light_signature = visible_signature;
        return stats;
    }

    [[nodiscard]] static const LightClusterHeader* ClusterHeaders(const ScratchType& scratch_) noexcept {
        return scratch_.headers.data();
    }

    [[nodiscard]] static std::uint32_t ClusterHeaderCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.headers.size());
    }

    [[nodiscard]] static const std::uint32_t* ClusterLightIndices(const ScratchType& scratch_) noexcept {
        return scratch_.light_indices.data();
    }

    [[nodiscard]] static std::uint32_t ClusterLightIndexCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.light_indices.size());
    }

private:
    struct Float4Value final {
        float x;
        float y;
        float z;
        float w;
    };

    static constexpr std::uint64_t k_hash_offset_basis = 14695981039346656037ULL;
    static constexpr std::uint64_t k_hash_prime = 1099511628211ULL;
    static constexpr float k_epsilon = 1e-6F;

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= k_hash_prime;
    }

    template<typename T>
    static std::uint64_t ReinterpretAsU64(T value_) noexcept {
        static_assert(sizeof(T) <= sizeof(std::uint64_t));
        std::uint64_t out = 0U;
        std::memcpy(&out, &value_, sizeof(T));
        return out;
    }

    [[nodiscard]] static std::uint32_t ComputeClusterCount(const ConfigType& config_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            return static_cast<std::uint32_t>(config_.tile_count_x) *
                   static_cast<std::uint32_t>(config_.tile_count_y);
        } else {
            return static_cast<std::uint32_t>(config_.cluster_count_x) *
                   static_cast<std::uint32_t>(config_.cluster_count_y) *
                   static_cast<std::uint32_t>(config_.cluster_count_z);
        }
    }

    [[nodiscard]] static std::uint64_t ComposeConfigSignature(const ConfigType& config_) noexcept {
        std::uint64_t hash = k_hash_offset_basis;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            HashCombine(hash, static_cast<std::uint64_t>(config_.tile_count_x));
            HashCombine(hash, static_cast<std::uint64_t>(config_.tile_count_y));
            HashCombine(hash, static_cast<std::uint64_t>(config_.max_lights_per_tile));
            HashCombine(hash, static_cast<std::uint64_t>(config_.stable_sort));
            HashCombine(hash, static_cast<std::uint64_t>(config_.include_hidden_lights));
        } else {
            HashCombine(hash, static_cast<std::uint64_t>(config_.cluster_count_x));
            HashCombine(hash, static_cast<std::uint64_t>(config_.cluster_count_y));
            HashCombine(hash, static_cast<std::uint64_t>(config_.cluster_count_z));
            HashCombine(hash, static_cast<std::uint64_t>(config_.max_lights_per_cluster));
            HashCombine(hash, ReinterpretAsU64(config_.near_plane));
            HashCombine(hash, ReinterpretAsU64(config_.far_plane));
            HashCombine(hash, ReinterpretAsU64(config_.z_slice_scale));
            HashCombine(hash, ReinterpretAsU64(config_.z_slice_bias));
            HashCombine(hash, static_cast<std::uint64_t>(config_.stable_sort));
            HashCombine(hash, static_cast<std::uint64_t>(config_.include_hidden_lights));
            HashCombine(hash, static_cast<std::uint64_t>(config_.reverse_z));
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComposeCameraSignature(const CameraType* camera_component_) noexcept {
        if (camera_component_ == nullptr) {
            return 0U;
        }
        std::uint64_t hash = k_hash_offset_basis;
        HashCombine(hash, static_cast<std::uint64_t>(camera_component_->runtime.revision));
        HashCombine(hash, static_cast<std::uint64_t>(camera_component_->runtime.culling_mask));
        HashCombine(hash, ReinterpretAsU64(camera_component_->runtime.view_projection_matrix.m[0]));
        HashCombine(hash, ReinterpretAsU64(camera_component_->runtime.view_projection_matrix.m[5]));
        HashCombine(hash, ReinterpretAsU64(camera_component_->runtime.view_projection_matrix.m[10]));
        HashCombine(hash, ReinterpretAsU64(camera_component_->runtime.view_projection_matrix.m[12]));
        return hash;
    }

    static void EnsureSizedScratch(ScratchType& scratch_,
                                   std::uint32_t light_count_,
                                   std::uint32_t cluster_count_) {
        Reserve(scratch_, light_count_);
        scratch_.headers.resize(cluster_count_);
        scratch_.cluster_counts.resize(cluster_count_);
        scratch_.cluster_write_offsets.resize(cluster_count_);
        scratch_.light_spans.resize(light_count_);
        scratch_.light_sort_keys.resize(light_count_);
    }

    static void ResetClusterBuffers(ScratchType& scratch_,
                                    std::uint32_t cluster_count_) {
        for (std::uint32_t i = 0U; i < cluster_count_; ++i) {
            scratch_.headers[i] = LightClusterHeader{};
            scratch_.cluster_counts[i] = 0U;
            scratch_.cluster_write_offsets[i] = 0U;
        }
    }

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

    [[nodiscard]] static float Clamp01(float value_) noexcept {
        return std::clamp(value_, 0.0F, 1.0F);
    }

    [[nodiscard]] static std::uint32_t BuildStableSortKeyPrefix(const LightType& light_) noexcept {
        const std::uint32_t light_type = static_cast<std::uint32_t>(light_.style.kind) & 0x3U;
        const std::uint32_t shadow_bit = static_cast<std::uint32_t>(light_.style.cast_shadow & 0x1U);
        return (light_type << 29U) | (shadow_bit << 28U);
    }

    [[nodiscard]] static std::uint64_t BuildLightSortKey(const LightType& light_,
                                                         float depth_distance_,
                                                         std::uint32_t light_index_) noexcept {
        const std::uint32_t prefix = BuildStableSortKeyPrefix(light_);
        const float depth_clamped = std::max(0.0F, depth_distance_);
        const std::uint32_t depth_bucket = static_cast<std::uint32_t>(
            std::min(depth_clamped * 2048.0F, static_cast<float>((1U << 28U) - 1U)));
        const std::uint64_t combined = (static_cast<std::uint64_t>(prefix) << 32U) |
                                       static_cast<std::uint64_t>(depth_bucket);
        return (combined << 32U) | static_cast<std::uint64_t>(light_index_);
    }

    [[nodiscard]] static bool BuildLightSpan(const LightType& light_,
                                             const GeomType& geom_,
                                             const LightDerivedOptical& optical_,
                                             const Matrix4x4& view_matrix_,
                                             const Matrix4x4& projection_matrix_,
                                             const Matrix4x4& view_projection_matrix_,
                                             const ConfigType& config_,
                                             SpanType& out_span_,
                                             std::uint64_t& out_sort_key_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const float center_x = geom_.position_x;
            const float center_y = geom_.position_y;
            const float radius = std::max(0.0F, optical_.radius);

            const Float4Value clip_center = MultiplyPoint(view_projection_matrix_, center_x, center_y, 0.0F);
            if (std::abs(clip_center.w) <= k_epsilon) {
                return false;
            }

            const float ndc_x = clip_center.x / clip_center.w;
            const float ndc_y = clip_center.y / clip_center.w;
            const float radius_ndc_x = radius * std::abs(projection_matrix_.m[0]);
            const float radius_ndc_y = radius * std::abs(projection_matrix_.m[5]);

            const float ndc_min_x = ndc_x - radius_ndc_x;
            const float ndc_max_x = ndc_x + radius_ndc_x;
            const float ndc_min_y = ndc_y - radius_ndc_y;
            const float ndc_max_y = ndc_y + radius_ndc_y;

            if (ndc_max_x < -1.0F || ndc_min_x > 1.0F || ndc_max_y < -1.0F || ndc_min_y > 1.0F) {
                return false;
            }

            const std::uint32_t tile_count_x = std::max<std::uint32_t>(1U, config_.tile_count_x);
            const std::uint32_t tile_count_y = std::max<std::uint32_t>(1U, config_.tile_count_y);
            const std::uint32_t min_x = NdcToGridCoord(Clamp01(0.5F * ndc_min_x + 0.5F), tile_count_x);
            const std::uint32_t max_x = NdcToGridCoord(Clamp01(0.5F * ndc_max_x + 0.5F), tile_count_x);
            const std::uint32_t min_y = NdcToGridCoord(Clamp01(0.5F * (-ndc_max_y) + 0.5F), tile_count_y);
            const std::uint32_t max_y = NdcToGridCoord(Clamp01(0.5F * (-ndc_min_y) + 0.5F), tile_count_y);

            out_span_.min_x = static_cast<std::uint16_t>(std::min<std::uint32_t>(min_x, tile_count_x - 1U));
            out_span_.max_x = static_cast<std::uint16_t>(std::min<std::uint32_t>(max_x, tile_count_x - 1U));
            out_span_.min_y = static_cast<std::uint16_t>(std::min<std::uint32_t>(min_y, tile_count_y - 1U));
            out_span_.max_y = static_cast<std::uint16_t>(std::min<std::uint32_t>(max_y, tile_count_y - 1U));
            out_span_.min_z = 0U;
            out_span_.max_z = 0U;
            out_sort_key_ = BuildLightSortKey(light_, 0.0F, 0U);
            return true;
        } else {
            const float center_x = geom_.position_x;
            const float center_y = geom_.position_y;
            const float center_z = geom_.position_z;
            const float radius = std::max(0.0F, optical_.radius);
            const Float4Value view_center = MultiplyPoint(view_matrix_, center_x, center_y, center_z);
            const float depth_distance = std::max(k_epsilon, std::abs(view_center.z));
            const float near_plane = std::max(k_epsilon, config_.near_plane);
            const float far_plane = std::max(near_plane + k_epsilon, config_.far_plane);
            const float depth_min = std::max(near_plane, depth_distance - radius);
            const float depth_max = std::min(far_plane, depth_distance + radius);
            if (depth_max <= near_plane || depth_min >= far_plane) {
                return false;
            }

            const Float4Value clip_center = MultiplyPoint(view_projection_matrix_, center_x, center_y, center_z);
            if (std::abs(clip_center.w) <= k_epsilon) {
                return false;
            }
            const float ndc_x = clip_center.x / clip_center.w;
            const float ndc_y = clip_center.y / clip_center.w;
            const float radius_ndc_x = (radius * std::abs(projection_matrix_.m[0])) / depth_distance;
            const float radius_ndc_y = (radius * std::abs(projection_matrix_.m[5])) / depth_distance;
            const float ndc_min_x = ndc_x - radius_ndc_x;
            const float ndc_max_x = ndc_x + radius_ndc_x;
            const float ndc_min_y = ndc_y - radius_ndc_y;
            const float ndc_max_y = ndc_y + radius_ndc_y;
            if (ndc_max_x < -1.0F || ndc_min_x > 1.0F || ndc_max_y < -1.0F || ndc_min_y > 1.0F) {
                return false;
            }

            const std::uint32_t cluster_count_x = std::max<std::uint32_t>(1U, config_.cluster_count_x);
            const std::uint32_t cluster_count_y = std::max<std::uint32_t>(1U, config_.cluster_count_y);
            const std::uint32_t cluster_count_z = std::max<std::uint32_t>(1U, config_.cluster_count_z);
            const std::uint32_t min_x = NdcToGridCoord(Clamp01(0.5F * ndc_min_x + 0.5F), cluster_count_x);
            const std::uint32_t max_x = NdcToGridCoord(Clamp01(0.5F * ndc_max_x + 0.5F), cluster_count_x);
            const std::uint32_t min_y = NdcToGridCoord(Clamp01(0.5F * (-ndc_max_y) + 0.5F), cluster_count_y);
            const std::uint32_t max_y = NdcToGridCoord(Clamp01(0.5F * (-ndc_min_y) + 0.5F), cluster_count_y);
            const std::uint32_t min_z = DepthToSlice(depth_min, near_plane, far_plane, cluster_count_z, config_);
            const std::uint32_t max_z = DepthToSlice(depth_max, near_plane, far_plane, cluster_count_z, config_);

            out_span_.min_x = static_cast<std::uint16_t>(std::min<std::uint32_t>(min_x, cluster_count_x - 1U));
            out_span_.max_x = static_cast<std::uint16_t>(std::min<std::uint32_t>(max_x, cluster_count_x - 1U));
            out_span_.min_y = static_cast<std::uint16_t>(std::min<std::uint32_t>(min_y, cluster_count_y - 1U));
            out_span_.max_y = static_cast<std::uint16_t>(std::min<std::uint32_t>(max_y, cluster_count_y - 1U));
            out_span_.min_z = static_cast<std::uint16_t>(std::min<std::uint32_t>(min_z, cluster_count_z - 1U));
            out_span_.max_z = static_cast<std::uint16_t>(std::min<std::uint32_t>(max_z, cluster_count_z - 1U));
            out_sort_key_ = BuildLightSortKey(light_, depth_distance, 0U);
            return true;
        }
    }

    [[nodiscard]] static std::uint32_t NdcToGridCoord(float normalized_coord_,
                                                      std::uint32_t grid_size_) noexcept {
        if (grid_size_ <= 1U) {
            return 0U;
        }
        const float scaled = normalized_coord_ * static_cast<float>(grid_size_);
        const std::uint32_t coord = static_cast<std::uint32_t>(scaled);
        return std::min<std::uint32_t>(coord, grid_size_ - 1U);
    }

    [[nodiscard]] static std::uint32_t DepthToSlice(float depth_,
                                                    float near_plane_,
                                                    float far_plane_,
                                                    std::uint32_t slice_count_,
                                                    const ConfigType& config_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (slice_count_ <= 1U) {
            return 0U;
        }
        const float clamped_depth = std::clamp(depth_, near_plane_, far_plane_);
        const float z_scale = std::max(config_.z_slice_scale, k_epsilon);
        const float z_bias = std::max(config_.z_slice_bias, k_epsilon);
        const float near_t = std::log2(near_plane_ * z_scale + z_bias);
        const float far_t = std::log2(far_plane_ * z_scale + z_bias);
        const float depth_t = std::log2(clamped_depth * z_scale + z_bias);
        const float denominator = std::max(k_epsilon, far_t - near_t);
        float normalized = (depth_t - near_t) / denominator;
        normalized = Clamp01(normalized);
        if (config_.reverse_z != 0U) {
            normalized = 1.0F - normalized;
        }
        return NdcToGridCoord(normalized, slice_count_);
    }

    static void AccumulateClusterCounts(const SpanType& span_,
                                        const ConfigType& config_,
                                        LightCullingMcVector<std::uint32_t>& cluster_counts_,
                                        std::uint32_t& overflow_assignment_count_) {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const std::uint32_t grid_x = std::max<std::uint32_t>(1U, config_.tile_count_x);
            const std::uint32_t cap = std::max<std::uint32_t>(1U, config_.max_lights_per_tile);
            for (std::uint32_t y = span_.min_y; y <= span_.max_y; ++y) {
                const std::uint32_t row_base = y * grid_x;
                for (std::uint32_t x = span_.min_x; x <= span_.max_x; ++x) {
                    const std::uint32_t cluster_index = row_base + x;
                    if (cluster_counts_[cluster_index] >= cap) {
                        ++overflow_assignment_count_;
                        continue;
                    }
                    ++cluster_counts_[cluster_index];
                }
            }
        } else {
            const std::uint32_t grid_x = std::max<std::uint32_t>(1U, config_.cluster_count_x);
            const std::uint32_t grid_y = std::max<std::uint32_t>(1U, config_.cluster_count_y);
            const std::uint32_t grid_xy = grid_x * grid_y;
            const std::uint32_t cap = std::max<std::uint32_t>(1U, config_.max_lights_per_cluster);
            for (std::uint32_t z = span_.min_z; z <= span_.max_z; ++z) {
                const std::uint32_t slab_base = z * grid_xy;
                for (std::uint32_t y = span_.min_y; y <= span_.max_y; ++y) {
                    const std::uint32_t row_base = slab_base + y * grid_x;
                    for (std::uint32_t x = span_.min_x; x <= span_.max_x; ++x) {
                        const std::uint32_t cluster_index = row_base + x;
                        if (cluster_counts_[cluster_index] >= cap) {
                            ++overflow_assignment_count_;
                            continue;
                        }
                        ++cluster_counts_[cluster_index];
                    }
                }
            }
        }
    }

    static void EmitLightIndices(std::uint32_t light_index_,
                                 const SpanType& span_,
                                 const ConfigType& config_,
                                 const LightCullingMcVector<LightClusterHeader>& headers_,
                                 LightCullingMcVector<std::uint32_t>& cluster_write_offsets_,
                                 LightCullingMcVector<std::uint32_t>& light_indices_) {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const std::uint32_t grid_x = std::max<std::uint32_t>(1U, config_.tile_count_x);
            for (std::uint32_t y = span_.min_y; y <= span_.max_y; ++y) {
                const std::uint32_t row_base = y * grid_x;
                for (std::uint32_t x = span_.min_x; x <= span_.max_x; ++x) {
                    const std::uint32_t cluster_index = row_base + x;
                    WriteLightIndex(cluster_index,
                                    light_index_,
                                    headers_,
                                    cluster_write_offsets_,
                                    light_indices_);
                }
            }
        } else {
            const std::uint32_t grid_x = std::max<std::uint32_t>(1U, config_.cluster_count_x);
            const std::uint32_t grid_y = std::max<std::uint32_t>(1U, config_.cluster_count_y);
            const std::uint32_t grid_xy = grid_x * grid_y;
            for (std::uint32_t z = span_.min_z; z <= span_.max_z; ++z) {
                const std::uint32_t slab_base = z * grid_xy;
                for (std::uint32_t y = span_.min_y; y <= span_.max_y; ++y) {
                    const std::uint32_t row_base = slab_base + y * grid_x;
                    for (std::uint32_t x = span_.min_x; x <= span_.max_x; ++x) {
                        const std::uint32_t cluster_index = row_base + x;
                        if (cluster_index >= headers_.size()) {
                            continue;
                        }
                        WriteLightIndex(cluster_index,
                                        light_index_,
                                        headers_,
                                        cluster_write_offsets_,
                                        light_indices_);
                    }
                }
            }
        }
    }

    static void WriteLightIndex(std::uint32_t cluster_index_,
                                std::uint32_t light_index_,
                                const LightCullingMcVector<LightClusterHeader>& headers_,
                                LightCullingMcVector<std::uint32_t>& cluster_write_offsets_,
                                LightCullingMcVector<std::uint32_t>& light_indices_) {
        if (cluster_index_ >= headers_.size() || cluster_index_ >= cluster_write_offsets_.size()) {
            return;
        }
        const LightClusterHeader& header = headers_[cluster_index_];
        const std::uint32_t begin_index = header.offset;
        const std::uint32_t end_index = begin_index + static_cast<std::uint32_t>(header.count);
        std::uint32_t write_index = cluster_write_offsets_[cluster_index_];
        if (write_index < begin_index) {
            write_index = begin_index;
        }
        if (write_index >= end_index) {
            return;
        }
        if (write_index >= light_indices_.size()) {
            return;
        }
        light_indices_[write_index] = light_index_;
        cluster_write_offsets_[cluster_index_] = write_index + 1U;
    }
};

} // namespace vr::ecs
