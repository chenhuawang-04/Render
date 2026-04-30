#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/light_gpu_layout.hpp"
#include "vr/ecs/system/light_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace vr::ecs {

template<typename T>
using LightRuntimeMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct LightUploadRange final {
    std::uint32_t begin_index;
    std::uint32_t count;
};

struct LightRuntimeBuildConfig final {
    bool force_full_rebuild;
    bool rebuild_keys_even_if_clean;
    std::uint32_t merge_gap;
};

struct LightRuntimeBuildHint final {
    const std::uint32_t* dirty_component_indices;
    std::uint32_t dirty_component_count;
    std::uint8_t use_dirty_component_indices;

    const std::uint32_t* transform_dirty_component_indices;
    std::uint32_t transform_dirty_component_count;
    std::uint8_t use_transform_dirty_component_indices;
};

struct LightRuntimeBuildStats final {
    std::uint32_t component_count;
    std::uint32_t scanned_dirty_count;
    std::uint32_t scanned_transform_dirty_count;
    std::uint32_t updated_record_count;
    std::uint32_t updated_style_or_binding_count;
    std::uint32_t updated_transform_only_count;
    std::uint32_t upload_range_count;
    std::uint32_t out_of_range_dirty_count;
    std::uint64_t style_signature;
    std::uint64_t binding_signature;
    std::uint64_t transform_signature;
    std::uint32_t cache_epoch;
    bool cache_reused;
    bool full_rebuild;
    bool transform_only_update;
};

struct LightDerivedGeom2D final {
    float position_x;
    float position_y;
    float direction_x;
    float direction_y;
};

struct LightDerivedGeom3D final {
    float position_x;
    float position_y;
    float position_z;
    float direction_x;
    float direction_y;
    float direction_z;
};

struct LightDerivedOptical final {
    float radius;
    float inv_radius;
    float cone_cos_inner;
    float cone_cos_outer;
    float source_height;
    float source_radius;
    float source_length;
    float falloff_exponent;
};

template<DimensionTag DimensionT>
struct LightDerivedGeomTraits;

template<>
struct LightDerivedGeomTraits<Dim2> final {
    using GeomType = LightDerivedGeom2D;
};

template<>
struct LightDerivedGeomTraits<Dim3> final {
    using GeomType = LightDerivedGeom3D;
};

template<DimensionTag DimensionT>
using LightDerivedGeom = typename LightDerivedGeomTraits<DimensionT>::GeomType;

template<DimensionTag DimensionT>
struct LightRuntimeCache final {
    const Light<DimensionT>* components;
    const Transform<DimensionT>* transforms;
    std::uint32_t component_count;
    std::uint64_t style_signature;
    std::uint64_t binding_signature;
    std::uint64_t transform_signature;
    LightRuntimeBuildConfig build_config;
    std::uint32_t epoch;
    bool valid;
};

template<DimensionTag DimensionT>
struct LightRuntimeScratch final {
    using GpuRecordType = LightGpuRecord<DimensionT>;
    using GeomType = LightDerivedGeom<DimensionT>;

    LightRuntimeMcVector<GpuRecordType> gpu_records{};
    LightRuntimeMcVector<GeomType> derived_geom{};
    LightRuntimeMcVector<LightDerivedOptical> derived_optical{};
    LightRuntimeMcVector<LightUploadRange> upload_ranges{};
    LightRuntimeMcVector<std::uint32_t> dirty_component_indices{};
    LightRuntimeMcVector<std::uint32_t> transform_dirty_component_indices{};
    LightRuntimeMcVector<std::uint32_t> updated_component_indices{};
    LightRuntimeMcVector<std::uint32_t> marker_stamps{};
    LightRuntimeMcVector<std::uint32_t> handle_generations{};
    std::uint32_t marker_epoch = 1U;
    LightRuntimeCache<DimensionT> cache{};
};

static_assert(PurePodLightComponent<LightUploadRange>);
static_assert(PurePodLightComponent<LightRuntimeBuildConfig>);
static_assert(PurePodLightComponent<LightRuntimeBuildHint>);
static_assert(PurePodLightComponent<LightRuntimeBuildStats>);
static_assert(PurePodLightComponent<LightDerivedGeom2D>);
static_assert(PurePodLightComponent<LightDerivedGeom3D>);
static_assert(PurePodLightComponent<LightDerivedOptical>);
static_assert(PurePodLightComponent<LightRuntimeCache<Dim2>>);
static_assert(PurePodLightComponent<LightRuntimeCache<Dim3>>);

