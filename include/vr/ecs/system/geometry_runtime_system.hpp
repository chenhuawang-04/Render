#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/geometry_batch_system.hpp"
#include "vr/ecs/system/geometry_path_system.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace vr::ecs {

template<typename T>
using GeometryRuntimeMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

enum class GeometryRuntimeCacheStatus : std::uint8_t {
    miss = 0U,
    hit_reused = 1U,
    hit_partial_update = 2U,
};

enum class GeometryRuntimeCacheMissReason : std::uint8_t {
    none = 0U,
    invalid_input = 1U,
    cold_start = 2U,
    components_pointer_changed = 3U,
    transforms_pointer_changed = 4U,
    component_count_changed = 5U,
    geometry_signature_changed = 6U,
    visibility_signature_changed = 7U,
    transform_signature_changed = 8U,
    build_config_changed = 9U,
    animation_signature_changed = 10U,
};

[[nodiscard]] constexpr std::uint32_t NextRuntimeCacheEpoch(std::uint32_t current_epoch_) noexcept {
    return (current_epoch_ == std::numeric_limits<std::uint32_t>::max())
               ? 1U
               : (current_epoch_ + 1U);
}

struct Geometry2DPathPrimitive final {
    float x0;
    float y0;
    float x1;
    float y1;
    std::uint32_t fill_color_rgba8;
    std::uint32_t stroke_color_rgba8;
    float stroke_width_px;
    std::uint32_t params;
    std::uint32_t component_index;
    std::uint32_t user_data;
};

struct Geometry2DDrawBatch final {
    std::uint64_t sort_key;
    std::uint32_t primitive_begin;
    std::uint32_t primitive_count;
    std::uint32_t geometry_id;
    std::uint32_t effective_visual_resource_id;
    std::uint32_t first_component_index;
    std::uint32_t params;
};

struct Geometry2DRuntimeBuildConfig final {
    std::uint32_t quad_subdivision = 8U;
    std::uint32_t cubic_subdivision = 12U;
    std::uint32_t max_primitives_per_component = 0U;
    float zero_length_epsilon = 1e-6F;
    bool build_ordered_indices = true;
};

struct Geometry2DRuntimeBuildHint final {
    std::uint64_t external_build_signature = 0U;
    std::uint8_t use_external_build_signature = 0U;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
    std::uint32_t reserved2 = 0U;
};

struct Geometry2DRuntimeBuildStats final {
    GeometryBatchBuildStats batch{};
    std::uint32_t emitted_primitive_count = 0U;
    std::uint32_t emitted_batch_count = 0U;
    std::uint32_t approximated_quad_count = 0U;
    std::uint32_t approximated_cubic_count = 0U;
    std::uint32_t truncated_component_count = 0U;
    std::uint32_t cache_epoch = 0U;
    std::uint64_t build_signature = 0U;
    GeometryRuntimeCacheStatus cache_status = GeometryRuntimeCacheStatus::miss;
    GeometryRuntimeCacheMissReason cache_miss_reason = GeometryRuntimeCacheMissReason::none;
    bool cache_reused = false;
    bool cache_valid_before_build = false;
    bool cache_key_matched = false;
    bool signature_from_hint = false;
};

struct Geometry2DRuntimeCache final {
    const Geometry<Dim2>* components = nullptr;
    std::uint32_t component_count = 0U;
    std::uint64_t signature = 0U;
    std::uint32_t epoch = 0U;
    Geometry2DRuntimeBuildConfig build_config{};
    Geometry2DRuntimeBuildStats last_stats{};
    bool valid = false;
};

struct Geometry2DRuntimeScratch final {
    GeometryBatchScratch<Dim2> batch_scratch{};
    GeometryRuntimeMcVector<Geometry2DPathPrimitive> primitives{};
    GeometryRuntimeMcVector<Geometry2DDrawBatch> draw_batches{};
    Geometry2DRuntimeCache cache{};
};

struct Geometry3DGpuInstance final {
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

    std::uint32_t params;
    std::uint32_t geometry_id;
    std::uint32_t effective_visual_resource_id;

    std::uint32_t submesh_index;
    std::uint32_t appearance_record_index;
    std::uint32_t component_index;
    std::uint32_t reserved0;

    float deform_param0_x;
    float deform_param0_y;
    float deform_param0_z;
    float deform_param0_w;

    float deform_param1_x;
    float deform_param1_y;
    float deform_param1_z;
    float deform_param1_w;

    float morph_weight0;
    float morph_weight1;
    float morph_weight_reserved0;
    float morph_weight_reserved1;
};

struct Geometry3DDrawBatch final {
    std::uint64_t sort_key;
    std::uint32_t instance_begin;
    std::uint32_t instance_count;
    std::uint32_t geometry_id;
    std::uint32_t effective_visual_resource_id;
    std::uint32_t submesh_index;
    std::uint32_t first_component_index;
    std::uint32_t params;
};

struct Geometry3DRuntimeBuildConfig final {
    bool build_ordered_indices = true;
};

struct Geometry3DRuntimeBuildHint final {
    std::uint64_t external_geometry_signature = 0U;
    std::uint64_t external_transform_signature = 0U;
    std::uint64_t external_visible_set_signature = 0U;
    const std::uint32_t* transform_dirty_component_indices = nullptr;
    const std::uint32_t* visible_component_indices = nullptr;
    const SkeletalPoseOutputState<Dim3>* skeletal_outputs = nullptr;
    const VertexDeformOutputState* vertex_deform_outputs = nullptr;
    const MorphWeightOutputState* morph_outputs = nullptr;
    const FrameSequenceOutputState* frame_sequence_outputs = nullptr;
    std::uint32_t transform_dirty_component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t skeletal_output_count = 0U;
    std::uint32_t vertex_deform_output_count = 0U;
    std::uint32_t morph_output_count = 0U;
    std::uint32_t frame_sequence_output_count = 0U;
    std::uint8_t use_external_geometry_signature = 0U;
    std::uint8_t use_external_transform_signature = 0U;
    std::uint8_t use_visible_component_indices = 0U;
    std::uint8_t use_external_visible_set_signature = 0U;
};

struct Geometry3DRuntimeBuildStats final {
    GeometryBatchBuildStats batch{};
    std::uint32_t candidate_component_count = 0U;
    std::uint32_t emitted_instance_count = 0U;
    std::uint32_t emitted_batch_count = 0U;
    std::uint32_t transform_rewritten_instance_count = 0U;
    std::uint32_t skeletal_rewritten_instance_count = 0U;
    std::uint32_t skeletal_animated_instance_count = 0U;
    std::uint32_t vertex_deform_rewritten_instance_count = 0U;
    std::uint32_t vertex_deform_animated_instance_count = 0U;
    std::uint32_t morph_rewritten_instance_count = 0U;
    std::uint32_t morph_animated_instance_count = 0U;
    std::uint32_t frame_sequence_animated_instance_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
    std::uint32_t shadow_cast_batch_count = 0U;
    std::uint32_t cache_epoch = 0U;
    std::uint64_t geometry_signature = 0U;
    std::uint64_t transform_signature = 0U;
    std::uint64_t visible_set_signature = 0U;
    std::uint64_t skeletal_signature = 0U;
    std::uint64_t vertex_deform_signature = 0U;
    std::uint64_t morph_signature = 0U;
    std::uint64_t frame_sequence_signature = 0U;
    GeometryRuntimeCacheStatus cache_status = GeometryRuntimeCacheStatus::miss;
    GeometryRuntimeCacheMissReason cache_miss_reason = GeometryRuntimeCacheMissReason::none;
    bool cache_reused = false;
    bool transform_only_update = false;
    bool used_visible_component_indices = false;
    bool cache_valid_before_build = false;
    bool cache_key_matched = false;
    bool geometry_signature_from_hint = false;
    bool transform_signature_from_hint = false;
    bool visible_set_signature_from_hint = false;
    bool transform_update_from_dirty_hint = false;
};

struct Geometry3DRuntimeCache final {
    const Geometry<Dim3>* components = nullptr;
    const Transform<Dim3>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    std::uint32_t candidate_component_count = 0U;
    std::uint64_t geometry_signature = 0U;
    std::uint64_t transform_signature = 0U;
    std::uint64_t visible_set_signature = 0U;
    std::uint64_t skeletal_signature = 0U;
    std::uint64_t vertex_deform_signature = 0U;
    std::uint64_t morph_signature = 0U;
    std::uint64_t frame_sequence_signature = 0U;
    std::uint32_t epoch = 0U;
    GeometryRuntimeMcVector<std::uint32_t> instance_world_revisions{};
    GeometryRuntimeMcVector<std::uint32_t> component_to_instance_index{};
    Geometry3DRuntimeBuildConfig build_config{};
    Geometry3DRuntimeBuildStats last_stats{};
    bool valid = false;
};

struct Geometry3DRuntimeScratch final {
    GeometryBatchScratch<Dim3> batch_scratch{};
    GeometryRuntimeMcVector<Geometry3DGpuInstance> instances{};
    GeometryRuntimeMcVector<Geometry3DDrawBatch> draw_batches{};
    Geometry3DRuntimeCache cache{};
};

static_assert(PurePodGeometryComponent<Geometry2DPathPrimitive>);
static_assert(PurePodGeometryComponent<Geometry2DDrawBatch>);
static_assert(PurePodGeometryComponent<Geometry3DGpuInstance>);
static_assert(PurePodGeometryComponent<Geometry3DDrawBatch>);
static_assert(alignof(Geometry3DGpuInstance) == 4U);

template<DimensionTag DimensionT>
class GeometryRuntimeSystem;

template<>
class GeometryRuntimeSystem<Dim2> final {
public:
    using GeometryType = Geometry<Dim2>;
    using BatchSystemType = GeometryBatchSystem<Dim2>;
    using ScratchType = Geometry2DRuntimeScratch;