template<DimensionTag DimensionT>
class LightRuntimeSystem final {
public:
    using LightType = Light<DimensionT>;
    using TransformType = Transform<DimensionT>;
    using SystemType = LightSystem<DimensionT>;
    using GpuRecordType = LightGpuRecord<DimensionT>;
    using GeomType = LightDerivedGeom<DimensionT>;
    using ScratchType = LightRuntimeScratch<DimensionT>;

    static void Reserve(ScratchType& scratch_, std::uint32_t component_count_) {
        const std::size_t reserve_count = static_cast<std::size_t>(component_count_);
        if (scratch_.gpu_records.capacity() < reserve_count) {
            scratch_.gpu_records.reserve(reserve_count);
        }
        if (scratch_.derived_geom.capacity() < reserve_count) {
            scratch_.derived_geom.reserve(reserve_count);
        }
        if (scratch_.derived_optical.capacity() < reserve_count) {
            scratch_.derived_optical.reserve(reserve_count);
        }
        if (scratch_.dirty_component_indices.capacity() < reserve_count) {
            scratch_.dirty_component_indices.reserve(reserve_count);
        }
        if (scratch_.transform_dirty_component_indices.capacity() < reserve_count) {
            scratch_.transform_dirty_component_indices.reserve(reserve_count);
        }
        if (scratch_.updated_component_indices.capacity() < reserve_count) {
            scratch_.updated_component_indices.reserve(reserve_count);
        }
        if (scratch_.marker_stamps.capacity() < reserve_count) {
            scratch_.marker_stamps.reserve(reserve_count);
        }
        if (scratch_.handle_generations.capacity() < reserve_count) {
            scratch_.handle_generations.reserve(reserve_count);
        }
    }

    [[nodiscard]] static LightRuntimeBuildStats Build(
        LightType* components_,
        const TransformType* transforms_,
        std::uint32_t component_count_,
        ScratchType& scratch_,
        const LightRuntimeBuildConfig& build_config_ = {},
        const LightRuntimeBuildHint& build_hint_ = {}) {
        LightRuntimeBuildStats stats{};
        stats.component_count = component_count_;

        scratch_.upload_ranges.clear();
        scratch_.dirty_component_indices.clear();
        scratch_.transform_dirty_component_indices.clear();
        scratch_.updated_component_indices.clear();

        if (components_ == nullptr || component_count_ == 0U) {
            scratch_.gpu_records.clear();
            scratch_.derived_geom.clear();
            scratch_.derived_optical.clear();
            scratch_.cache = {};
            return stats;
        }

        Reserve(scratch_, component_count_);
        EnsureVectorSizes(scratch_, component_count_);

        ComputeSignatures(components_,
                          transforms_,
                          component_count_,
                          stats.style_signature,
                          stats.binding_signature,
                          stats.transform_signature);

        const bool cache_key_matched =
            scratch_.cache.valid &&
            scratch_.cache.components == components_ &&
            scratch_.cache.transforms == transforms_ &&
            scratch_.cache.component_count == component_count_ &&
            scratch_.cache.style_signature == stats.style_signature &&
            scratch_.cache.binding_signature == stats.binding_signature &&
            scratch_.cache.transform_signature == stats.transform_signature &&
            IsBuildConfigEqual(scratch_.cache.build_config, build_config_);
        if (cache_key_matched) {
            stats.cache_reused = true;
            stats.cache_epoch = scratch_.cache.epoch;
            return stats;
        }

        const bool cache_geometry_binding_matched =
            scratch_.cache.valid &&
            scratch_.cache.components == components_ &&
            scratch_.cache.transforms == transforms_ &&
            scratch_.cache.component_count == component_count_ &&
            scratch_.cache.style_signature == stats.style_signature &&
            scratch_.cache.binding_signature == stats.binding_signature &&
            IsBuildConfigEqual(scratch_.cache.build_config, build_config_);

        const bool force_full_rebuild =
            build_config_.force_full_rebuild ||
            !scratch_.cache.valid ||
            scratch_.cache.components != components_ ||
            scratch_.cache.transforms != transforms_ ||
            scratch_.cache.component_count != component_count_ ||
            !IsBuildConfigEqual(scratch_.cache.build_config, build_config_) ||
            scratch_.gpu_records.size() != static_cast<std::size_t>(component_count_) ||
            scratch_.derived_geom.size() != static_cast<std::size_t>(component_count_) ||
            scratch_.derived_optical.size() != static_cast<std::size_t>(component_count_);

        if (force_full_rebuild) {
            stats.full_rebuild = true;
            BuildFull(components_, transforms_, component_count_, scratch_, stats);
            BuildUploadRanges(scratch_.updated_component_indices,
                              build_config_.merge_gap,
                              scratch_.upload_ranges);
            stats.upload_range_count = static_cast<std::uint32_t>(scratch_.upload_ranges.size());
            CommitCache(components_, transforms_, component_count_, build_config_, stats, scratch_);
            return stats;
        }

        CollectDirtyIndices(components_,
                            component_count_,
                            build_hint_,
                            scratch_,
                            stats);
        CollectTransformDirtyIndices(component_count_,
                                     build_hint_,
                                     scratch_,
                                     stats);

        if (!scratch_.dirty_component_indices.empty()) {
            for (const std::uint32_t component_index : scratch_.dirty_component_indices) {
                if (component_index >= component_count_) {
                    ++stats.out_of_range_dirty_count;
                    continue;
                }
                UpdateOpticalFromComponent(components_[component_index],
                                           scratch_.derived_optical[component_index]);
                UpdateGeomFromTransform(component_index,
                                        transforms_,
                                        scratch_.derived_geom[component_index]);
                EncodeGpuRecord(components_[component_index],
                                scratch_.derived_geom[component_index],
                                scratch_.derived_optical[component_index],
                                scratch_.gpu_records[component_index]);
                UpdateRuntimeKeysAndHandle(components_[component_index], component_index, scratch_);
                SystemType::MarkUploaded(components_[component_index]);
                scratch_.updated_component_indices.push_back(component_index);
                ++stats.updated_record_count;
                ++stats.updated_style_or_binding_count;
            }
        }

        bool transform_only_update = false;
        if (!scratch_.transform_dirty_component_indices.empty()) {
            for (const std::uint32_t component_index : scratch_.transform_dirty_component_indices) {
                if (component_index >= component_count_) {
                    ++stats.out_of_range_dirty_count;
                    continue;
                }
                if (HasMarker(scratch_, component_index)) {
                    continue;
                }
                UpdateGeomFromTransform(component_index,
                                        transforms_,
                                        scratch_.derived_geom[component_index]);
                EncodeGpuRecord(components_[component_index],
                                scratch_.derived_geom[component_index],
                                scratch_.derived_optical[component_index],
                                scratch_.gpu_records[component_index]);
                UpdateRuntimeKeysAndHandle(components_[component_index], component_index, scratch_);
                SystemType::MarkUploaded(components_[component_index]);
                scratch_.updated_component_indices.push_back(component_index);
                ++stats.updated_record_count;
                ++stats.updated_transform_only_count;
                transform_only_update = true;
            }
        }

        stats.transform_only_update = transform_only_update &&
                                      scratch_.dirty_component_indices.empty();

        if (scratch_.updated_component_indices.empty() &&
            build_config_.rebuild_keys_even_if_clean &&
            !cache_geometry_binding_matched) {
            BuildFull(components_, transforms_, component_count_, scratch_, stats);
        }

        BuildUploadRanges(scratch_.updated_component_indices,
                          build_config_.merge_gap,
                          scratch_.upload_ranges);

        stats.upload_range_count = static_cast<std::uint32_t>(scratch_.upload_ranges.size());
        CommitCache(components_, transforms_, component_count_, build_config_, stats, scratch_);
        return stats;
    }

    [[nodiscard]] static const GpuRecordType* GpuRecords(const ScratchType& scratch_) noexcept {
        return scratch_.gpu_records.data();
    }

    [[nodiscard]] static std::uint32_t GpuRecordCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.gpu_records.size());
    }

    [[nodiscard]] static const LightDerivedOptical* DerivedOpticalData(const ScratchType& scratch_) noexcept {
        return scratch_.derived_optical.data();
    }

    [[nodiscard]] static const GeomType* DerivedGeomData(const ScratchType& scratch_) noexcept {
        return scratch_.derived_geom.data();
    }

    [[nodiscard]] static const LightUploadRange* UploadRanges(const ScratchType& scratch_) noexcept {
        return scratch_.upload_ranges.data();
    }

    [[nodiscard]] static std::uint32_t UploadRangeCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.upload_ranges.size());
    }

    [[nodiscard]] static const std::uint32_t* UpdatedComponentIndices(const ScratchType& scratch_) noexcept {
        return scratch_.updated_component_indices.data();
    }

    [[nodiscard]] static std::uint32_t UpdatedComponentIndexCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.updated_component_indices.size());
    }