    static void Reserve(ScratchType& scratch_,
                        std::uint32_t component_count_,
                        std::uint32_t primitive_capacity_hint_ = 0U) {
        BatchSystemType::Reserve(scratch_.batch_scratch, component_count_);

        const std::size_t primitive_reserve = static_cast<std::size_t>(
            primitive_capacity_hint_ > 0U ? primitive_capacity_hint_ : component_count_);
        if (scratch_.primitives.capacity() < primitive_reserve) {
            scratch_.primitives.reserve(primitive_reserve);
        }
        if (scratch_.draw_batches.capacity() < primitive_reserve) {
            scratch_.draw_batches.reserve(primitive_reserve);
        }
    }

    [[nodiscard]] static Geometry2DRuntimeBuildStats Build(const GeometryType* components_,
                                                           std::uint32_t component_count_,
                                                           ScratchType& scratch_,
                                                           const Geometry2DRuntimeBuildConfig& build_config_ = {},
                                                           const Geometry2DRuntimeBuildHint& build_hint_ = {}) {
        Geometry2DRuntimeBuildStats stats{};
        stats.cache_valid_before_build = scratch_.cache.valid;

        if (components_ == nullptr || component_count_ == 0U) {
            scratch_.primitives.clear();
            scratch_.draw_batches.clear();
            InvalidateCache(scratch_.cache);

            stats.cache_status = GeometryRuntimeCacheStatus::miss;
            stats.cache_miss_reason = GeometryRuntimeCacheMissReason::invalid_input;
            stats.cache_epoch = scratch_.cache.epoch;
            return stats;
        }

        const bool use_external_signature = build_hint_.use_external_build_signature != 0U;
        const std::uint64_t signature = use_external_signature
            ? build_hint_.external_build_signature
            : ComputeBuildSignature(components_, component_count_);
        stats.build_signature = signature;
        stats.signature_from_hint = use_external_signature;
        const CacheProbeResult cache_probe = ProbeCache(scratch_.cache,
                                                        components_,
                                                        component_count_,
                                                        signature,
                                                        build_config_);
        stats.cache_key_matched = cache_probe.key_matched;
        if (cache_probe.key_matched) {
            stats = scratch_.cache.last_stats;
            stats.build_signature = signature;
            stats.cache_status = GeometryRuntimeCacheStatus::hit_reused;
            stats.cache_miss_reason = GeometryRuntimeCacheMissReason::none;
            stats.cache_reused = true;
            stats.cache_valid_before_build = true;
            stats.cache_key_matched = true;
            stats.cache_epoch = scratch_.cache.epoch;
            stats.signature_from_hint = use_external_signature;
            return stats;
        }

        scratch_.primitives.clear();
        scratch_.draw_batches.clear();

        stats.batch = BatchSystemType::BuildAndSort(components_,
                                                    component_count_,
                                                    scratch_.batch_scratch,
                                                    build_config_.build_ordered_indices);
        if (stats.batch.visible_count == 0U) {
            stats.cache_status = GeometryRuntimeCacheStatus::miss;
            stats.cache_miss_reason = cache_probe.miss_reason;
            stats.cache_reused = false;
            stats.cache_valid_before_build = scratch_.cache.valid;
            stats.cache_key_matched = false;
            stats.signature_from_hint = use_external_signature;

            CommitMissCache(scratch_.cache,
                            components_,
                            component_count_,
                            signature,
                            build_config_,
                            stats);
            return stats;
        }

        const std::uint32_t quad_subdivision = std::max(1U, build_config_.quad_subdivision);
        const std::uint32_t cubic_subdivision = std::max(1U, build_config_.cubic_subdivision);
        const float zero_length_epsilon = std::max(0.0F, build_config_.zero_length_epsilon);

        const GeometryBatchItem* sorted_items = BatchSystemType::SortedItems(scratch_.batch_scratch);
        const std::uint32_t sorted_count = BatchSystemType::VisibleCount(scratch_.batch_scratch);

        for (std::uint32_t i = 0U; i < sorted_count; ++i) {
            const GeometryBatchItem& item = sorted_items[i];
            const GeometryType& component = components_[item.component_index];

            const std::uint32_t primitive_begin = static_cast<std::uint32_t>(scratch_.primitives.size());
            const bool truncated = EmitPathPrimitives(component,
                                                      item.component_index,
                                                      quad_subdivision,
                                                      cubic_subdivision,
                                                      build_config_.max_primitives_per_component,
                                                      zero_length_epsilon,
                                                      scratch_);
            if (truncated) {
                ++stats.truncated_component_count;
            }

            const std::uint32_t primitive_count =
                static_cast<std::uint32_t>(scratch_.primitives.size()) - primitive_begin;
            if (primitive_count == 0U) {
                continue;
            }

            AppendOrMergeBatch(component,
                               item.sort_key,
                               item.component_index,
                               primitive_begin,
                               primitive_count,
                               scratch_);
        }

        stats.emitted_primitive_count = static_cast<std::uint32_t>(scratch_.primitives.size());
        stats.emitted_batch_count = static_cast<std::uint32_t>(scratch_.draw_batches.size());
        stats.approximated_quad_count = quad_subdivision;
        stats.approximated_cubic_count = cubic_subdivision;
        stats.build_signature = signature;
        stats.cache_status = GeometryRuntimeCacheStatus::miss;
        stats.cache_miss_reason = cache_probe.miss_reason;
        stats.cache_reused = false;
        stats.cache_valid_before_build = scratch_.cache.valid;
        stats.cache_key_matched = false;
        stats.signature_from_hint = use_external_signature;

        CommitMissCache(scratch_.cache,
                        components_,
                        component_count_,
                        signature,
                        build_config_,
                        stats);
        return stats;
    }

private:
    struct PathPenState final {
        Float2 current_point{.x = 0.0F, .y = 0.0F};
        Float2 subpath_start{.x = 0.0F, .y = 0.0F};
        bool has_current = false;
    };

    struct CacheProbeResult final {
        bool key_matched = false;
        GeometryRuntimeCacheMissReason miss_reason = GeometryRuntimeCacheMissReason::none;
    };

    static void InvalidateCache(Geometry2DRuntimeCache& cache_) noexcept {
        cache_.components = nullptr;
        cache_.component_count = 0U;
        cache_.signature = 0U;
        cache_.epoch = 0U;
        cache_.valid = false;
    }

    static void CommitMissCache(Geometry2DRuntimeCache& cache_,
                                const GeometryType* components_,
                                std::uint32_t component_count_,
                                std::uint64_t signature_,
                                const Geometry2DRuntimeBuildConfig& build_config_,
                                Geometry2DRuntimeBuildStats& stats_) {
        cache_.components = components_;
        cache_.component_count = component_count_;
        cache_.signature = signature_;
        cache_.build_config = build_config_;
        cache_.epoch = NextRuntimeCacheEpoch(cache_.epoch);
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

    [[nodiscard]] static std::uint32_t PackStyleParams(const GeometryType& component_) noexcept {
        const AppearanceRuntimeBridge2D appearance_bridge =
            ReadAppearanceRuntimeBridge2D(component_.runtime);
        RuntimeBlendPreset blend_preset = ResolveRuntimeBlendPreset(appearance_bridge);
        if (component_.runtime.route.appearance_handle.index != invalid_appearance_handle.index &&
            component_.runtime.route.appearance_handle.generation != 0U) {
            blend_preset = ResolveRuntimeBlendPreset(component_.runtime.route.appearance_pipeline_bucket);
        }

        std::uint32_t params = 0U;
        params |= (appearance_bridge.antialiasing != 0U) ? 0x1U : 0U;
        params |= (static_cast<std::uint32_t>(component_.style.topology) & 0x3U) << 1U;
        params |= (static_cast<std::uint32_t>(component_.style.fill_rule) & 0x1U) << 3U;
        params |= (static_cast<std::uint32_t>(component_.style.line_join) & 0x3U) << 4U;
        params |= (static_cast<std::uint32_t>(component_.style.line_cap) & 0x3U) << 6U;
        params |= EncodeRuntimeBlendPresetBits(blend_preset, geometry2d_runtime_blend_shift);
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

    [[nodiscard]] static bool IsBuildConfigEqual(const Geometry2DRuntimeBuildConfig& lhs_,
                                                 const Geometry2DRuntimeBuildConfig& rhs_) noexcept {
        return lhs_.quad_subdivision == rhs_.quad_subdivision &&
               lhs_.cubic_subdivision == rhs_.cubic_subdivision &&
               lhs_.max_primitives_per_component == rhs_.max_primitives_per_component &&
               FloatBits(lhs_.zero_length_epsilon) == FloatBits(rhs_.zero_length_epsilon) &&
               lhs_.build_ordered_indices == rhs_.build_ordered_indices;
    }

    [[nodiscard]] static CacheProbeResult ProbeCache(const Geometry2DRuntimeCache& cache_,
                                                     const GeometryType* components_,
                                                     std::uint32_t component_count_,
                                                     std::uint64_t signature_,
                                                     const Geometry2DRuntimeBuildConfig& build_config_) noexcept {
        if (!cache_.valid) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::cold_start
            };
        }
        if (cache_.components != components_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::components_pointer_changed
            };
        }
        if (cache_.component_count != component_count_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::component_count_changed
            };
        }
        if (cache_.signature != signature_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::geometry_signature_changed
            };
        }
        if (!IsBuildConfigEqual(cache_.build_config, build_config_)) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::build_config_changed
            };
        }
        return CacheProbeResult{
            .key_matched = true,
            .miss_reason = GeometryRuntimeCacheMissReason::none
        };
    }

    [[nodiscard]] static std::uint64_t ComputeBuildSignature(const GeometryType* components_,
                                                             std::uint32_t component_count_) noexcept {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            const GeometryType& component = components_[i];
            HashCombine(hash, static_cast<std::uint64_t>(component.runtime.route.visible));
            if (component.runtime.route.visible == 0U) {
                continue;
            }

            HashCombine(hash, component.runtime.route.sort_key);
            HashCombine(hash, static_cast<std::uint64_t>(component.runtime.route.geometry_id));
            HashCombine(hash,
                        static_cast<std::uint64_t>(ResolveEffectiveVisualResourceId(component.runtime.route)));
            HashCombine(hash, static_cast<std::uint64_t>(component.runtime.route.user_data));
            HashCombine(hash, static_cast<std::uint64_t>(component.path.revision));
            HashCombine(hash, static_cast<std::uint64_t>(component.runtime.path_data_hash));
            const AppearanceRuntimeBridge2D appearance_bridge =
                ReadAppearanceRuntimeBridge2D(component.runtime);
            HashCombine(hash,
                        static_cast<std::uint64_t>(
                            PackRgba8(ResolveAppearanceRuntimeBridge2DFillColor(appearance_bridge))));
            HashCombine(hash,
                        static_cast<std::uint64_t>(
                            PackRgba8(ResolveAppearanceRuntimeBridge2DStrokeColor(appearance_bridge))));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(component.style.stroke_width_px)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(component.style.miter_limit)));
            HashCombine(hash, static_cast<std::uint64_t>(PackStyleParams(component)));
        }
        return hash;
    }

    [[nodiscard]] static float LengthSquared(const Float2& delta_) noexcept {
        return delta_.x * delta_.x + delta_.y * delta_.y;
    }

    [[nodiscard]] static bool TryEmitLineSegment(const GeometryType& component_,
                                                 std::uint32_t component_index_,
                                                 const Float2& p0_,
                                                 const Float2& p1_,
                                                 float zero_length_epsilon_,
                                                 ScratchType& scratch_) {
        const Float2 delta{
            .x = p1_.x - p0_.x,
            .y = p1_.y - p0_.y
        };
        if (LengthSquared(delta) <= zero_length_epsilon_ * zero_length_epsilon_) {
            return false;
        }

        Geometry2DPathPrimitive primitive{};
        primitive.x0 = p0_.x;
        primitive.y0 = p0_.y;
        primitive.x1 = p1_.x;
        primitive.y1 = p1_.y;
        const AppearanceRuntimeBridge2D appearance_bridge =
            ReadAppearanceRuntimeBridge2D(component_.runtime);
        primitive.fill_color_rgba8 =
            PackRgba8(ResolveAppearanceRuntimeBridge2DFillColor(appearance_bridge));
        primitive.stroke_color_rgba8 =
            PackRgba8(ResolveAppearanceRuntimeBridge2DStrokeColor(appearance_bridge));
        primitive.stroke_width_px = component_.style.stroke_width_px;
        primitive.params = PackStyleParams(component_);
        primitive.component_index = component_index_;
        primitive.user_data = component_.runtime.route.user_data;
        scratch_.primitives.push_back(primitive);
        return true;
    }

    [[nodiscard]] static Float2 Lerp(const Float2& a_, const Float2& b_, float t_) noexcept {
        return Float2{
            .x = a_.x + (b_.x - a_.x) * t_,
            .y = a_.y + (b_.y - a_.y) * t_,
        };
    }

    [[nodiscard]] static Float2 EvaluateQuadratic(const Float2& p0_,
                                                  const Float2& c_,
                                                  const Float2& p1_,
                                                  float t_) noexcept {
        const Float2 a = Lerp(p0_, c_, t_);
        const Float2 b = Lerp(c_, p1_, t_);
        return Lerp(a, b, t_);
    }

    [[nodiscard]] static Float2 EvaluateCubic(const Float2& p0_,
                                              const Float2& c0_,
                                              const Float2& c1_,
                                              const Float2& p1_,
                                              float t_) noexcept {
        const Float2 a = Lerp(p0_, c0_, t_);
        const Float2 b = Lerp(c0_, c1_, t_);
        const Float2 c = Lerp(c1_, p1_, t_);
        const Float2 d = Lerp(a, b, t_);
        const Float2 e = Lerp(b, c, t_);
        return Lerp(d, e, t_);
    }

    [[nodiscard]] static bool EmitPathPrimitives(const GeometryType& component_,
                                                 std::uint32_t component_index_,
                                                 std::uint32_t quad_subdivision_,
                                                 std::uint32_t cubic_subdivision_,
                                                 std::uint32_t max_primitives_per_component_,
                                                 float zero_length_epsilon_,
                                                 ScratchType& scratch_) {
        PathPenState state{};
        const std::uint32_t primitive_begin = static_cast<std::uint32_t>(scratch_.primitives.size());
        bool truncated = false;

        GeometryPathSystem::ForEachCommandRaw(component_,
                                              [&](const GeometryPathCommandView& command_view_) {
                                                  const std::uint32_t emitted_count =
                                                      static_cast<std::uint32_t>(scratch_.primitives.size()) -
                                                      primitive_begin;
                                                  if (max_primitives_per_component_ > 0U &&
                                                      emitted_count >= max_primitives_per_component_) {
                                                      truncated = true;
                                                      return;
                                                  }

                                                  switch (command_view_.type) {
                                                  case GeometryPathCommandType::move_to:
                                                      if (command_view_.size_bytes == sizeof(GeometryPathMoveToCommand)) {
                                                          const auto* command =
                                                              reinterpret_cast<const GeometryPathMoveToCommand*>(
                                                                  command_view_.bytes);
                                                          state.current_point = command->to;
                                                          state.subpath_start = command->to;
                                                          state.has_current = true;
                                                      }
                                                      break;
                                                  case GeometryPathCommandType::line_to:
                                                      if (command_view_.size_bytes == sizeof(GeometryPathLineToCommand)) {
                                                          const auto* command =
                                                              reinterpret_cast<const GeometryPathLineToCommand*>(
                                                                  command_view_.bytes);
                                                          if (!state.has_current) {
                                                              state.current_point = command->to;
                                                              state.subpath_start = command->to;
                                                              state.has_current = true;
                                                              break;
                                                          }
                                                          (void)TryEmitLineSegment(component_,
                                                                                   component_index_,
                                                                                   state.current_point,
                                                                                   command->to,
                                                                                   zero_length_epsilon_,
                                                                                   scratch_);
                                                          state.current_point = command->to;
                                                      }
                                                      break;
                                                  case GeometryPathCommandType::quad_to:
                                                      if (command_view_.size_bytes == sizeof(GeometryPathQuadToCommand)) {
                                                          const auto* command =
                                                              reinterpret_cast<const GeometryPathQuadToCommand*>(
                                                                  command_view_.bytes);
                                                          if (!state.has_current) {
                                                              state.current_point = command->to;
                                                              state.subpath_start = command->to;
                                                              state.has_current = true;
                                                              break;
                                                          }

                                                          Float2 previous = state.current_point;
                                                          for (std::uint32_t step = 1U; step <= quad_subdivision_; ++step) {
                                                              const float t = static_cast<float>(step) /
                                                                              static_cast<float>(quad_subdivision_);
                                                              const Float2 current =
                                                                  EvaluateQuadratic(state.current_point,
                                                                                    command->control,
                                                                                    command->to,
                                                                                    t);
                                                              (void)TryEmitLineSegment(component_,
                                                                                       component_index_,
                                                                                       previous,
                                                                                       current,
                                                                                       zero_length_epsilon_,
                                                                                       scratch_);
                                                              previous = current;
                                                          }
                                                          state.current_point = command->to;
                                                      }
                                                      break;
                                                  case GeometryPathCommandType::cubic_to:
                                                      if (command_view_.size_bytes == sizeof(GeometryPathCubicToCommand)) {
                                                          const auto* command =
                                                              reinterpret_cast<const GeometryPathCubicToCommand*>(
                                                                  command_view_.bytes);
                                                          if (!state.has_current) {
                                                              state.current_point = command->to;
                                                              state.subpath_start = command->to;
                                                              state.has_current = true;
                                                              break;
                                                          }

                                                          Float2 previous = state.current_point;
                                                          for (std::uint32_t step = 1U; step <= cubic_subdivision_; ++step) {
                                                              const float t = static_cast<float>(step) /
                                                                              static_cast<float>(cubic_subdivision_);
                                                              const Float2 current =
                                                                  EvaluateCubic(state.current_point,
                                                                                command->control0,
                                                                                command->control1,
                                                                                command->to,
                                                                                t);
                                                              (void)TryEmitLineSegment(component_,
                                                                                       component_index_,
                                                                                       previous,
                                                                                       current,
                                                                                       zero_length_epsilon_,
                                                                                       scratch_);
                                                              previous = current;
                                                          }
                                                          state.current_point = command->to;
                                                      }
                                                      break;
                                                  case GeometryPathCommandType::close:
                                                      if (command_view_.size_bytes == sizeof(GeometryPathCloseCommand) &&
                                                          state.has_current) {
                                                          (void)TryEmitLineSegment(component_,
                                                                                   component_index_,
                                                                                   state.current_point,
                                                                                   state.subpath_start,
                                                                                   zero_length_epsilon_,
                                                                                   scratch_);
                                                          state.current_point = state.subpath_start;
                                                      }
                                                      break;
                                                  default:
                                                      break;
                                                  }
                                              });
        return truncated;
    }

    static void AppendOrMergeBatch(const GeometryType& component_,
                                   std::uint64_t sort_key_,
                                   std::uint32_t component_index_,
                                   std::uint32_t primitive_begin_,
                                   std::uint32_t primitive_count_,
                                   ScratchType& scratch_) {
        if (primitive_count_ == 0U) {
            return;
        }

        const std::uint32_t params = PackStyleParams(component_);
        const std::uint32_t effective_visual_resource_id =
            ResolveEffectiveVisualResourceId(component_.runtime.route);
        if (!scratch_.draw_batches.empty()) {
            Geometry2DDrawBatch& last = scratch_.draw_batches.back();
            if (last.sort_key == sort_key_ &&
                last.geometry_id == component_.runtime.route.geometry_id &&
                last.effective_visual_resource_id == effective_visual_resource_id &&
                last.params == params &&
                last.primitive_begin + last.primitive_count == primitive_begin_) {
                last.primitive_count += primitive_count_;
                return;
            }
        }

        Geometry2DDrawBatch batch{};
        batch.sort_key = sort_key_;
        batch.primitive_begin = primitive_begin_;
        batch.primitive_count = primitive_count_;
        batch.geometry_id = component_.runtime.route.geometry_id;
        batch.effective_visual_resource_id = effective_visual_resource_id;
        batch.first_component_index = component_index_;
        batch.params = params;
        scratch_.draw_batches.push_back(batch);
    }
};