private:
    static constexpr std::uint64_t hash_offset_basis = 14695981039346656037ULL;
    static constexpr std::uint64_t hash_prime = 1099511628211ULL;

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= hash_prime;
    }

    static void EnsureVectorSizes(ScratchType& scratch_, std::uint32_t component_count_) {
        const std::size_t count = static_cast<std::size_t>(component_count_);
        scratch_.gpu_records.resize(count);
        scratch_.derived_geom.resize(count);
        scratch_.derived_optical.resize(count);
        if (scratch_.marker_stamps.size() < count) {
            const std::size_t old_size = scratch_.marker_stamps.size();
            scratch_.marker_stamps.resize(count);
            for (std::size_t i = old_size; i < count; ++i) {
                scratch_.marker_stamps[i] = 0U;
            }
        }
        if (scratch_.handle_generations.size() < count) {
            const std::size_t old_size = scratch_.handle_generations.size();
            scratch_.handle_generations.resize(count);
            for (std::size_t i = old_size; i < count; ++i) {
                scratch_.handle_generations[i] = 1U;
            }
        }
    }

    static void ComputeSignatures(LightType* components_,
                                  const TransformType* transforms_,
                                  std::uint32_t component_count_,
                                  std::uint64_t& style_signature_,
                                  std::uint64_t& binding_signature_,
                                  std::uint64_t& transform_signature_) noexcept {
        std::uint64_t style_hash = hash_offset_basis;
        std::uint64_t binding_hash = hash_offset_basis;
        std::uint64_t transform_hash = hash_offset_basis;

        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            const LightType& component = components_[i];
            HashCombine(style_hash, static_cast<std::uint64_t>(component.state.revision_style));
            HashCombine(style_hash, static_cast<std::uint64_t>(component.visibility.visible));
            HashCombine(style_hash, static_cast<std::uint64_t>(component.visibility.light_channel_mask));
            HashCombine(binding_hash, static_cast<std::uint64_t>(component.state.revision_binding));
            HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.cookie.texture_id));
            HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.cookie.sampler_id));
            if constexpr (std::same_as<DimensionT, Dim2>) {
                HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.occluder.texture_id));
                HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.occluder.sampler_id));
            } else {
                HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.ies.texture_id));
                HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.shadow.texture_id));
                HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.shadow_config.resolution));
            }

            if (transforms_ != nullptr) {
                HashCombine(transform_hash, static_cast<std::uint64_t>(transforms_[i].runtime.world_revision));
            } else {
                HashCombine(transform_hash, static_cast<std::uint64_t>(i + 1U));
            }
        }

        style_signature_ = style_hash;
        binding_signature_ = binding_hash;
        transform_signature_ = transform_hash;
    }

    static bool IsBuildConfigEqual(const LightRuntimeBuildConfig& lhs_,
                                   const LightRuntimeBuildConfig& rhs_) noexcept {
        return lhs_.force_full_rebuild == rhs_.force_full_rebuild &&
               lhs_.rebuild_keys_even_if_clean == rhs_.rebuild_keys_even_if_clean &&
               lhs_.merge_gap == rhs_.merge_gap;
    }

    static void BuildFull(LightType* components_,
                          const TransformType* transforms_,
                          std::uint32_t component_count_,
                          ScratchType& scratch_,
                          LightRuntimeBuildStats& stats_) {
        AdvanceMarkerEpoch(scratch_);
        for (std::uint32_t component_index = 0U; component_index < component_count_; ++component_index) {
            UpdateOpticalFromComponent(components_[component_index],
                                       scratch_.derived_optical[component_index]);
            UpdateGeomFromTransform(component_index,
                                    transforms_,
                                    scratch_.derived_geom[component_index]);
            EncodeGpuRecord(components_[component_index],
                            scratch_.derived_geom[component_index],
                            scratch_.derived_optical[component_index],
                            scratch_.gpu_records[component_index]);
            UpdateRuntimeKeysAndHandle(components_[component_index], component_index, scratch_);
            SystemType::MarkUploaded(components_[component_index]);
            scratch_.updated_component_indices.push_back(component_index);
            ++stats_.updated_record_count;
        }
        stats_.updated_style_or_binding_count = stats_.updated_record_count;
    }

    static void CollectDirtyIndices(LightType* components_,
                                    std::uint32_t component_count_,
                                    const LightRuntimeBuildHint& build_hint_,
                                    ScratchType& scratch_,
                                    LightRuntimeBuildStats& stats_) {
        AdvanceMarkerEpoch(scratch_);
        if (build_hint_.use_dirty_component_indices != 0U &&
            build_hint_.dirty_component_indices != nullptr &&
            build_hint_.dirty_component_count > 0U) {
            for (std::uint32_t i = 0U; i < build_hint_.dirty_component_count; ++i) {
                const std::uint32_t index = build_hint_.dirty_component_indices[i];
                if (index >= component_count_) {
                    ++stats_.out_of_range_dirty_count;
                    continue;
                }
                AppendUniqueIndex(scratch_, index, scratch_.dirty_component_indices);
            }
            stats_.scanned_dirty_count = static_cast<std::uint32_t>(build_hint_.dirty_component_count);
            return;
        }

        for (std::uint32_t component_index = 0U; component_index < component_count_; ++component_index) {
            const std::uint32_t dirty_flags = SystemType::DirtyFlags(components_[component_index]);
            if ((dirty_flags & (light_dirty_style_flag | light_dirty_binding_flag | light_dirty_runtime_flag)) == 0U) {
                continue;
            }
            AppendUniqueIndex(scratch_, component_index, scratch_.dirty_component_indices);
        }
        stats_.scanned_dirty_count = component_count_;
    }

    static void CollectTransformDirtyIndices(std::uint32_t component_count_,
                                             const LightRuntimeBuildHint& build_hint_,
                                             ScratchType& scratch_,
                                             LightRuntimeBuildStats& stats_) {
        if (build_hint_.use_transform_dirty_component_indices == 0U ||
            build_hint_.transform_dirty_component_indices == nullptr ||
            build_hint_.transform_dirty_component_count == 0U) {
            return;
        }

        for (std::uint32_t i = 0U; i < build_hint_.transform_dirty_component_count; ++i) {
            const std::uint32_t index = build_hint_.transform_dirty_component_indices[i];
            if (index >= component_count_) {
                ++stats_.out_of_range_dirty_count;
                continue;
            }
            scratch_.transform_dirty_component_indices.push_back(index);
        }
        if (!scratch_.transform_dirty_component_indices.empty()) {
            std::sort(scratch_.transform_dirty_component_indices.begin(),
                      scratch_.transform_dirty_component_indices.end());
            const auto new_end = std::unique(scratch_.transform_dirty_component_indices.begin(),
                                             scratch_.transform_dirty_component_indices.end());
            scratch_.transform_dirty_component_indices.resize(
                static_cast<std::size_t>(new_end - scratch_.transform_dirty_component_indices.begin()));
        }
        stats_.scanned_transform_dirty_count = build_hint_.transform_dirty_component_count;
    }

    static void AdvanceMarkerEpoch(ScratchType& scratch_) {
        if (scratch_.marker_epoch == 0U || scratch_.marker_epoch == (std::numeric_limits<std::uint32_t>::max)()) {
            for (std::size_t i = 0U; i < scratch_.marker_stamps.size(); ++i) {
                scratch_.marker_stamps[i] = 0U;
            }
            scratch_.marker_epoch = 1U;
            return;
        }
        ++scratch_.marker_epoch;
    }

    static bool HasMarker(const ScratchType& scratch_, std::uint32_t component_index_) noexcept {
        if (component_index_ >= scratch_.marker_stamps.size()) {
            return false;
        }
        return scratch_.marker_stamps[component_index_] == scratch_.marker_epoch;
    }

    static void AppendUniqueIndex(ScratchType& scratch_,
                                  std::uint32_t component_index_,
                                  LightRuntimeMcVector<std::uint32_t>& out_indices_) {
        if (component_index_ >= scratch_.marker_stamps.size()) {
            return;
        }
        if (scratch_.marker_stamps[component_index_] == scratch_.marker_epoch) {
            return;
        }
        scratch_.marker_stamps[component_index_] = scratch_.marker_epoch;
        out_indices_.push_back(component_index_);
    }

    static void UpdateRuntimeKeysAndHandle(LightType& component_,
                                           std::uint32_t component_index_,
                                           ScratchType& scratch_) {
        const std::uint64_t pipeline_key = ComposePipelineKey(component_);
        const std::uint64_t resource_key = ComposeResourceKey(component_);
        const std::uint64_t sort_key = ComposeSortKey(component_, component_index_);
        SystemType::SetRuntimeKeys(component_, pipeline_key, resource_key, sort_key);

        if (component_index_ >= scratch_.handle_generations.size()) {
            return;
        }
        const std::uint32_t generation = scratch_.handle_generations[component_index_];
        SystemType::SetGpuRecordHandle(component_,
                                       LightHandle{
                                           .index = component_index_,
                                           .generation = generation,
                                       });
    }

    [[nodiscard]] static std::uint64_t ComposePipelineKey(const LightType& component_) noexcept {
        std::uint64_t key = 0U;
        key |= (std::same_as<DimensionT, Dim3> ? 1ULL : 0ULL) << 63U;
        key |= (static_cast<std::uint64_t>(component_.style.kind) & 0x7ULL) << 56U;
        key |= (static_cast<std::uint64_t>(component_.style.cast_shadow & 0x1U)) << 55U;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            key |= (static_cast<std::uint64_t>(component_.style.blend_mode) & 0x7ULL) << 48U;
            key |= (static_cast<std::uint64_t>(component_.style.affect_normals_only & 0x1U)) << 47U;
        } else {
            key |= (static_cast<std::uint64_t>(component_.style.falloff_mode) & 0x7ULL) << 48U;
            key |= (static_cast<std::uint64_t>(component_.binding.shadow_config.filter_mode) & 0x7ULL) << 44U;
            key |= (static_cast<std::uint64_t>(component_.binding.shadow_config.cascade_count) & 0x7ULL) << 40U;
        }
        return key;
    }

    [[nodiscard]] static std::uint64_t ComposeResourceKey(const LightType& component_) noexcept {
        std::uint64_t hash = hash_offset_basis;
        HashCombine(hash, static_cast<std::uint64_t>(scene_dimension_v<DimensionT>));
        HashCombine(hash, static_cast<std::uint64_t>(component_.binding.cookie.texture_id));
        HashCombine(hash, static_cast<std::uint64_t>(component_.binding.cookie.sampler_id));
        if constexpr (std::same_as<DimensionT, Dim2>) {
            HashCombine(hash, static_cast<std::uint64_t>(component_.binding.occluder.texture_id));
            HashCombine(hash, static_cast<std::uint64_t>(component_.binding.occluder.sampler_id));
        } else {
            HashCombine(hash, static_cast<std::uint64_t>(component_.binding.ies.texture_id));
            HashCombine(hash, static_cast<std::uint64_t>(component_.binding.shadow.texture_id));
            HashCombine(hash, static_cast<std::uint64_t>(component_.binding.shadow_config.resolution));
            HashCombine(hash, static_cast<std::uint64_t>(component_.binding.shadow_config.cascade_count));
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComposeSortKey(const LightType& component_,
                                                      std::uint32_t component_index_) noexcept {
        std::uint64_t key = 0U;
        const std::uint64_t type_bits = static_cast<std::uint64_t>(component_.style.kind) & 0x3ULL;
        const std::uint64_t shadow_bits = static_cast<std::uint64_t>(component_.style.cast_shadow & 0x1U);
        key |= type_bits << 62U;
        key |= shadow_bits << 61U;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const std::uint32_t layer_shifted = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(component_.style.layer) -
                static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()));
            key |= (static_cast<std::uint64_t>(layer_shifted) & 0xFFFFULL) << 45U;
        } else {
            const std::uint64_t range_bits = static_cast<std::uint64_t>(std::min(component_.style.range, 65535.0F));
            key |= (range_bits & 0xFFFFULL) << 45U;
        }
        key |= static_cast<std::uint64_t>(component_index_) & 0xFFFFFFFFULL;
        return key;
    }

    static float ColorChannelToFloat(std::uint8_t value_) noexcept {
        return static_cast<float>(value_) / 255.0F;
    }

    static void UpdateOpticalFromComponent(const LightType& component_,
                                           LightDerivedOptical& out_optical_) noexcept {
        out_optical_.radius = std::max(0.0F, component_.style.range);
        out_optical_.inv_radius = (out_optical_.radius > 1e-6F) ? (1.0F / out_optical_.radius) : 0.0F;
        out_optical_.cone_cos_inner = std::cos(std::clamp(component_.style.inner_angle_radians, 0.0F, 3.13F));
        out_optical_.cone_cos_outer = std::cos(std::clamp(component_.style.outer_angle_radians,
                                                           component_.style.inner_angle_radians,
                                                           3.13F));
        out_optical_.source_height = component_.style.source_height;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            out_optical_.source_radius = 0.0F;
            out_optical_.source_length = 0.0F;
        } else {
            out_optical_.source_radius = component_.style.source_radius;
            out_optical_.source_length = component_.style.source_length;
        }
        out_optical_.falloff_exponent = std::max(0.0F, component_.style.falloff_exponent);
    }

    static void Normalize2(float& x_, float& y_) noexcept {
        const float len_sq = x_ * x_ + y_ * y_;
        if (len_sq <= 1e-12F) {
            x_ = 0.0F;
            y_ = -1.0F;
            return;
        }
        const float inv_len = 1.0F / std::sqrt(len_sq);
        x_ *= inv_len;
        y_ *= inv_len;
    }

    static void Normalize3(float& x_, float& y_, float& z_) noexcept {
        const float len_sq = x_ * x_ + y_ * y_ + z_ * z_;
        if (len_sq <= 1e-12F) {
            x_ = 0.0F;
            y_ = 0.0F;
            z_ = -1.0F;
            return;
        }
        const float inv_len = 1.0F / std::sqrt(len_sq);
        x_ *= inv_len;
        y_ *= inv_len;
        z_ *= inv_len;
    }

    static void UpdateGeomFromTransform(std::uint32_t component_index_,
                                        const TransformType* transforms_,
                                        GeomType& out_geom_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            out_geom_.position_x = 0.0F;
            out_geom_.position_y = 0.0F;
            out_geom_.direction_x = 0.0F;
            out_geom_.direction_y = -1.0F;
            if (transforms_ == nullptr) {
                return;
            }
            const Affine2x3& world = transforms_[component_index_].runtime.world_matrix;
            out_geom_.position_x = world.m02;
            out_geom_.position_y = world.m12;
            out_geom_.direction_x = world.m00;
            out_geom_.direction_y = world.m10;
            Normalize2(out_geom_.direction_x, out_geom_.direction_y);
        } else {
            out_geom_.position_x = 0.0F;
            out_geom_.position_y = 0.0F;
            out_geom_.position_z = 0.0F;
            out_geom_.direction_x = 0.0F;
            out_geom_.direction_y = 0.0F;
            out_geom_.direction_z = -1.0F;
            if (transforms_ == nullptr) {
                return;
            }
            const Matrix4x4& world = transforms_[component_index_].runtime.world_matrix;
            out_geom_.position_x = world.m[12];
            out_geom_.position_y = world.m[13];
            out_geom_.position_z = world.m[14];
            out_geom_.direction_x = world.m[8];
            out_geom_.direction_y = world.m[9];
            out_geom_.direction_z = world.m[10];
            Normalize3(out_geom_.direction_x, out_geom_.direction_y, out_geom_.direction_z);
        }
    }

    static std::uint32_t ComposeFlags(const LightType& component_) noexcept {
        std::uint32_t flags = 0U;
        flags |= static_cast<std::uint32_t>(component_.style.cast_shadow & 0x1U) << 0U;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            flags |= static_cast<std::uint32_t>(component_.style.affect_normals_only & 0x1U) << 1U;
            flags |= static_cast<std::uint32_t>(component_.style.blend_mode) << 8U;
        } else {
            flags |= static_cast<std::uint32_t>(component_.style.falloff_mode) << 8U;
            flags |= static_cast<std::uint32_t>(component_.binding.shadow_config.filter_mode) << 16U;
        }
        return flags;
    }

    static void EncodeGpuRecord(const LightType& component_,
                                const GeomType& geom_,
                                const LightDerivedOptical& optical_,
                                GpuRecordType& out_record_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            out_record_.position_x = geom_.position_x;
            out_record_.position_y = geom_.position_y;
            out_record_.radius = optical_.radius;
            out_record_.intensity = component_.style.intensity;
            out_record_.color_r = ColorChannelToFloat(component_.style.color.r);
            out_record_.color_g = ColorChannelToFloat(component_.style.color.g);
            out_record_.color_b = ColorChannelToFloat(component_.style.color.b);
            out_record_.falloff_exponent = optical_.falloff_exponent;
            out_record_.direction_x = geom_.direction_x;
            out_record_.direction_y = geom_.direction_y;
            out_record_.cone_cos_outer = optical_.cone_cos_outer;
            out_record_.cone_cos_inner = optical_.cone_cos_inner;
            out_record_.source_height = optical_.source_height;
            out_record_.light_type = static_cast<std::uint32_t>(component_.style.kind);
            out_record_.channel_mask = component_.visibility.light_channel_mask;
            out_record_.flags = ComposeFlags(component_);
            out_record_.shadow_view_begin = (std::numeric_limits<std::uint32_t>::max)();
            out_record_.shadow_meta = 0U;
            out_record_.shadow_namespace_id = 0U;
            out_record_.reserved0 = 0U;
        } else {
            out_record_.position_x = geom_.position_x;
            out_record_.position_y = geom_.position_y;
            out_record_.position_z = geom_.position_z;
            out_record_.radius = optical_.radius;
            out_record_.color_r = ColorChannelToFloat(component_.style.color.r);
            out_record_.color_g = ColorChannelToFloat(component_.style.color.g);
            out_record_.color_b = ColorChannelToFloat(component_.style.color.b);
            out_record_.intensity = component_.style.intensity;
            out_record_.direction_x = geom_.direction_x;
            out_record_.direction_y = geom_.direction_y;
            out_record_.direction_z = geom_.direction_z;
            out_record_.cone_cos_outer = optical_.cone_cos_outer;
            out_record_.cone_cos_inner = optical_.cone_cos_inner;
            out_record_.source_radius = optical_.source_radius;
            out_record_.source_length = optical_.source_length;
            out_record_.source_height = optical_.source_height;
            out_record_.falloff_exponent = optical_.falloff_exponent;
            out_record_.volumetric_strength = component_.style.volumetric_strength;
            out_record_.light_type = static_cast<std::uint32_t>(component_.style.kind);
            out_record_.channel_mask = component_.visibility.light_channel_mask;
            out_record_.flags = ComposeFlags(component_);
            out_record_.shadow_view_begin = (std::numeric_limits<std::uint32_t>::max)();
            out_record_.shadow_meta = 0U;
            out_record_.shadow_namespace_id = 0U;
        }
    }

    static void BuildUploadRanges(const LightRuntimeMcVector<std::uint32_t>& updated_component_indices_,
                                  std::uint32_t merge_gap_,
                                  LightRuntimeMcVector<LightUploadRange>& out_upload_ranges_) {
        out_upload_ranges_.clear();
        if (updated_component_indices_.empty()) {
            return;
        }

        LightRuntimeMcVector<std::uint32_t> sorted_indices = updated_component_indices_;
        std::sort(sorted_indices.begin(), sorted_indices.end());

        std::uint32_t range_begin = sorted_indices[0U];
        std::uint32_t range_end = range_begin;
        for (std::size_t i = 1U; i < sorted_indices.size(); ++i) {
            const std::uint32_t index = sorted_indices[i];
            const std::uint32_t max_contiguous = range_end + 1U + merge_gap_;
            if (index <= max_contiguous) {
                range_end = index;
                continue;
            }

            out_upload_ranges_.push_back(LightUploadRange{
                .begin_index = range_begin,
                .count = range_end - range_begin + 1U,
            });
            range_begin = index;
            range_end = index;
        }

        out_upload_ranges_.push_back(LightUploadRange{
            .begin_index = range_begin,
            .count = range_end - range_begin + 1U,
        });
    }

    static void CommitCache(LightType* components_,
                            const TransformType* transforms_,
                            std::uint32_t component_count_,
                            const LightRuntimeBuildConfig& build_config_,
                            const LightRuntimeBuildStats& stats_,
                            ScratchType& scratch_) {
        scratch_.cache.components = components_;
        scratch_.cache.transforms = transforms_;
        scratch_.cache.component_count = component_count_;
        scratch_.cache.style_signature = stats_.style_signature;
        scratch_.cache.binding_signature = stats_.binding_signature;
        scratch_.cache.transform_signature = stats_.transform_signature;
        scratch_.cache.build_config = build_config_;
        scratch_.cache.epoch = (scratch_.cache.epoch == (std::numeric_limits<std::uint32_t>::max)())
            ? 1U
            : (scratch_.cache.epoch + 1U);
        scratch_.cache.valid = true;
    }
};

} // namespace vr::ecs