template<>
class GeometryRuntimeSystem<Dim3> final {
public:
    using GeometryType = Geometry<Dim3>;
    using TransformType = Transform<Dim3>;
    using BatchSystemType = GeometryBatchSystem<Dim3>;
    using ScratchType = Geometry3DRuntimeScratch;

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
    }

    [[nodiscard]] static Geometry3DRuntimeBuildStats Build(const GeometryType* components_,
                                                           const TransformType* transforms_,
                                                           std::uint32_t component_count_,
                                                           ScratchType& scratch_,
                                                           const Geometry3DRuntimeBuildConfig& build_config_ = {},
                                                           const Geometry3DRuntimeBuildHint& build_hint_ = {}) {
        Geometry3DRuntimeBuildStats stats{};
        stats.cache_valid_before_build = scratch_.cache.valid;

        if (components_ == nullptr || component_count_ == 0U) {
            scratch_.instances.clear();
            scratch_.draw_batches.clear();
            InvalidateCache(scratch_.cache);

            stats.cache_status = GeometryRuntimeCacheStatus::miss;
            stats.cache_miss_reason = GeometryRuntimeCacheMissReason::invalid_input;
            stats.cache_epoch = scratch_.cache.epoch;
            return stats;
        }

        const bool use_external_geometry_signature = build_hint_.use_external_geometry_signature != 0U;
        const bool use_external_transform_signature = build_hint_.use_external_transform_signature != 0U;
        const bool use_visible_component_indices = build_hint_.use_visible_component_indices != 0U;
        const bool use_external_visible_set_signature =
            build_hint_.use_external_visible_set_signature != 0U;
        const bool use_skeletal_outputs =
            build_hint_.skeletal_outputs != nullptr &&
            build_hint_.skeletal_output_count > 0U;
        const bool use_vertex_deform_outputs =
            build_hint_.vertex_deform_outputs != nullptr &&
            build_hint_.vertex_deform_output_count > 0U;
        const bool use_morph_outputs =
            build_hint_.morph_outputs != nullptr &&
            build_hint_.morph_output_count > 0U;
        const bool use_frame_sequence_outputs =
            build_hint_.frame_sequence_outputs != nullptr &&
            build_hint_.frame_sequence_output_count > 0U;
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
        const std::uint64_t geometry_signature = use_external_geometry_signature
            ? build_hint_.external_geometry_signature
            : ComputeGeometrySignature(components_,
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
        const std::uint64_t skeletal_signature = use_skeletal_outputs
            ? ComputeSkeletalSignature(components_,
                                       component_count_,
                                       candidate_component_indices,
                                       candidate_component_count,
                                       use_visible_component_indices,
                                       build_hint_.skeletal_outputs,
                                       build_hint_.skeletal_output_count)
            : 0U;
        const std::uint64_t vertex_deform_signature = use_vertex_deform_outputs
            ? ComputeVertexDeformSignature(components_,
                                           component_count_,
                                           candidate_component_indices,
                                           candidate_component_count,
                                           use_visible_component_indices,
                                           build_hint_.vertex_deform_outputs,
                                           build_hint_.vertex_deform_output_count)
            : 0U;
        const std::uint64_t morph_signature = use_morph_outputs
            ? ComputeMorphSignature(components_,
                                    component_count_,
                                    candidate_component_indices,
                                    candidate_component_count,
                                    use_visible_component_indices,
                                    build_hint_.morph_outputs,
                                    build_hint_.morph_output_count)
            : 0U;
        const std::uint64_t frame_sequence_signature = use_frame_sequence_outputs
            ? ComputeFrameSequenceSignature(components_,
                                            component_count_,
                                            candidate_component_indices,
                                            candidate_component_count,
                                            use_visible_component_indices,
                                            build_hint_.frame_sequence_outputs,
                                            build_hint_.frame_sequence_output_count)
            : 0U;
        stats.candidate_component_count = candidate_component_count;
        stats.geometry_signature = geometry_signature;
        stats.transform_signature = transform_signature;
        stats.visible_set_signature = visible_set_signature;
        stats.skeletal_signature = skeletal_signature;
        stats.vertex_deform_signature = vertex_deform_signature;
        stats.morph_signature = morph_signature;
        stats.frame_sequence_signature = frame_sequence_signature;
        stats.used_visible_component_indices = use_visible_component_indices;
        stats.geometry_signature_from_hint = use_external_geometry_signature;
        stats.transform_signature_from_hint = use_external_transform_signature;
        stats.visible_set_signature_from_hint = use_external_visible_set_signature;
        const CacheProbeResult cache_probe = ProbeCache(scratch_.cache,
                                                        components_,
                                                        transforms_,
                                                        component_count_,
                                                        geometry_signature,
                                                        frame_sequence_signature,
                                                        visible_set_signature,
                                                        candidate_component_count,
                                                        build_config_);
        stats.cache_key_matched = cache_probe.key_matched;
        if (cache_probe.key_matched) {
            if (scratch_.cache.transform_signature == transform_signature &&
                scratch_.cache.skeletal_signature == skeletal_signature &&
                scratch_.cache.vertex_deform_signature == vertex_deform_signature &&
                scratch_.cache.morph_signature == morph_signature) {
                stats = scratch_.cache.last_stats;
                stats.geometry_signature = geometry_signature;
                stats.transform_signature = transform_signature;
                stats.visible_set_signature = visible_set_signature;
                stats.skeletal_signature = skeletal_signature;
                stats.vertex_deform_signature = vertex_deform_signature;
                stats.morph_signature = morph_signature;
                stats.frame_sequence_signature = frame_sequence_signature;
                stats.cache_status = GeometryRuntimeCacheStatus::hit_reused;
                stats.cache_miss_reason = GeometryRuntimeCacheMissReason::none;
                stats.cache_reused = true;
                stats.transform_only_update = false;
                stats.transform_rewritten_instance_count = 0U;
                stats.skeletal_rewritten_instance_count = 0U;
                stats.vertex_deform_rewritten_instance_count = 0U;
                stats.morph_rewritten_instance_count = 0U;
                stats.used_visible_component_indices = use_visible_component_indices;
                stats.candidate_component_count = candidate_component_count;
                stats.cache_valid_before_build = true;
                stats.cache_key_matched = true;
                stats.cache_epoch = scratch_.cache.epoch;
                stats.geometry_signature_from_hint = use_external_geometry_signature;
                stats.transform_signature_from_hint = use_external_transform_signature;
                stats.visible_set_signature_from_hint = use_external_visible_set_signature;
                stats.transform_update_from_dirty_hint = false;
                return stats;
            }

            stats = scratch_.cache.last_stats;
            bool used_dirty_hint = false;
            const bool skeletal_changed = scratch_.cache.skeletal_signature != skeletal_signature;
            stats.transform_rewritten_instance_count =
                UpdateInstanceWorldMatrices(scratch_.instances,
                                            scratch_.cache.instance_world_revisions,
                                            scratch_.cache.component_to_instance_index,
                                            components_,
                                            transforms_,
                                            component_count_,
                                            build_hint_.skeletal_outputs,
                                            build_hint_.skeletal_output_count,
                                            build_hint_.transform_dirty_component_indices,
                                            build_hint_.transform_dirty_component_count,
                                            skeletal_changed,
                                            used_dirty_hint);
            stats.skeletal_rewritten_instance_count = stats.transform_rewritten_instance_count;
            stats.vertex_deform_rewritten_instance_count =
                UpdateInstanceVertexDeformParameters(scratch_.instances,
                                                    components_,
                                                    component_count_,
                                                    build_hint_.vertex_deform_outputs,
                                                    build_hint_.vertex_deform_output_count);
            stats.morph_rewritten_instance_count =
                UpdateInstanceMorphWeights(scratch_.instances,
                                           components_,
                                           component_count_,
                                           build_hint_.morph_outputs,
                                           build_hint_.morph_output_count);
            scratch_.cache.transform_signature = transform_signature;
            scratch_.cache.skeletal_signature = skeletal_signature;
            scratch_.cache.vertex_deform_signature = vertex_deform_signature;
            scratch_.cache.morph_signature = morph_signature;
            stats.geometry_signature = geometry_signature;
            stats.transform_signature = transform_signature;
            stats.visible_set_signature = visible_set_signature;
            stats.skeletal_signature = skeletal_signature;
            stats.vertex_deform_signature = vertex_deform_signature;
            stats.morph_signature = morph_signature;
            stats.frame_sequence_signature = frame_sequence_signature;
            stats.cache_status =
                (stats.transform_rewritten_instance_count > 0U ||
                 stats.vertex_deform_rewritten_instance_count > 0U ||
                 stats.morph_rewritten_instance_count > 0U)
                    ? GeometryRuntimeCacheStatus::hit_partial_update
                    : GeometryRuntimeCacheStatus::hit_reused;
            stats.cache_miss_reason = GeometryRuntimeCacheMissReason::none;
            stats.cache_reused = true;
            stats.transform_only_update =
                (stats.transform_rewritten_instance_count > 0U ||
                 stats.skeletal_rewritten_instance_count > 0U) &&
                stats.vertex_deform_rewritten_instance_count == 0U &&
                stats.morph_rewritten_instance_count == 0U;
            stats.used_visible_component_indices = use_visible_component_indices;
            stats.candidate_component_count = candidate_component_count;
            stats.cache_valid_before_build = true;
            stats.cache_key_matched = true;
            stats.cache_epoch = scratch_.cache.epoch;
            stats.geometry_signature_from_hint = use_external_geometry_signature;
            stats.transform_signature_from_hint = use_external_transform_signature;
            stats.visible_set_signature_from_hint = use_external_visible_set_signature;
            stats.transform_update_from_dirty_hint = used_dirty_hint;
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
        if (stats.batch.visible_count == 0U) {
            stats.cache_status = GeometryRuntimeCacheStatus::miss;
            stats.cache_miss_reason = cache_probe.miss_reason;
            stats.cache_reused = false;
            stats.transform_only_update = false;
            stats.geometry_signature = geometry_signature;
            stats.transform_signature = transform_signature;
            stats.visible_set_signature = visible_set_signature;
            stats.skeletal_signature = skeletal_signature;
            stats.vertex_deform_signature = vertex_deform_signature;
            stats.morph_signature = morph_signature;
            stats.frame_sequence_signature = frame_sequence_signature;
            stats.used_visible_component_indices = use_visible_component_indices;
            stats.candidate_component_count = candidate_component_count;
            stats.cache_valid_before_build = scratch_.cache.valid;
            stats.cache_key_matched = false;
            stats.geometry_signature_from_hint = use_external_geometry_signature;
            stats.transform_signature_from_hint = use_external_transform_signature;
            stats.visible_set_signature_from_hint = use_external_visible_set_signature;
            stats.transform_update_from_dirty_hint = false;
            InitializeComponentToInstanceMap(scratch_.cache.component_to_instance_index,
                                             component_count_);
            CommitMissCache(scratch_.cache,
                            components_,
                            transforms_,
                            component_count_,
                            geometry_signature,
                            transform_signature,
                            visible_set_signature,
                            skeletal_signature,
                            vertex_deform_signature,
                            morph_signature,
                            frame_sequence_signature,
                            candidate_component_count,
                            build_config_,
                            stats);
            return stats;
        }

        const GeometryBatchItem* sorted_items = BatchSystemType::SortedItems(scratch_.batch_scratch);
        const std::uint32_t sorted_count = BatchSystemType::VisibleCount(scratch_.batch_scratch);
        scratch_.instances.resize(sorted_count);
        InitializeComponentToInstanceMap(scratch_.cache.component_to_instance_index,
                                         component_count_);

        for (std::uint32_t i = 0U; i < sorted_count; ++i) {
            const GeometryBatchItem& item = sorted_items[i];
            const GeometryType& component = components_[item.component_index];

            Matrix4x4 world_matrix = spatial_math::IdentityMatrix4x4();
            if (transforms_ != nullptr && item.component_index < component_count_) {
                world_matrix = transforms_[item.component_index].runtime.world_matrix;
            }
            world_matrix = ComposeEffectiveWorldMatrix(world_matrix,
                                                       component,
                                                       item.component_index,
                                                       build_hint_.skeletal_outputs,
                                                       build_hint_.skeletal_output_count);

            Geometry3DGpuInstance instance{};
            WriteWorldMatrixToInstance(instance, world_matrix);
            instance.params = PackParams(component);
            instance.geometry_id = component.runtime.route.geometry_id;
            instance.effective_visual_resource_id =
                ResolveEffectiveVisualResourceId(component.runtime.route);
            instance.submesh_index = ResolveAnimatedSubmeshIndex(component,
                                                                 item.component_index,
                                                                 build_hint_.frame_sequence_outputs,
                                                                 build_hint_.frame_sequence_output_count);
            instance.appearance_record_index = invalid_appearance_index;
            instance.component_index = item.component_index;
            instance.reserved0 = 0U;
            const DeformInstanceParams deform_params = ResolveVertexDeformInstanceParams(
                component,
                item.component_index,
                build_hint_.vertex_deform_outputs,
                build_hint_.vertex_deform_output_count);
            WriteVertexDeformParamsToInstance(instance, deform_params);
            if (deform_params.enabled) {
                ++stats.vertex_deform_animated_instance_count;
            }
            const MorphInstanceWeights morph_weights = ResolveMorphInstanceWeights(component,
                                                                                  item.component_index,
                                                                                  build_hint_.morph_outputs,
                                                                                  build_hint_.morph_output_count);
            WriteMorphWeightsToInstance(instance, morph_weights);
            if (morph_weights.enabled) {
                ++stats.morph_animated_instance_count;
            }
            if (IsSkeletalRootMotionAnimated(component,
                                             item.component_index,
                                             build_hint_.skeletal_outputs,
                                             build_hint_.skeletal_output_count)) {
                ++stats.skeletal_animated_instance_count;
            }
            if (IsFrameSequenceAnimated(component,
                                        item.component_index,
                                        build_hint_.frame_sequence_outputs,
                                        build_hint_.frame_sequence_output_count)) {
                ++stats.frame_sequence_animated_instance_count;
            }
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

        for (const Geometry3DDrawBatch& batch : scratch_.draw_batches) {
            if ((batch.params & 0x1U) != 0U) {
                ++stats.depth_test_batch_count;
            }
            if ((batch.params & 0x2U) != 0U) {
                ++stats.depth_write_batch_count;
            }
            if ((batch.params & 0x8U) != 0U) {
                ++stats.shadow_cast_batch_count;
            }
        }

        stats.emitted_instance_count = static_cast<std::uint32_t>(scratch_.instances.size());
        stats.emitted_batch_count = static_cast<std::uint32_t>(scratch_.draw_batches.size());
        stats.transform_rewritten_instance_count = stats.emitted_instance_count;
        stats.skeletal_rewritten_instance_count = stats.skeletal_animated_instance_count;
        stats.vertex_deform_rewritten_instance_count = stats.vertex_deform_animated_instance_count;
        stats.morph_rewritten_instance_count = stats.morph_animated_instance_count;
        stats.geometry_signature = geometry_signature;
        stats.transform_signature = transform_signature;
        stats.visible_set_signature = visible_set_signature;
        stats.skeletal_signature = skeletal_signature;
        stats.vertex_deform_signature = vertex_deform_signature;
        stats.morph_signature = morph_signature;
        stats.frame_sequence_signature = frame_sequence_signature;
        stats.cache_status = GeometryRuntimeCacheStatus::miss;
        stats.cache_miss_reason = cache_probe.miss_reason;
        stats.cache_reused = false;
        stats.transform_only_update = false;
        stats.used_visible_component_indices = use_visible_component_indices;
        stats.candidate_component_count = candidate_component_count;
        stats.cache_valid_before_build = scratch_.cache.valid;
        stats.cache_key_matched = false;
        stats.geometry_signature_from_hint = use_external_geometry_signature;
        stats.transform_signature_from_hint = use_external_transform_signature;
        stats.visible_set_signature_from_hint = use_external_visible_set_signature;
        stats.transform_update_from_dirty_hint = false;

        CommitMissCache(scratch_.cache,
                        components_,
                        transforms_,
                        component_count_,
                        geometry_signature,
                        transform_signature,
                        visible_set_signature,
                        skeletal_signature,
                        vertex_deform_signature,
                        morph_signature,
                        frame_sequence_signature,
                        candidate_component_count,
                        build_config_,
                        stats);
        return stats;
    }

private:
    struct CacheProbeResult final {
        bool key_matched = false;
        GeometryRuntimeCacheMissReason miss_reason = GeometryRuntimeCacheMissReason::none;
    };

    struct AuthoredAppearanceState final {
        Rgba8 base_color{255U, 255U, 255U, 255U};
        float metallic = 0.0F;
        float roughness = 1.0F;
        float normal_scale = 1.0F;
        float occlusion_strength = 1.0F;
        bool unlit = false;
    };

    static constexpr std::uint32_t invalid_instance_index = std::numeric_limits<std::uint32_t>::max();

    static void InitializeComponentToInstanceMap(
        GeometryRuntimeMcVector<std::uint32_t>& component_to_instance_index_,
        std::uint32_t component_count_) {
        component_to_instance_index_.resize(static_cast<std::size_t>(component_count_));
        std::fill(component_to_instance_index_.begin(),
                  component_to_instance_index_.end(),
                  invalid_instance_index);
    }

    static void InvalidateCache(Geometry3DRuntimeCache& cache_) {
        cache_.components = nullptr;
        cache_.transforms = nullptr;
        cache_.component_count = 0U;
        cache_.candidate_component_count = 0U;
        cache_.geometry_signature = 0U;
        cache_.transform_signature = 0U;
        cache_.visible_set_signature = 0U;
        cache_.skeletal_signature = 0U;
        cache_.vertex_deform_signature = 0U;
        cache_.morph_signature = 0U;
        cache_.frame_sequence_signature = 0U;
        cache_.epoch = 0U;
        cache_.instance_world_revisions.clear();
        cache_.component_to_instance_index.clear();
        cache_.valid = false;
    }

    static void CommitMissCache(Geometry3DRuntimeCache& cache_,
                                const GeometryType* components_,
                                const TransformType* transforms_,
                                std::uint32_t component_count_,
                                std::uint64_t geometry_signature_,
                                std::uint64_t transform_signature_,
                                std::uint64_t visible_set_signature_,
                                std::uint64_t skeletal_signature_,
                                std::uint64_t vertex_deform_signature_,
                                std::uint64_t morph_signature_,
                                std::uint64_t frame_sequence_signature_,
                                std::uint32_t candidate_component_count_,
                                const Geometry3DRuntimeBuildConfig& build_config_,
                                Geometry3DRuntimeBuildStats& stats_) {
        cache_.components = components_;
        cache_.transforms = transforms_;
        cache_.component_count = component_count_;
        cache_.candidate_component_count = candidate_component_count_;
        cache_.geometry_signature = geometry_signature_;
        cache_.transform_signature = transform_signature_;
        cache_.visible_set_signature = visible_set_signature_;
        cache_.skeletal_signature = skeletal_signature_;
        cache_.vertex_deform_signature = vertex_deform_signature_;
        cache_.morph_signature = morph_signature_;
        cache_.frame_sequence_signature = frame_sequence_signature_;
        cache_.build_config = build_config_;
        cache_.epoch = NextRuntimeCacheEpoch(cache_.epoch);
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

    [[nodiscard]] static bool HasLinkedAppearancePath(const GeometryType& component_) noexcept {
        return component_.runtime.route.appearance_handle.index != invalid_appearance_handle.index &&
               component_.runtime.route.appearance_handle.generation != 0U;
    }

    [[nodiscard]] static AuthoredAppearanceState ResolveAuthoredAppearanceState(
        const GeometryType& component_) noexcept {
        if (HasLinkedAppearancePath(component_)) {
            return AuthoredAppearanceState{};
        }
        const AppearanceRuntimeBridge3D appearance_bridge =
            ReadAppearanceRuntimeBridge3D(component_.runtime);
        return AuthoredAppearanceState{
            .base_color = ResolveAppearanceRuntimeBridge3DBaseColor(appearance_bridge),
            .metallic = appearance_bridge.metallic,
            .roughness = appearance_bridge.roughness,
            .normal_scale = appearance_bridge.normal_scale,
            .occlusion_strength = appearance_bridge.occlusion_strength,
            .unlit = IsAppearanceRuntimeBridge3DUnlit(appearance_bridge)
        };
    }

    [[nodiscard]] static std::uint32_t PackParams(const GeometryType& component_) noexcept {
        const AppearanceRuntimeBridge3D appearance_bridge =
            ReadAppearanceRuntimeBridge3D(component_.runtime);
        RuntimeBlendPreset blend_preset = ResolveRuntimeBlendPreset(appearance_bridge);
        if (HasLinkedAppearancePath(component_)) {
            blend_preset = ResolveRuntimeBlendPreset(component_.runtime.route.appearance_pipeline_bucket);
        }
        const AuthoredAppearanceState authored_appearance =
            ResolveAuthoredAppearanceState(component_);
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
        params |= IsAppearanceRuntimeBridge3DCastShadowEnabled(appearance_bridge)
            ? 0x8U
            : 0U;
        params |= IsAppearanceRuntimeBridge3DReceiveShadowEnabled(appearance_bridge)
            ? 0x10U
            : 0U;
        params |= (static_cast<std::uint32_t>(component_.style.topology) & 0x3U) << 5U;
        params |= (!authored_appearance.unlit ? 1U : 0U) << 7U;
        params |= (static_cast<std::uint32_t>(component_.mesh.lod_index) & 0xFFFFU) << 8U;
        params |= EncodeRuntimeBlendPresetBits(blend_preset, geometry_runtime_blend_shift);
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

    [[nodiscard]] static bool IsBuildConfigEqual(const Geometry3DRuntimeBuildConfig& lhs_,
                                                 const Geometry3DRuntimeBuildConfig& rhs_) noexcept {
        return lhs_.build_ordered_indices == rhs_.build_ordered_indices;
    }

    [[nodiscard]] static CacheProbeResult ProbeCache(const Geometry3DRuntimeCache& cache_,
                                                     const GeometryType* components_,
                                                     const TransformType* transforms_,
                                                     std::uint32_t component_count_,
                                                     std::uint64_t geometry_signature_,
                                                     std::uint64_t frame_sequence_signature_,
                                                     std::uint64_t visible_set_signature_,
                                                     std::uint32_t candidate_component_count_,
                                                     const Geometry3DRuntimeBuildConfig& build_config_) noexcept {
        if (!cache_.valid) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::cold_start
            };
        }
        if (cache_.components != components_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::components_pointer_changed
            };
        }
        if (cache_.transforms != transforms_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::transforms_pointer_changed
            };
        }
        if (cache_.component_count != component_count_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::component_count_changed
            };
        }
        if (cache_.geometry_signature != geometry_signature_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::geometry_signature_changed
            };
        }
        if (cache_.frame_sequence_signature != frame_sequence_signature_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::animation_signature_changed
            };
        }
        if (cache_.visible_set_signature != visible_set_signature_ ||
            cache_.candidate_component_count != candidate_component_count_) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::visibility_signature_changed
            };
        }
        if (!IsBuildConfigEqual(cache_.build_config, build_config_)) {
            return CacheProbeResult{
                .key_matched = false,
                .miss_reason = GeometryRuntimeCacheMissReason::build_config_changed
            };
        }
        return CacheProbeResult{
            .key_matched = true,
            .miss_reason = GeometryRuntimeCacheMissReason::none
        };
    }

    [[nodiscard]] static std::uint64_t ComputeVisibleSetSignature(
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_component_count_,
        bool use_candidate_indices_) noexcept {
        if (!use_candidate_indices_) {
            return 0U;
        }

        std::uint64_t hash = 0x9a3f2bc8d4e5f607ULL;
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

    static void HashGeometryComponent(std::uint64_t& hash_,
                                      const GeometryType& component_) noexcept {
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.route.visible));
        if (component_.runtime.route.visible == 0U) {
            return;
        }
        const AuthoredAppearanceState authored_appearance =
            ResolveAuthoredAppearanceState(component_);

        HashCombine(hash_, component_.runtime.route.sort_key);
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.route.geometry_id));
        HashCombine(hash_,
                    static_cast<std::uint64_t>(ResolveEffectiveVisualResourceId(component_.runtime.route)));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.route.user_data));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.mesh.submesh_index));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.mesh.lod_index));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.mesh.flags));
        HashCombine(hash_, static_cast<std::uint64_t>(component_.runtime.mesh_revision));
        HashCombine(hash_, static_cast<std::uint64_t>(PackRgba8(authored_appearance.base_color)));
        HashCombine(hash_, static_cast<std::uint64_t>(PackParams(component_)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(authored_appearance.metallic)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(authored_appearance.roughness)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(authored_appearance.normal_scale)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(authored_appearance.occlusion_strength)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.style.line_width)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.runtime.bounds_min.x)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.runtime.bounds_min.y)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.runtime.bounds_min.z)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.runtime.bounds_max.x)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.runtime.bounds_max.y)));
        HashCombine(hash_, static_cast<std::uint64_t>(FloatBits(component_.runtime.bounds_max.z)));
    }

    struct DeformInstanceParams final {
        Float4 params0{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        Float4 params1{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        bool enabled = false;
    };

    struct MorphInstanceWeights final {
        float weight0 = 0.0F;
        float weight1 = 0.0F;
        bool enabled = false;
    };

    [[nodiscard]] static bool IsVertexDeformEnabled(const GeometryType& component_) noexcept {
        return (component_.mesh.flags & geometry_mesh_vertex_deform_shader_flag) != 0U;
    }

    [[nodiscard]] static bool IsFrameSequenceSubmeshEnabled(const GeometryType& component_) noexcept {
        return (component_.mesh.flags & geometry_mesh_frame_sequence_submesh_flag) != 0U;
    }

    [[nodiscard]] static bool IsSkeletalRootMotionEnabled(const GeometryType& component_) noexcept {
        return (component_.mesh.flags & geometry_mesh_skeletal_root_motion_flag) != 0U;
    }

    [[nodiscard]] static bool IsMorphEnabled(const GeometryType& component_) noexcept {
        return (component_.mesh.flags & geometry_mesh_morph_targets_flag) != 0U;
    }

    [[nodiscard]] static const VertexDeformOutputState* ResolveVertexDeformOutput(
        std::uint32_t component_index_,
        const VertexDeformOutputState* outputs_,
        std::uint32_t output_count_) noexcept {
        if (outputs_ == nullptr || component_index_ >= output_count_) {
            return nullptr;
        }
        return outputs_ + component_index_;
    }

    [[nodiscard]] static const FrameSequenceOutputState* ResolveFrameSequenceOutput(
        std::uint32_t component_index_,
        const FrameSequenceOutputState* outputs_,
        std::uint32_t output_count_) noexcept {
        if (outputs_ == nullptr || component_index_ >= output_count_) {
            return nullptr;
        }
        return outputs_ + component_index_;
    }

    [[nodiscard]] static const SkeletalPoseOutputState<Dim3>* ResolveSkeletalOutput(
        std::uint32_t component_index_,
        const SkeletalPoseOutputState<Dim3>* outputs_,
        std::uint32_t output_count_) noexcept {
        if (outputs_ == nullptr || component_index_ >= output_count_) {
            return nullptr;
        }
        return outputs_ + component_index_;
    }

    [[nodiscard]] static bool IsSkeletalRootMotionAnimated(
        const GeometryType& component_,
        std::uint32_t component_index_,
        const SkeletalPoseOutputState<Dim3>* outputs_,
        std::uint32_t output_count_) noexcept {
        if (!IsSkeletalRootMotionEnabled(component_)) {
            return false;
        }
        const SkeletalPoseOutputState<Dim3>* output = ResolveSkeletalOutput(component_index_,
                                                                            outputs_,
                                                                            output_count_);
        return output != nullptr &&
               output->joints != nullptr &&
               output->sampled_joint_count > 0U &&
               output->joint_count > 0U;
    }

    [[nodiscard]] static Matrix4x4 ComposeSkeletalRootMotionMatrix(
        const SkeletalPoseOutputState<Dim3>& output_) noexcept {
        if (output_.joints == nullptr || output_.sampled_joint_count == 0U || output_.joint_count == 0U) {
            return spatial_math::IdentityMatrix4x4();
        }
        const SkeletalJointPose<Dim3>& root = output_.joints[0U];
        return spatial_math::ComposeMatrix4x4Trs(root.position, root.rotation, root.scale);
    }

    [[nodiscard]] static Matrix4x4 ComposeEffectiveWorldMatrix(
        const Matrix4x4& base_world_matrix_,
        const GeometryType& component_,
        std::uint32_t component_index_,
        const SkeletalPoseOutputState<Dim3>* skeletal_outputs_,
        std::uint32_t skeletal_output_count_) noexcept {
        if (!IsSkeletalRootMotionEnabled(component_)) {
            return base_world_matrix_;
        }
        const SkeletalPoseOutputState<Dim3>* output = ResolveSkeletalOutput(component_index_,
                                                                            skeletal_outputs_,
                                                                            skeletal_output_count_);
        if (output == nullptr || output->joints == nullptr || output->sampled_joint_count == 0U || output->joint_count == 0U) {
            return base_world_matrix_;
        }
        const Matrix4x4 root_motion = ComposeSkeletalRootMotionMatrix(*output);
        return spatial_math::MultiplyMatrix4x4(base_world_matrix_, root_motion);
    }

    [[nodiscard]] static DeformInstanceParams ResolveVertexDeformInstanceParams(
        const GeometryType& component_,
        std::uint32_t component_index_,
        const VertexDeformOutputState* outputs_,
        std::uint32_t output_count_) noexcept {
        DeformInstanceParams result{};
        if (!IsVertexDeformEnabled(component_)) {
            return result;
        }

        const VertexDeformOutputState* output = ResolveVertexDeformOutput(component_index_,
                                                                          outputs_,
                                                                          output_count_);
        if (output == nullptr || output->parameters == nullptr || output->sampled_parameter_count == 0U) {
            return result;
        }

        result.params0 = output->parameters[0U];
        if (output->sampled_parameter_count > 1U && output->parameter_count > 1U) {
            result.params1 = output->parameters[1U];
        }
        result.enabled = true;
        return result;
    }

    [[nodiscard]] static bool IsFrameSequenceAnimated(const GeometryType& component_,
                                                      std::uint32_t component_index_,
                                                      const FrameSequenceOutputState* outputs_,
                                                      std::uint32_t output_count_) noexcept {
        if (!IsFrameSequenceSubmeshEnabled(component_)) {
            return false;
        }
        const FrameSequenceOutputState* output = ResolveFrameSequenceOutput(component_index_,
                                                                            outputs_,
                                                                            output_count_);
        return output != nullptr && output->frame_count > 0U;
    }

    [[nodiscard]] static std::uint32_t ResolveAnimatedSubmeshIndex(
        const GeometryType& component_,
        std::uint32_t component_index_,
        const FrameSequenceOutputState* outputs_,
        std::uint32_t output_count_) noexcept {
        if (!IsFrameSequenceSubmeshEnabled(component_)) {
            return component_.mesh.submesh_index;
        }

        const FrameSequenceOutputState* output = ResolveFrameSequenceOutput(component_index_,
                                                                            outputs_,
                                                                            output_count_);
        if (output == nullptr || output->frame_count == 0U) {
            return component_.mesh.submesh_index;
        }
        return component_.mesh.submesh_index + output->frame_index_a;
    }

    static void WriteVertexDeformParamsToInstance(Geometry3DGpuInstance& instance_,
                                                  const DeformInstanceParams& params_) noexcept {
        instance_.deform_param0_x = params_.params0.x;
        instance_.deform_param0_y = params_.params0.y;
        instance_.deform_param0_z = params_.params0.z;
        instance_.deform_param0_w = params_.params0.w;
        instance_.deform_param1_x = params_.params1.x;
        instance_.deform_param1_y = params_.params1.y;
        instance_.deform_param1_z = params_.params1.z;
        instance_.deform_param1_w = params_.params1.w;
    }

    [[nodiscard]] static MorphInstanceWeights ResolveMorphInstanceWeights(
        const GeometryType& component_,
        std::uint32_t component_index_,
        const MorphWeightOutputState* outputs_,
        std::uint32_t output_count_) noexcept {
        MorphInstanceWeights result{};
        if (!IsMorphEnabled(component_)) {
            return result;
        }
        if (outputs_ == nullptr || component_index_ >= output_count_) {
            return result;
        }
        const MorphWeightOutputState& output = outputs_[component_index_];
        if (output.weights == nullptr || output.sampled_weight_count == 0U || output.weight_count == 0U) {
            return result;
        }
        result.weight0 = output.weights[0U];
        if (output.sampled_weight_count > 1U && output.weight_count > 1U) {
            result.weight1 = output.weights[1U];
        }
        result.enabled = (result.weight0 != 0.0F) || (result.weight1 != 0.0F);
        return result;
    }

    static void WriteMorphWeightsToInstance(Geometry3DGpuInstance& instance_,
                                            const MorphInstanceWeights& weights_) noexcept {
        instance_.morph_weight0 = weights_.weight0;
        instance_.morph_weight1 = weights_.weight1;
        instance_.morph_weight_reserved0 = 0.0F;
        instance_.morph_weight_reserved1 = 0.0F;
    }

    [[nodiscard]] static bool InstanceVertexDeformParamsEqual(const Geometry3DGpuInstance& instance_,
                                                              const DeformInstanceParams& params_) noexcept {
        return instance_.deform_param0_x == params_.params0.x &&
               instance_.deform_param0_y == params_.params0.y &&
               instance_.deform_param0_z == params_.params0.z &&
               instance_.deform_param0_w == params_.params0.w &&
               instance_.deform_param1_x == params_.params1.x &&
               instance_.deform_param1_y == params_.params1.y &&
               instance_.deform_param1_z == params_.params1.z &&
               instance_.deform_param1_w == params_.params1.w;
    }

    [[nodiscard]] static bool InstanceMorphWeightsEqual(const Geometry3DGpuInstance& instance_,
                                                        const MorphInstanceWeights& weights_) noexcept {
        return instance_.morph_weight0 == weights_.weight0 &&
               instance_.morph_weight1 == weights_.weight1;
    }

    [[nodiscard]] static std::uint64_t ComputeGeometrySignature(
        const GeometryType* components_,
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
                HashGeometryComponent(hash, components_[i]);
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
            HashGeometryComponent(hash, components_[component_index]);
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeTransformSignature(const GeometryType* components_,
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

    [[nodiscard]] static std::uint64_t ComputeVertexDeformSignature(
        const GeometryType* components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_component_count_,
        bool use_candidate_indices_,
        const VertexDeformOutputState* outputs_,
        std::uint32_t output_count_) noexcept {
        std::uint64_t hash = 0xa8d6f1b34c2e9087ULL;
        if (components_ == nullptr || component_count_ == 0U || outputs_ == nullptr || output_count_ == 0U) {
            return hash;
        }

        const auto hash_component = [&](std::uint32_t component_index_) noexcept {
            if (component_index_ >= component_count_) {
                return;
            }
            const GeometryType& component = components_[component_index_];
            if (component.runtime.route.visible == 0U || !IsVertexDeformEnabled(component)) {
                return;
            }

            const DeformInstanceParams params = ResolveVertexDeformInstanceParams(component,
                                                                                  component_index_,
                                                                                  outputs_,
                                                                                  output_count_);
            HashCombine(hash, static_cast<std::uint64_t>(component_index_));
            HashCombine(hash, static_cast<std::uint64_t>(params.enabled ? 1U : 0U));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(params.params0.x)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(params.params0.y)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(params.params0.z)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(params.params0.w)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(params.params1.x)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(params.params1.y)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(params.params1.z)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(params.params1.w)));
        };

        if (!use_candidate_indices_) {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                hash_component(i);
            }
            return hash;
        }

        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            return hash;
        }
        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            hash_component(candidate_component_indices_[i]);
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeMorphSignature(
        const GeometryType* components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_component_count_,
        bool use_candidate_indices_,
        const MorphWeightOutputState* outputs_,
        std::uint32_t output_count_) noexcept {
        std::uint64_t hash = 0xb15f4d80c3e27a91ULL;
        if (components_ == nullptr || component_count_ == 0U || outputs_ == nullptr || output_count_ == 0U) {
            return hash;
        }

        const auto hash_component = [&](std::uint32_t component_index_) noexcept {
            if (component_index_ >= component_count_) {
                return;
            }
            const GeometryType& component = components_[component_index_];
            if (component.runtime.route.visible == 0U || !IsMorphEnabled(component)) {
                return;
            }
            const MorphInstanceWeights weights = ResolveMorphInstanceWeights(component,
                                                                            component_index_,
                                                                            outputs_,
                                                                            output_count_);
            HashCombine(hash, static_cast<std::uint64_t>(component_index_));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(weights.weight0)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(weights.weight1)));
        };

        if (!use_candidate_indices_) {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                hash_component(i);
            }
            return hash;
        }

        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            return hash;
        }
        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            hash_component(candidate_component_indices_[i]);
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeFrameSequenceSignature(
        const GeometryType* components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_component_count_,
        bool use_candidate_indices_,
        const FrameSequenceOutputState* outputs_,
        std::uint32_t output_count_) noexcept {
        std::uint64_t hash = 0x7c51de93ab40621fULL;
        if (components_ == nullptr || component_count_ == 0U || outputs_ == nullptr || output_count_ == 0U) {
            return hash;
        }

        const auto hash_component = [&](std::uint32_t component_index_) noexcept {
            if (component_index_ >= component_count_) {
                return;
            }
            const GeometryType& component = components_[component_index_];
            if (component.runtime.route.visible == 0U || !IsFrameSequenceSubmeshEnabled(component)) {
                return;
            }

            const FrameSequenceOutputState* output = ResolveFrameSequenceOutput(component_index_,
                                                                                outputs_,
                                                                                output_count_);
            HashCombine(hash, static_cast<std::uint64_t>(component_index_));
            if (output == nullptr) {
                HashCombine(hash, 0xffffffffffffffffULL);
                return;
            }
            HashCombine(hash, static_cast<std::uint64_t>(output->frame_index_a));
            HashCombine(hash, static_cast<std::uint64_t>(output->frame_index_b));
            HashCombine(hash, static_cast<std::uint64_t>(output->frame_count));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(output->blend_alpha)));
        };

        if (!use_candidate_indices_) {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                hash_component(i);
            }
            return hash;
        }

        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            return hash;
        }
        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            hash_component(candidate_component_indices_[i]);
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeSkeletalSignature(
        const GeometryType* components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_component_count_,
        bool use_candidate_indices_,
        const SkeletalPoseOutputState<Dim3>* outputs_,
        std::uint32_t output_count_) noexcept {
        std::uint64_t hash = 0x6f13c2a9d4875be1ULL;
        if (components_ == nullptr || component_count_ == 0U || outputs_ == nullptr || output_count_ == 0U) {
            return hash;
        }

        const auto hash_component = [&](std::uint32_t component_index_) noexcept {
            if (component_index_ >= component_count_) {
                return;
            }
            const GeometryType& component = components_[component_index_];
            if (component.runtime.route.visible == 0U || !IsSkeletalRootMotionEnabled(component)) {
                return;
            }
            const SkeletalPoseOutputState<Dim3>* output = ResolveSkeletalOutput(component_index_,
                                                                                outputs_,
                                                                                output_count_);
            HashCombine(hash, static_cast<std::uint64_t>(component_index_));
            if (output == nullptr || output->joints == nullptr || output->sampled_joint_count == 0U || output->joint_count == 0U) {
                HashCombine(hash, 0xffffffffffffffffULL);
                return;
            }
            const SkeletalJointPose<Dim3>& root = output->joints[0U];
            HashCombine(hash, static_cast<std::uint64_t>(output->sampled_joint_count));
            HashCombine(hash, static_cast<std::uint64_t>(output->revision));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(root.position.x)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(root.position.y)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(root.position.z)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(root.rotation.x)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(root.rotation.y)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(root.rotation.z)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(root.rotation.w)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(root.scale.x)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(root.scale.y)));
            HashCombine(hash, static_cast<std::uint64_t>(FloatBits(root.scale.z)));
        };

        if (!use_candidate_indices_) {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                hash_component(i);
            }
            return hash;
        }

        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            return hash;
        }
        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            hash_component(candidate_component_indices_[i]);
        }
        return hash;
    }

    static void WriteWorldMatrixToInstance(Geometry3DGpuInstance& instance_,
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

    static void InitializeInstanceWorldRevisionCache(GeometryRuntimeMcVector<std::uint32_t>& revisions_,
                                                     const GeometryRuntimeMcVector<Geometry3DGpuInstance>& instances_,
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
        GeometryRuntimeMcVector<Geometry3DGpuInstance>& instances_,
        GeometryRuntimeMcVector<std::uint32_t>& cached_revisions_,
        const GeometryRuntimeMcVector<std::uint32_t>& component_to_instance_index_,
        const GeometryType* components_,
        const TransformType* transforms_,
        std::uint32_t component_count_,
        const SkeletalPoseOutputState<Dim3>* skeletal_outputs_,
        std::uint32_t skeletal_output_count_,
        const std::uint32_t* transform_dirty_component_indices_,
        std::uint32_t transform_dirty_component_count_,
        bool force_skeletal_rewrite_,
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
        if (!force_skeletal_rewrite_ &&
            transform_dirty_component_indices_ != nullptr &&
            transform_dirty_component_count_ > 0U &&
            component_to_instance_index_.size() == static_cast<std::size_t>(component_count_)) {
            used_dirty_hint_ = true;
            if (transform_dirty_component_count_ == 1U) {
                const std::uint32_t component_index = transform_dirty_component_indices_[0U];
                if (component_index >= component_count_) {
                    return 0U;
                }
                const std::uint32_t instance_index = component_to_instance_index_[component_index];
                if (instance_index == invalid_instance_index ||
                    instance_index >= instances_.size()) {
                    return 0U;
                }

                const std::uint32_t current_revision = has_transforms
                    ? transforms_[component_index].runtime.world_revision
                    : 0U;
                if (cached_revisions_[instance_index] == current_revision) {
                    return 0U;
                }

                Geometry3DGpuInstance& instance = instances_[instance_index];
                if (has_transforms) {
                    const Matrix4x4 effective_world = ComposeEffectiveWorldMatrix(
                        transforms_[component_index].runtime.world_matrix,
                        components_[component_index],
                        component_index,
                        skeletal_outputs_,
                        skeletal_output_count_);
                    WriteWorldMatrixToInstance(instance, effective_world);
                } else {
                    const Matrix4x4 effective_world = ComposeEffectiveWorldMatrix(identity,
                                                                                  components_[component_index],
                                                                                  component_index,
                                                                                  skeletal_outputs_,
                                                                                  skeletal_output_count_);
                    WriteWorldMatrixToInstance(instance, effective_world);
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
                if (instance_index == invalid_instance_index ||
                    instance_index >= instances_.size()) {
                    continue;
                }

                Geometry3DGpuInstance& instance = instances_[instance_index];
                const std::uint32_t current_revision = has_transforms
                    ? transforms_[component_index].runtime.world_revision
                    : 0U;
                if (cached_revisions_[instance_index] == current_revision) {
                    continue;
                }

                if (has_transforms) {
                    const Matrix4x4 effective_world = ComposeEffectiveWorldMatrix(
                        transforms_[component_index].runtime.world_matrix,
                        components_[component_index],
                        component_index,
                        skeletal_outputs_,
                        skeletal_output_count_);
                    WriteWorldMatrixToInstance(instance, effective_world);
                } else {
                    const Matrix4x4 effective_world = ComposeEffectiveWorldMatrix(identity,
                                                                                  components_[component_index],
                                                                                  component_index,
                                                                                  skeletal_outputs_,
                                                                                  skeletal_output_count_);
                    WriteWorldMatrixToInstance(instance, effective_world);
                }
                cached_revisions_[instance_index] = current_revision;
                ++rewritten_count;
            }
            return rewritten_count;
        }

        if (has_transforms) {
            for (std::size_t index = 0U; index < instances_.size(); ++index) {
                Geometry3DGpuInstance& instance = instances_[index];
                const std::uint32_t component_index = instance.component_index;
                const bool valid_component_index = component_index < component_count_;
                const std::uint32_t current_revision = valid_component_index
                    ? transforms_[component_index].runtime.world_revision
                    : 0U;
                if (!force_skeletal_rewrite_ && cached_revisions_[index] == current_revision) {
                    continue;
                }

                if (valid_component_index) {
                    const Matrix4x4 effective_world = ComposeEffectiveWorldMatrix(
                        transforms_[component_index].runtime.world_matrix,
                        components_[component_index],
                        component_index,
                        skeletal_outputs_,
                        skeletal_output_count_);
                    WriteWorldMatrixToInstance(instance, effective_world);
                } else {
                    WriteWorldMatrixToInstance(instance, identity);
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
            WriteWorldMatrixToInstance(instances_[index], identity);
            cached_revisions_[index] = 0U;
            ++rewritten_count;
        }
        return rewritten_count;
    }

    [[nodiscard]] static std::uint32_t UpdateInstanceVertexDeformParameters(
        GeometryRuntimeMcVector<Geometry3DGpuInstance>& instances_,
        const GeometryType* components_,
        std::uint32_t component_count_,
        const VertexDeformOutputState* outputs_,
        std::uint32_t output_count_) {
        if (instances_.empty() || components_ == nullptr || outputs_ == nullptr || output_count_ == 0U) {
            return 0U;
        }

        std::uint32_t rewritten_count = 0U;
        for (Geometry3DGpuInstance& instance : instances_) {
            if (instance.component_index >= component_count_) {
                continue;
            }
            const GeometryType& component = components_[instance.component_index];
            const DeformInstanceParams params = ResolveVertexDeformInstanceParams(component,
                                                                                  instance.component_index,
                                                                                  outputs_,
                                                                                  output_count_);
            if (InstanceVertexDeformParamsEqual(instance, params)) {
                continue;
            }
            WriteVertexDeformParamsToInstance(instance, params);
            ++rewritten_count;
        }
        return rewritten_count;
    }

    [[nodiscard]] static std::uint32_t UpdateInstanceMorphWeights(
        GeometryRuntimeMcVector<Geometry3DGpuInstance>& instances_,
        const GeometryType* components_,
        std::uint32_t component_count_,
        const MorphWeightOutputState* outputs_,
        std::uint32_t output_count_) {
        if (instances_.empty() || components_ == nullptr || outputs_ == nullptr || output_count_ == 0U) {
            return 0U;
        }

        std::uint32_t rewritten_count = 0U;
        for (Geometry3DGpuInstance& instance : instances_) {
            if (instance.component_index >= component_count_) {
                continue;
            }
            const GeometryType& component = components_[instance.component_index];
            const MorphInstanceWeights weights = ResolveMorphInstanceWeights(component,
                                                                             instance.component_index,
                                                                             outputs_,
                                                                             output_count_);
            if (InstanceMorphWeightsEqual(instance, weights)) {
                continue;
            }
            WriteMorphWeightsToInstance(instance, weights);
            ++rewritten_count;
        }
        return rewritten_count;
    }

    static void AppendOrMergeBatch(const GeometryType& component_,
                                   std::uint64_t sort_key_,
                                   std::uint32_t component_index_,
                                   std::uint32_t instance_index_,
                                   ScratchType& scratch_) {
        const std::uint32_t params = PackParams(component_);
        const std::uint32_t effective_visual_resource_id =
            ResolveEffectiveVisualResourceId(component_.runtime.route);
        const std::uint32_t animated_submesh_index = scratch_.instances[instance_index_].submesh_index;
        if (!scratch_.draw_batches.empty()) {
            Geometry3DDrawBatch& last = scratch_.draw_batches.back();
            if (last.sort_key == sort_key_ &&
                last.geometry_id == component_.runtime.route.geometry_id &&
                last.effective_visual_resource_id == effective_visual_resource_id &&
                last.submesh_index == animated_submesh_index &&
                last.params == params &&
                last.instance_begin + last.instance_count == instance_index_) {
                ++last.instance_count;
                return;
            }
        }

        Geometry3DDrawBatch batch{};
        batch.sort_key = sort_key_;
        batch.instance_begin = instance_index_;
        batch.instance_count = 1U;
        batch.geometry_id = component_.runtime.route.geometry_id;
        batch.effective_visual_resource_id = effective_visual_resource_id;
        batch.submesh_index = animated_submesh_index;
        batch.first_component_index = component_index_;
        batch.params = params;
        scratch_.draw_batches.push_back(batch);
    }
};

} // namespace vr::ecs

