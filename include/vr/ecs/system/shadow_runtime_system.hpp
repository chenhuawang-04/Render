#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/shadow_gpu_layout.hpp"
#include "vr/ecs/system/shadow_system.hpp"
#include "vr/ecs/system/spatial_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace vr::ecs {

template<typename T>
using ShadowRuntimeMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ShadowUploadRange final {
    std::uint32_t begin_index;
    std::uint32_t count;
};

struct ShadowRuntimeBuildConfig final {
    bool force_full_rebuild = false;
    bool rebuild_keys_even_if_clean = false;
    std::uint32_t merge_gap = 0U;

    std::uint16_t atlas_width = 4096U;
    std::uint16_t atlas_height = 4096U;
    std::uint16_t atlas_layer_count = 8U;

    std::uint8_t enable_atlas_packing = 1U;
    std::uint8_t reserve0 = 0U;
};

struct ShadowRuntimeBuildHint final {
    const std::uint32_t* dirty_component_indices = nullptr;
    std::uint32_t dirty_component_count = 0U;
    std::uint8_t use_dirty_component_indices = 0U;

    const std::uint32_t* transform_dirty_component_indices = nullptr;
    std::uint32_t transform_dirty_component_count = 0U;
    std::uint8_t use_transform_dirty_component_indices = 0U;
};

struct ShadowRuntimeBuildStats final {
    std::uint32_t component_count = 0U;
    std::uint32_t scanned_dirty_count = 0U;
    std::uint32_t scanned_transform_dirty_count = 0U;
    std::uint32_t updated_record_count = 0U;
    std::uint32_t updated_style_or_binding_count = 0U;
    std::uint32_t updated_transform_only_count = 0U;
    std::uint32_t generated_view_count = 0U;
    std::uint32_t atlas_allocation_fail_count = 0U;
    std::uint32_t upload_range_count = 0U;
    std::uint32_t view_upload_range_count = 0U;
    std::uint32_t out_of_range_dirty_count = 0U;
    std::uint64_t style_signature = 0U;
    std::uint64_t binding_signature = 0U;
    std::uint64_t transform_signature = 0U;
    std::uint64_t camera_signature = 0U;
    std::uint32_t cache_epoch = 0U;
    bool cache_reused = false;
    bool full_rebuild = false;
    bool transform_only_update = false;
};

struct ShadowDerivedGeom2D final {
    float position_x;
    float position_y;
    float direction_x;
    float direction_y;
};

struct ShadowDerivedGeom3D final {
    float position_x;
    float position_y;
    float position_z;
    float direction_x;
    float direction_y;
    float direction_z;
};

struct ShadowDerivedStyle final {
    float max_distance;
    float depth_bias;
    float normal_bias;
    float slope_scaled_bias;
    float cascade_lambda;
    float near_plane_offset;
    float far_plane_offset;
    std::uint8_t projection_kind;
    std::uint8_t cascade_count;
    std::uint8_t face_count;
    std::uint8_t reverse_z;
};

template<DimensionTag DimensionT>
struct ShadowDerivedGeomTraits;

template<>
struct ShadowDerivedGeomTraits<Dim2> final {
    using GeomType = ShadowDerivedGeom2D;
};

template<>
struct ShadowDerivedGeomTraits<Dim3> final {
    using GeomType = ShadowDerivedGeom3D;
};

template<DimensionTag DimensionT>
using ShadowDerivedGeom = typename ShadowDerivedGeomTraits<DimensionT>::GeomType;

template<DimensionTag DimensionT>
struct ShadowRuntimeCache final {
    const Shadow<DimensionT>* components = nullptr;
    const Transform<DimensionT>* transforms = nullptr;
    const Camera<DimensionT>* camera_component = nullptr;
    std::uint32_t component_count = 0U;
    std::uint64_t style_signature = 0U;
    std::uint64_t binding_signature = 0U;
    std::uint64_t transform_signature = 0U;
    std::uint64_t camera_signature = 0U;
    ShadowRuntimeBuildConfig build_config{};
    std::uint32_t epoch = 0U;
    bool valid = false;
};

template<DimensionTag DimensionT>
struct ShadowRuntimeScratch final {
    using GpuRecordType = ShadowGpuRecord<DimensionT>;
    using GeomType = ShadowDerivedGeom<DimensionT>;

    ShadowRuntimeMcVector<GpuRecordType> gpu_records{};
    ShadowRuntimeMcVector<ShadowViewGpuRecord> view_records{};
    ShadowRuntimeMcVector<GeomType> derived_geom{};
    ShadowRuntimeMcVector<ShadowDerivedStyle> derived_style{};

    ShadowRuntimeMcVector<std::uint32_t> component_view_begin{};
    ShadowRuntimeMcVector<std::uint16_t> component_view_count{};

    ShadowRuntimeMcVector<ShadowUploadRange> upload_ranges{};
    ShadowRuntimeMcVector<ShadowUploadRange> view_upload_ranges{};

    ShadowRuntimeMcVector<std::uint32_t> dirty_component_indices{};
    ShadowRuntimeMcVector<std::uint32_t> transform_dirty_component_indices{};
    ShadowRuntimeMcVector<std::uint32_t> updated_component_indices{};
    ShadowRuntimeMcVector<std::uint32_t> updated_view_indices{};

    ShadowRuntimeMcVector<std::uint32_t> marker_stamps{};
    ShadowRuntimeMcVector<std::uint32_t> handle_generations{};

    std::uint32_t marker_epoch = 1U;
    ShadowRuntimeCache<DimensionT> cache{};
};

static_assert(PurePodShadowComponent<ShadowDerivedGeom2D>);
static_assert(PurePodShadowComponent<ShadowDerivedGeom3D>);
static_assert(PurePodShadowComponent<ShadowDerivedStyle>);

template<DimensionTag DimensionT>
class ShadowRuntimeSystem final {
public:
    using ShadowType = Shadow<DimensionT>;
    using TransformType = Transform<DimensionT>;
    using CameraType = Camera<DimensionT>;
    using SystemType = ShadowSystem<DimensionT>;
    using GpuRecordType = ShadowGpuRecord<DimensionT>;
    using GeomType = ShadowDerivedGeom<DimensionT>;
    using ScratchType = ShadowRuntimeScratch<DimensionT>;

    static void Reserve(ScratchType& scratch_, std::uint32_t component_count_) {
        const std::size_t reserve_count = static_cast<std::size_t>(component_count_);
        if (scratch_.gpu_records.capacity() < reserve_count) {
            scratch_.gpu_records.reserve(reserve_count);
        }
        if (scratch_.derived_geom.capacity() < reserve_count) {
            scratch_.derived_geom.reserve(reserve_count);
        }
        if (scratch_.derived_style.capacity() < reserve_count) {
            scratch_.derived_style.reserve(reserve_count);
        }
        if (scratch_.component_view_begin.capacity() < reserve_count) {
            scratch_.component_view_begin.reserve(reserve_count);
        }
        if (scratch_.component_view_count.capacity() < reserve_count) {
            scratch_.component_view_count.reserve(reserve_count);
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

    [[nodiscard]] static ShadowRuntimeBuildStats Build(
        ShadowType* components_,
        const TransformType* transforms_,
        const CameraType* camera_component_,
        std::uint32_t component_count_,
        ScratchType& scratch_,
        const ShadowRuntimeBuildConfig& build_config_ = {},
        const ShadowRuntimeBuildHint& build_hint_ = {}) {
        ShadowRuntimeBuildStats stats{};
        stats.component_count = component_count_;

        scratch_.upload_ranges.clear();
        scratch_.view_upload_ranges.clear();
        scratch_.dirty_component_indices.clear();
        scratch_.transform_dirty_component_indices.clear();
        scratch_.updated_component_indices.clear();
        scratch_.updated_view_indices.clear();

        if (components_ == nullptr || component_count_ == 0U) {
            scratch_.gpu_records.clear();
            scratch_.view_records.clear();
            scratch_.derived_geom.clear();
            scratch_.derived_style.clear();
            scratch_.component_view_begin.clear();
            scratch_.component_view_count.clear();
            scratch_.cache = {};
            return stats;
        }

        Reserve(scratch_, component_count_);
        EnsureVectorSizes(scratch_, component_count_);

        ComputeSignatures(components_,
                          transforms_,
                          camera_component_,
                          component_count_,
                          stats.style_signature,
                          stats.binding_signature,
                          stats.transform_signature,
                          stats.camera_signature);

        const bool cache_key_matched =
            scratch_.cache.valid &&
            scratch_.cache.components == components_ &&
            scratch_.cache.transforms == transforms_ &&
            scratch_.cache.camera_component == camera_component_ &&
            scratch_.cache.component_count == component_count_ &&
            scratch_.cache.style_signature == stats.style_signature &&
            scratch_.cache.binding_signature == stats.binding_signature &&
            scratch_.cache.transform_signature == stats.transform_signature &&
            scratch_.cache.camera_signature == stats.camera_signature &&
            IsBuildConfigEqual(scratch_.cache.build_config, build_config_);

        if (cache_key_matched) {
            stats.cache_reused = true;
            stats.cache_epoch = scratch_.cache.epoch;
            return stats;
        }

        const bool force_full_rebuild = build_config_.force_full_rebuild ||
                                        !scratch_.cache.valid ||
                                        scratch_.cache.components != components_ ||
                                        scratch_.cache.transforms != transforms_ ||
                                        scratch_.cache.camera_component != camera_component_ ||
                                        scratch_.cache.component_count != component_count_ ||
                                        !IsBuildConfigEqual(scratch_.cache.build_config, build_config_) ||
                                        scratch_.gpu_records.size() != static_cast<std::size_t>(component_count_) ||
                                        scratch_.derived_geom.size() != static_cast<std::size_t>(component_count_) ||
                                        scratch_.derived_style.size() != static_cast<std::size_t>(component_count_) ||
                                        scratch_.component_view_begin.size() != static_cast<std::size_t>(component_count_) ||
                                        scratch_.component_view_count.size() != static_cast<std::size_t>(component_count_);

        if (force_full_rebuild) {
            stats.full_rebuild = true;
            BuildFull(components_, transforms_, camera_component_, component_count_, scratch_, build_config_, stats);
            BuildUploadRanges(scratch_.updated_component_indices, build_config_.merge_gap, scratch_.upload_ranges);
            BuildUploadRanges(scratch_.updated_view_indices, build_config_.merge_gap, scratch_.view_upload_ranges);
            stats.upload_range_count = static_cast<std::uint32_t>(scratch_.upload_ranges.size());
            stats.view_upload_range_count = static_cast<std::uint32_t>(scratch_.view_upload_ranges.size());
            CommitCache(components_, transforms_, camera_component_, component_count_, build_config_, stats, scratch_);
            return stats;
        }

        CollectDirtyIndices(components_, component_count_, build_hint_, scratch_, stats);
        CollectTransformDirtyIndices(component_count_, build_hint_, scratch_, stats);

        const bool has_style_or_binding_dirty = ContainsStyleOrBindingDirty(components_, scratch_.dirty_component_indices);
        if (has_style_or_binding_dirty) {
            stats.full_rebuild = true;
            BuildFull(components_, transforms_, camera_component_, component_count_, scratch_, build_config_, stats);
            BuildUploadRanges(scratch_.updated_component_indices, build_config_.merge_gap, scratch_.upload_ranges);
            BuildUploadRanges(scratch_.updated_view_indices, build_config_.merge_gap, scratch_.view_upload_ranges);
            stats.upload_range_count = static_cast<std::uint32_t>(scratch_.upload_ranges.size());
            stats.view_upload_range_count = static_cast<std::uint32_t>(scratch_.view_upload_ranges.size());
            CommitCache(components_, transforms_, camera_component_, component_count_, build_config_, stats, scratch_);
            return stats;
        }

        bool transform_only_update = false;
        if (!scratch_.transform_dirty_component_indices.empty()) {
            for (const std::uint32_t component_index : scratch_.transform_dirty_component_indices) {
                if (component_index >= component_count_) {
                    ++stats.out_of_range_dirty_count;
                    continue;
                }
                UpdateGeomFromTransform(component_index, transforms_, scratch_.derived_geom[component_index]);
                RebuildComponentViews(component_index,
                                      components_[component_index],
                                      scratch_.derived_geom[component_index],
                                      scratch_.derived_style[component_index],
                                      camera_component_,
                                      scratch_);
                EncodeGpuRecord(component_index, components_[component_index], scratch_, scratch_.gpu_records[component_index]);
                UpdateRuntimeKeysAndHandle(components_[component_index], component_index, scratch_);
                SystemType::MarkUploaded(components_[component_index]);
                scratch_.updated_component_indices.push_back(component_index);
                ++stats.updated_record_count;
                ++stats.updated_transform_only_count;
                transform_only_update = true;
            }
        }

        stats.transform_only_update = transform_only_update && scratch_.dirty_component_indices.empty();

        if (scratch_.updated_component_indices.empty() &&
            build_config_.rebuild_keys_even_if_clean) {
            stats.full_rebuild = true;
            BuildFull(components_, transforms_, camera_component_, component_count_, scratch_, build_config_, stats);
        }

        BuildUploadRanges(scratch_.updated_component_indices, build_config_.merge_gap, scratch_.upload_ranges);
        BuildUploadRanges(scratch_.updated_view_indices, build_config_.merge_gap, scratch_.view_upload_ranges);

        stats.upload_range_count = static_cast<std::uint32_t>(scratch_.upload_ranges.size());
        stats.view_upload_range_count = static_cast<std::uint32_t>(scratch_.view_upload_ranges.size());
        stats.generated_view_count = static_cast<std::uint32_t>(scratch_.view_records.size());
        CommitCache(components_, transforms_, camera_component_, component_count_, build_config_, stats, scratch_);
        return stats;
    }

    [[nodiscard]] static const GpuRecordType* GpuRecords(const ScratchType& scratch_) noexcept {
        return scratch_.gpu_records.data();
    }

    [[nodiscard]] static std::uint32_t GpuRecordCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.gpu_records.size());
    }

    [[nodiscard]] static const ShadowViewGpuRecord* ViewRecords(const ScratchType& scratch_) noexcept {
        return scratch_.view_records.data();
    }

    [[nodiscard]] static std::uint32_t ViewRecordCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.view_records.size());
    }

    [[nodiscard]] static const ShadowUploadRange* UploadRanges(const ScratchType& scratch_) noexcept {
        return scratch_.upload_ranges.data();
    }

    [[nodiscard]] static std::uint32_t UploadRangeCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.upload_ranges.size());
    }

    [[nodiscard]] static const ShadowUploadRange* ViewUploadRanges(const ScratchType& scratch_) noexcept {
        return scratch_.view_upload_ranges.data();
    }

    [[nodiscard]] static std::uint32_t ViewUploadRangeCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.view_upload_ranges.size());
    }

private:
    static constexpr std::uint64_t hash_offset_basis = 14695981039346656037ULL;
    static constexpr std::uint64_t hash_prime = 1099511628211ULL;

    struct AtlasAllocatorState final {
        std::uint16_t cursor_x;
        std::uint16_t cursor_y;
        std::uint16_t row_height;
    };

    struct AtlasRect final {
        std::uint16_t x;
        std::uint16_t y;
        std::uint16_t width;
        std::uint16_t height;
        std::uint16_t layer;
        std::uint8_t valid;
        std::uint8_t reserved0;
    };

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= hash_prime;
    }

    static void HashFloat(std::uint64_t& hash_, float value_) noexcept {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value_, sizeof(bits));
        HashCombine(hash_, static_cast<std::uint64_t>(bits));
    }

    static bool IsBuildConfigEqual(const ShadowRuntimeBuildConfig& lhs_,
                                   const ShadowRuntimeBuildConfig& rhs_) noexcept {
        return lhs_.force_full_rebuild == rhs_.force_full_rebuild &&
               lhs_.rebuild_keys_even_if_clean == rhs_.rebuild_keys_even_if_clean &&
               lhs_.merge_gap == rhs_.merge_gap &&
               lhs_.atlas_width == rhs_.atlas_width &&
               lhs_.atlas_height == rhs_.atlas_height &&
               lhs_.atlas_layer_count == rhs_.atlas_layer_count &&
               lhs_.enable_atlas_packing == rhs_.enable_atlas_packing;
    }

    static void EnsureVectorSizes(ScratchType& scratch_, std::uint32_t component_count_) {
        const std::size_t count = static_cast<std::size_t>(component_count_);
        scratch_.gpu_records.resize(count);
        scratch_.derived_geom.resize(count);
        scratch_.derived_style.resize(count);
        scratch_.component_view_begin.resize(count);
        scratch_.component_view_count.resize(count);

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

    static void ComputeSignatures(ShadowType* components_,
                                  const TransformType* transforms_,
                                  const CameraType* camera_component_,
                                  std::uint32_t component_count_,
                                  std::uint64_t& style_signature_,
                                  std::uint64_t& binding_signature_,
                                  std::uint64_t& transform_signature_,
                                  std::uint64_t& camera_signature_) noexcept {
        std::uint64_t style_hash = hash_offset_basis;
        std::uint64_t binding_hash = hash_offset_basis;
        std::uint64_t transform_hash = hash_offset_basis;

        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            const ShadowType& component = components_[i];
            HashCombine(style_hash, static_cast<std::uint64_t>(component.state.revision_style));
            HashCombine(style_hash, static_cast<std::uint64_t>(component.visibility.visible));
            HashCombine(style_hash, static_cast<std::uint64_t>(component.visibility.enabled));
            HashFloat(style_hash, component.style.max_distance);
            HashFloat(style_hash, component.style.depth_bias);
            HashFloat(style_hash, component.style.normal_bias);
            HashCombine(style_hash, static_cast<std::uint64_t>(component.style.projection_kind));
            if constexpr (std::same_as<DimensionT, Dim2>) {
                HashFloat(style_hash, component.style.softness);
                HashCombine(style_hash, static_cast<std::uint64_t>(static_cast<std::uint16_t>(component.style.layer)));
            } else {
                HashFloat(style_hash, component.style.slope_scaled_bias);
                HashFloat(style_hash, component.style.cascade_lambda);
                HashCombine(style_hash, static_cast<std::uint64_t>(component.style.cascade_count));
                HashCombine(style_hash, static_cast<std::uint64_t>(component.style.face_count));
            }

            HashCombine(binding_hash, static_cast<std::uint64_t>(component.state.revision_binding));
            HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.light_component_index));
            HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.transform_component_index));
            HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.caster_mask));
            HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.receiver_mask));
            HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.atlas_namespace_id));
            HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.atlas_policy));
            if constexpr (std::same_as<DimensionT, Dim3>) {
                HashCombine(binding_hash, static_cast<std::uint64_t>(component.binding.camera_component_index));
            }

            if (transforms_ != nullptr) {
                HashCombine(transform_hash, static_cast<std::uint64_t>(transforms_[i].runtime.world_revision));
            } else {
                HashCombine(transform_hash, static_cast<std::uint64_t>(i + 1U));
            }
        }

        std::uint64_t camera_hash = hash_offset_basis;
        if (camera_component_ != nullptr) {
            HashCombine(camera_hash, static_cast<std::uint64_t>(camera_component_->runtime.revision));
            HashCombine(camera_hash, static_cast<std::uint64_t>(camera_component_->runtime.culling_mask));
            HashFloat(camera_hash, camera_component_->runtime.view_projection_matrix.m[0]);
            HashFloat(camera_hash, camera_component_->runtime.view_projection_matrix.m[5]);
            HashFloat(camera_hash, camera_component_->runtime.view_projection_matrix.m[10]);
            HashFloat(camera_hash, camera_component_->runtime.view_projection_matrix.m[14]);
        } else {
            camera_hash = 0U;
        }

        style_signature_ = style_hash;
        binding_signature_ = binding_hash;
        transform_signature_ = transform_hash;
        camera_signature_ = camera_hash;
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
                                  ShadowRuntimeMcVector<std::uint32_t>& out_indices_) {
        if (component_index_ >= scratch_.marker_stamps.size()) {
            return;
        }
        if (scratch_.marker_stamps[component_index_] == scratch_.marker_epoch) {
            return;
        }
        scratch_.marker_stamps[component_index_] = scratch_.marker_epoch;
        out_indices_.push_back(component_index_);
    }

    static void CollectDirtyIndices(ShadowType* components_,
                                    std::uint32_t component_count_,
                                    const ShadowRuntimeBuildHint& build_hint_,
                                    ScratchType& scratch_,
                                    ShadowRuntimeBuildStats& stats_) {
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
            stats_.scanned_dirty_count = build_hint_.dirty_component_count;
            return;
        }

        for (std::uint32_t component_index = 0U; component_index < component_count_; ++component_index) {
            const std::uint32_t dirty_flags = SystemType::DirtyFlags(components_[component_index]);
            if ((dirty_flags & (shadow_dirty_style_flag | shadow_dirty_binding_flag | shadow_dirty_runtime_flag)) == 0U) {
                continue;
            }
            AppendUniqueIndex(scratch_, component_index, scratch_.dirty_component_indices);
        }
        stats_.scanned_dirty_count = component_count_;
    }

    static void CollectTransformDirtyIndices(std::uint32_t component_count_,
                                             const ShadowRuntimeBuildHint& build_hint_,
                                             ScratchType& scratch_,
                                             ShadowRuntimeBuildStats& stats_) {
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

    static bool ContainsStyleOrBindingDirty(ShadowType* components_,
                                            const ShadowRuntimeMcVector<std::uint32_t>& dirty_indices_) {
        for (const std::uint32_t component_index : dirty_indices_) {
            const std::uint32_t dirty_flags = SystemType::DirtyFlags(components_[component_index]);
            if ((dirty_flags & (shadow_dirty_style_flag | shadow_dirty_binding_flag)) != 0U) {
                return true;
            }
        }
        return false;
    }

    static void UpdateDerivedStyle(const ShadowType& component_,
                                   ShadowDerivedStyle& out_style_) noexcept {
        out_style_.max_distance = std::max(0.0F, component_.style.max_distance);
        out_style_.depth_bias = std::max(0.0F, component_.style.depth_bias);
        out_style_.normal_bias = std::max(0.0F, component_.style.normal_bias);
        out_style_.projection_kind = static_cast<std::uint8_t>(component_.style.projection_kind);
        out_style_.reverse_z = component_.style.reverse_z;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            out_style_.slope_scaled_bias = 0.0F;
            out_style_.cascade_lambda = 0.0F;
            out_style_.near_plane_offset = 0.0F;
            out_style_.far_plane_offset = 0.0F;
            out_style_.cascade_count = 1U;
            out_style_.face_count = 1U;
        } else {
            out_style_.slope_scaled_bias = std::max(0.0F, component_.style.slope_scaled_bias);
            out_style_.cascade_lambda = std::clamp(component_.style.cascade_lambda, 0.0F, 1.0F);
            out_style_.near_plane_offset = component_.style.near_plane_offset;
            out_style_.far_plane_offset = std::max(0.0F, component_.style.far_plane_offset);
            out_style_.cascade_count = std::clamp<std::uint8_t>(component_.style.cascade_count, 1U, 4U);
            out_style_.face_count = std::clamp<std::uint8_t>(component_.style.face_count, 1U, 6U);
        }
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

    static std::uint32_t DetermineViewCount(const ShadowType& component_,
                                            const ShadowDerivedStyle& derived_style_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            return 1U;
        } else {
            switch (static_cast<ShadowProjectionKind>(derived_style_.projection_kind)) {
            case ShadowProjectionKind::directional:
                return static_cast<std::uint32_t>(derived_style_.cascade_count);
            case ShadowProjectionKind::point:
                return static_cast<std::uint32_t>(derived_style_.face_count);
            case ShadowProjectionKind::spot:
            default:
                return 1U;
            }
        }
    }

    static void BuildCascadeSplits(float near_plane_,
                                   float far_plane_,
                                   std::uint32_t cascade_count_,
                                   float lambda_,
                                   float* out_splits_) noexcept {
        if (cascade_count_ == 0U || out_splits_ == nullptr) {
            return;
        }
        const float near_plane = std::max(1e-4F, near_plane_);
        const float far_plane = std::max(near_plane + 1e-3F, far_plane_);
        const float lambda = std::clamp(lambda_, 0.0F, 1.0F);

        for (std::uint32_t i = 0U; i < cascade_count_; ++i) {
            const float split_ratio = static_cast<float>(i + 1U) / static_cast<float>(cascade_count_);
            const float log_split = near_plane * std::pow(far_plane / near_plane, split_ratio);
            const float uniform_split = near_plane + (far_plane - near_plane) * split_ratio;
            out_splits_[i] = log_split * lambda + uniform_split * (1.0F - lambda);
        }
    }

    static Float3 ExtractCameraForward(const CameraType* camera_component_) noexcept {
        if (camera_component_ == nullptr) {
            return Float3{.x = 0.0F, .y = 0.0F, .z = -1.0F};
        }

        const Matrix4x4& view = camera_component_->runtime.view_matrix;
        Float3 forward{
            .x = -view.m[2],
            .y = -view.m[6],
            .z = -view.m[10],
        };
        const Float3 normalized = spatial_math::Normalize3(forward);
        return normalized;
    }

    static Float3 ExtractCameraPosition(const CameraType* camera_component_) noexcept {
        if (camera_component_ == nullptr) {
            return Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        }

        Matrix4x4 inv_view{};
        const bool invert_ok = spatial_math::InvertAffineMatrix4x4(camera_component_->runtime.view_matrix, inv_view);
        if (!invert_ok) {
            return Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        }
        return Float3{
            .x = inv_view.m[12],
            .y = inv_view.m[13],
            .z = inv_view.m[14],
        };
    }

    static Float3 ExtractDirectionFromGeom(const GeomType& geom_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            return Float3{.x = geom_.direction_x, .y = geom_.direction_y, .z = 0.0F};
        } else {
            return Float3{.x = geom_.direction_x, .y = geom_.direction_y, .z = geom_.direction_z};
        }
    }

    static Float3 ExtractPositionFromGeom(const GeomType& geom_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            return Float3{.x = geom_.position_x, .y = geom_.position_y, .z = 0.0F};
        } else {
            return Float3{.x = geom_.position_x, .y = geom_.position_y, .z = geom_.position_z};
        }
    }

    static void FillIdentityViewRecord(ShadowViewGpuRecord& out_record_) noexcept {
        out_record_.view_matrix = spatial_math::IdentityMatrix4x4();
        out_record_.projection_matrix = spatial_math::IdentityMatrix4x4();
        out_record_.view_projection_matrix = spatial_math::IdentityMatrix4x4();
        out_record_.split_near = 0.0F;
        out_record_.split_far = 1.0F;
        out_record_.depth_bias = 0.0F;
        out_record_.normal_bias = 0.0F;
        out_record_.slope_scaled_bias = 0.0F;
        out_record_.texel_world_size = 0.0F;
        out_record_.atlas_namespace_id = 0U;
        out_record_.shadow_component_index = invalid_shadow_index;
        out_record_.atlas_x = 0U;
        out_record_.atlas_y = 0U;
        out_record_.atlas_width = 0U;
        out_record_.atlas_height = 0U;
        out_record_.atlas_layer = 0U;
        out_record_.view_index = 0U;
        out_record_.cascade_index = 0U;
        out_record_.flags = 0U;
    }

    static Matrix4x4 BuildDirectionalViewProjection(const Float3& light_direction_,
                                                    const Float3& center_,
                                                    float radius_,
                                                    float depth_padding_,
                                                    bool reverse_z_,
                                                    Matrix4x4& out_view_,
                                                    Matrix4x4& out_projection_) {
        const Float3 direction = spatial_math::Normalize3(light_direction_, Float3{.x = 0.0F, .y = -1.0F, .z = 0.0F});
        const float radius = std::max(radius_, 1.0F);
        const float depth_padding = std::max(depth_padding_, 1.0F);

        const Float3 eye{
            .x = center_.x - direction.x * (radius + depth_padding),
            .y = center_.y - direction.y * (radius + depth_padding),
            .z = center_.z - direction.z * (radius + depth_padding),
        };
        const Float3 up_hint = (std::abs(direction.y) > 0.95F)
            ? Float3{.x = 0.0F, .y = 0.0F, .z = 1.0F}
            : Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F};

        out_view_ = spatial_math::BuildLookAtViewRh(eye, center_, up_hint);
        const float near_plane = 0.1F;
        const float far_plane = std::max(near_plane + 1e-3F, 2.0F * radius + depth_padding * 2.0F);
        out_projection_ = spatial_math::BuildOrthographicProjection(-radius,
                                                                    radius,
                                                                    -radius,
                                                                    radius,
                                                                    reverse_z_ ? far_plane : near_plane,
                                                                    reverse_z_ ? near_plane : far_plane);
        return spatial_math::MultiplyMatrix4x4(out_projection_, out_view_);
    }

    static Matrix4x4 BuildSpotViewProjection(const Float3& light_position_,
                                             const Float3& light_direction_,
                                             float range_,
                                             bool reverse_z_,
                                             Matrix4x4& out_view_,
                                             Matrix4x4& out_projection_) {
        const Float3 direction = spatial_math::Normalize3(light_direction_, Float3{.x = 0.0F, .y = 0.0F, .z = -1.0F});
        const Float3 target{
            .x = light_position_.x + direction.x,
            .y = light_position_.y + direction.y,
            .z = light_position_.z + direction.z,
        };
        const Float3 up_hint = (std::abs(direction.y) > 0.95F)
            ? Float3{.x = 0.0F, .y = 0.0F, .z = 1.0F}
            : Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F};

        out_view_ = spatial_math::BuildLookAtViewRh(light_position_, target, up_hint);
        const float far_plane = std::max(0.5F, range_);
        const float near_plane = 0.05F;
        out_projection_ = spatial_math::BuildPerspectiveProjection(1.0F,
                                                                    1.0F,
                                                                    reverse_z_ ? far_plane : near_plane,
                                                                    reverse_z_ ? near_plane : far_plane,
                                                                    reverse_z_);
        return spatial_math::MultiplyMatrix4x4(out_projection_, out_view_);
    }

    static Matrix4x4 BuildPointFaceViewProjection(const Float3& light_position_,
                                                  std::uint32_t face_index_,
                                                  float range_,
                                                  bool reverse_z_,
                                                  Matrix4x4& out_view_,
                                                  Matrix4x4& out_projection_) {
        static constexpr std::array<Float3, 6U> face_directions{
            Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F},
            Float3{.x = -1.0F, .y = 0.0F, .z = 0.0F},
            Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            Float3{.x = 0.0F, .y = -1.0F, .z = 0.0F},
            Float3{.x = 0.0F, .y = 0.0F, .z = 1.0F},
            Float3{.x = 0.0F, .y = 0.0F, .z = -1.0F},
        };
        static constexpr std::array<Float3, 6U> up_hints{
            Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            Float3{.x = 0.0F, .y = 0.0F, .z = -1.0F},
            Float3{.x = 0.0F, .y = 0.0F, .z = 1.0F},
            Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
        };

        const std::uint32_t safe_face = std::min<std::uint32_t>(face_index_, 5U);
        const Float3 direction = face_directions[safe_face];
        const Float3 target{
            .x = light_position_.x + direction.x,
            .y = light_position_.y + direction.y,
            .z = light_position_.z + direction.z,
        };

        out_view_ = spatial_math::BuildLookAtViewRh(light_position_, target, up_hints[safe_face]);
        const float far_plane = std::max(0.5F, range_);
        const float near_plane = 0.05F;
        out_projection_ = spatial_math::BuildPerspectiveProjection(1.57079632679F,
                                                                    1.0F,
                                                                    reverse_z_ ? far_plane : near_plane,
                                                                    reverse_z_ ? near_plane : far_plane,
                                                                    reverse_z_);
        return spatial_math::MultiplyMatrix4x4(out_projection_, out_view_);
    }

    static bool AllocateAtlasRect(const ShadowRuntimeBuildConfig& build_config_,
                                  AtlasAllocatorState* allocators_,
                                  std::uint16_t width_,
                                  std::uint16_t height_,
                                  AtlasRect& out_rect_) {
        out_rect_ = {};
        if (build_config_.enable_atlas_packing == 0U) {
            out_rect_.x = 0U;
            out_rect_.y = 0U;
            out_rect_.width = width_;
            out_rect_.height = height_;
            out_rect_.layer = 0U;
            out_rect_.valid = 1U;
            return true;
        }

        const std::uint16_t atlas_width = std::max<std::uint16_t>(build_config_.atlas_width, 1U);
        const std::uint16_t atlas_height = std::max<std::uint16_t>(build_config_.atlas_height, 1U);
        const std::uint16_t atlas_layers = std::max<std::uint16_t>(build_config_.atlas_layer_count, 1U);

        if (width_ == 0U || height_ == 0U || width_ > atlas_width || height_ > atlas_height) {
            return false;
        }

        for (std::uint16_t layer = 0U; layer < atlas_layers; ++layer) {
            AtlasAllocatorState& allocator = allocators_[layer];
            if (allocator.cursor_x + width_ > atlas_width) {
                allocator.cursor_x = 0U;
                allocator.cursor_y = static_cast<std::uint16_t>(allocator.cursor_y + allocator.row_height);
                allocator.row_height = 0U;
            }
            if (allocator.cursor_y + height_ > atlas_height) {
                continue;
            }

            out_rect_.x = allocator.cursor_x;
            out_rect_.y = allocator.cursor_y;
            out_rect_.width = width_;
            out_rect_.height = height_;
            out_rect_.layer = layer;
            out_rect_.valid = 1U;
            out_rect_.reserved0 = 0U;

            allocator.cursor_x = static_cast<std::uint16_t>(allocator.cursor_x + width_);
            allocator.row_height = std::max<std::uint16_t>(allocator.row_height, height_);
            return true;
        }
        return false;
    }

    static void FillCommonViewFields(ShadowViewGpuRecord& out_record_,
                                     std::uint32_t component_index_,
                                     std::uint32_t view_index_,
                                     std::uint32_t cascade_index_,
                                     const ShadowType& component_,
                                     const ShadowDerivedStyle& derived_style_,
                                     const AtlasRect& atlas_rect_) {
        out_record_.depth_bias = derived_style_.depth_bias;
        out_record_.normal_bias = derived_style_.normal_bias;
        out_record_.slope_scaled_bias = derived_style_.slope_scaled_bias;
        out_record_.shadow_component_index = component_index_;
        out_record_.atlas_namespace_id = component_.binding.atlas_namespace_id;

        out_record_.atlas_x = atlas_rect_.x;
        out_record_.atlas_y = atlas_rect_.y;
        out_record_.atlas_width = atlas_rect_.width;
        out_record_.atlas_height = atlas_rect_.height;
        out_record_.atlas_layer = atlas_rect_.layer;

        out_record_.view_index = view_index_;
        out_record_.cascade_index = cascade_index_;
        out_record_.flags = 0U;
        if (component_.style.stabilize != 0U) {
            out_record_.flags |= shadow_view_flag_stabilize;
        }
        if (component_.style.reverse_z != 0U) {
            out_record_.flags |= shadow_view_flag_reverse_z;
        }
        out_record_.flags |= EncodeShadowViewFilterKernelFlags(component_.style.filter_kernel);
    }

    static void Build2DViewRecord(const ShadowType& component_,
                                  std::uint32_t component_index_,
                                  const GeomType& geom_,
                                  const ShadowDerivedStyle& derived_style_,
                                  const AtlasRect& atlas_rect_,
                                  ShadowViewGpuRecord& out_record_) {
        FillIdentityViewRecord(out_record_);
        const float half_extent = std::max(1.0F, derived_style_.max_distance);
        const float near_plane = 0.0F;
        const float far_plane = std::max(near_plane + 1e-3F, component_.style.occluder_height + derived_style_.max_distance + 1.0F);

        const Float3 eye{
            .x = geom_.position_x,
            .y = geom_.position_y,
            .z = component_.style.occluder_height,
        };
        const Float3 target{
            .x = geom_.position_x + geom_.direction_x,
            .y = geom_.position_y + geom_.direction_y,
            .z = 0.0F,
        };
        const Float3 up_hint{.x = 0.0F, .y = 1.0F, .z = 0.0F};

        out_record_.view_matrix = spatial_math::BuildLookAtViewRh(eye, target, up_hint);
        out_record_.projection_matrix = spatial_math::BuildOrthographicProjection(-half_extent,
                                                                                   half_extent,
                                                                                   -half_extent,
                                                                                   half_extent,
                                                                                   near_plane,
                                                                                   far_plane);
        out_record_.view_projection_matrix = spatial_math::MultiplyMatrix4x4(out_record_.projection_matrix,
                                                                              out_record_.view_matrix);
        out_record_.split_near = near_plane;
        out_record_.split_far = far_plane;
        out_record_.texel_world_size = (atlas_rect_.width > 0U)
            ? (2.0F * half_extent / static_cast<float>(atlas_rect_.width))
            : 0.0F;
        FillCommonViewFields(out_record_, component_index_, 0U, 0U, component_, derived_style_, atlas_rect_);
    }

    static void Build3DViewRecordDirectional(const ShadowType& component_,
                                             std::uint32_t component_index_,
                                             const GeomType& geom_,
                                             const ShadowDerivedStyle& derived_style_,
                                             const CameraType* camera_component_,
                                             std::uint32_t cascade_index_,
                                             std::uint32_t cascade_count_,
                                             float split_near_,
                                             float split_far_,
                                             const AtlasRect& atlas_rect_,
                                             ShadowViewGpuRecord& out_record_) {
        FillIdentityViewRecord(out_record_);

        const Float3 camera_pos = ExtractCameraPosition(camera_component_);
        const Float3 camera_forward = ExtractCameraForward(camera_component_);

        const float cascade_center_distance = (split_near_ + split_far_) * 0.5F;
        const Float3 center{
            .x = camera_pos.x + camera_forward.x * cascade_center_distance,
            .y = camera_pos.y + camera_forward.y * cascade_center_distance,
            .z = camera_pos.z + camera_forward.z * cascade_center_distance,
        };

        const float radius = std::max(2.0F, split_far_ - split_near_ + derived_style_.far_plane_offset);
        const float depth_padding = radius + derived_style_.far_plane_offset;

        Matrix4x4 view{};
        Matrix4x4 projection{};
        out_record_.view_projection_matrix = BuildDirectionalViewProjection(
            ExtractDirectionFromGeom(geom_),
            center,
            radius,
            depth_padding,
            derived_style_.reverse_z != 0U,
            view,
            projection);
        out_record_.view_matrix = view;
        out_record_.projection_matrix = projection;
        out_record_.split_near = split_near_;
        out_record_.split_far = split_far_;
        out_record_.texel_world_size = (atlas_rect_.width > 0U)
            ? ((radius * 2.0F) / static_cast<float>(atlas_rect_.width))
            : 0.0F;
        FillCommonViewFields(out_record_,
                             component_index_,
                             cascade_index_,
                             cascade_count_ > 1U ? cascade_index_ : 0U,
                             component_,
                             derived_style_,
                             atlas_rect_);
    }

    static void Build3DViewRecordSpot(const ShadowType& component_,
                                      std::uint32_t component_index_,
                                      const GeomType& geom_,
                                      const ShadowDerivedStyle& derived_style_,
                                      const AtlasRect& atlas_rect_,
                                      ShadowViewGpuRecord& out_record_) {
        FillIdentityViewRecord(out_record_);
        Matrix4x4 view{};
        Matrix4x4 projection{};
        out_record_.view_projection_matrix = BuildSpotViewProjection(
            ExtractPositionFromGeom(geom_),
            ExtractDirectionFromGeom(geom_),
            derived_style_.max_distance + derived_style_.far_plane_offset,
            derived_style_.reverse_z != 0U,
            view,
            projection);
        out_record_.view_matrix = view;
        out_record_.projection_matrix = projection;
        out_record_.split_near = 0.05F;
        out_record_.split_far = std::max(0.5F, derived_style_.max_distance);
        out_record_.texel_world_size = 0.0F;
        FillCommonViewFields(out_record_, component_index_, 0U, 0U, component_, derived_style_, atlas_rect_);
    }

    static void Build3DViewRecordPoint(const ShadowType& component_,
                                       std::uint32_t component_index_,
                                       const GeomType& geom_,
                                       const ShadowDerivedStyle& derived_style_,
                                       std::uint32_t face_index_,
                                       const AtlasRect& atlas_rect_,
                                       ShadowViewGpuRecord& out_record_) {
        FillIdentityViewRecord(out_record_);
        Matrix4x4 view{};
        Matrix4x4 projection{};
        out_record_.view_projection_matrix = BuildPointFaceViewProjection(
            ExtractPositionFromGeom(geom_),
            face_index_,
            derived_style_.max_distance + derived_style_.far_plane_offset,
            derived_style_.reverse_z != 0U,
            view,
            projection);
        out_record_.view_matrix = view;
        out_record_.projection_matrix = projection;
        out_record_.split_near = 0.05F;
        out_record_.split_far = std::max(0.5F, derived_style_.max_distance);
        out_record_.texel_world_size = 0.0F;
        FillCommonViewFields(out_record_, component_index_, face_index_, face_index_, component_, derived_style_, atlas_rect_);
        out_record_.flags |= (1U << 2U);
    }

    static void RebuildComponentViews(std::uint32_t component_index_,
                                      const ShadowType& component_,
                                      const GeomType& geom_,
                                      const ShadowDerivedStyle& derived_style_,
                                      const CameraType* camera_component_,
                                      ScratchType& scratch_) {
        if (component_index_ >= scratch_.component_view_begin.size() ||
            component_index_ >= scratch_.component_view_count.size()) {
            return;
        }

        const std::uint32_t view_begin = scratch_.component_view_begin[component_index_];
        const std::uint32_t view_count = scratch_.component_view_count[component_index_];
        if (view_count == 0U) {
            return;
        }

        const bool can_update_in_place = view_begin < scratch_.view_records.size() &&
                                         view_begin + view_count <= scratch_.view_records.size();
        if (!can_update_in_place) {
            return;
        }

        std::array<float, max_shadow_cascade_count> cascade_splits{};
        if constexpr (std::same_as<DimensionT, Dim3>) {
            if (static_cast<ShadowProjectionKind>(derived_style_.projection_kind) == ShadowProjectionKind::directional &&
                camera_component_ != nullptr &&
                view_count > 0U) {
                const float camera_near = std::max(1e-3F, camera_component_->style.near_plane + derived_style_.near_plane_offset);
                const float camera_far = std::max(camera_near + 1e-3F,
                                                  std::min(camera_component_->style.far_plane,
                                                           derived_style_.max_distance + derived_style_.far_plane_offset));
                BuildCascadeSplits(camera_near,
                                   camera_far,
                                   view_count,
                                   derived_style_.cascade_lambda,
                                   cascade_splits.data());
            }
        }

        for (std::uint32_t i = 0U; i < view_count; ++i) {
            ShadowViewGpuRecord& view_record = scratch_.view_records[view_begin + i];
            const AtlasRect atlas_rect{
                .x = static_cast<std::uint16_t>(std::min<std::uint32_t>(view_record.atlas_x, (std::numeric_limits<std::uint16_t>::max)())),
                .y = static_cast<std::uint16_t>(std::min<std::uint32_t>(view_record.atlas_y, (std::numeric_limits<std::uint16_t>::max)())),
                .width = static_cast<std::uint16_t>(std::min<std::uint32_t>(view_record.atlas_width, (std::numeric_limits<std::uint16_t>::max)())),
                .height = static_cast<std::uint16_t>(std::min<std::uint32_t>(view_record.atlas_height, (std::numeric_limits<std::uint16_t>::max)())),
                .layer = static_cast<std::uint16_t>(std::min<std::uint32_t>(view_record.atlas_layer, (std::numeric_limits<std::uint16_t>::max)())),
                .valid = 1U,
                .reserved0 = 0U,
            };

            if constexpr (std::same_as<DimensionT, Dim2>) {
                Build2DViewRecord(component_, component_index_, geom_, derived_style_, atlas_rect, view_record);
            } else {
                const ShadowProjectionKind kind = static_cast<ShadowProjectionKind>(derived_style_.projection_kind);
                if (kind == ShadowProjectionKind::directional) {
                    const float split_near = (i == 0U)
                        ? std::max(1e-3F, camera_component_ != nullptr ? camera_component_->style.near_plane : 0.05F)
                        : cascade_splits[i - 1U];
                    const float split_far = cascade_splits[i];
                    Build3DViewRecordDirectional(component_,
                                                 component_index_,
                                                 geom_,
                                                 derived_style_,
                                                 camera_component_,
                                                 i,
                                                 view_count,
                                                 split_near,
                                                 split_far,
                                                 atlas_rect,
                                                 view_record);
                } else if (kind == ShadowProjectionKind::point) {
                    Build3DViewRecordPoint(component_,
                                           component_index_,
                                           geom_,
                                           derived_style_,
                                           i,
                                           atlas_rect,
                                           view_record);
                } else {
                    Build3DViewRecordSpot(component_,
                                          component_index_,
                                          geom_,
                                          derived_style_,
                                          atlas_rect,
                                          view_record);
                }
            }
            scratch_.updated_view_indices.push_back(view_begin + i);
        }
    }

    static std::uint64_t ComposePipelineKey(const ShadowType& component_) noexcept {
        std::uint64_t key = 0U;
        key |= (std::same_as<DimensionT, Dim3> ? 1ULL : 0ULL) << 63U;
        key |= (static_cast<std::uint64_t>(component_.style.projection_kind) & 0x7ULL) << 56U;
        key |= (static_cast<std::uint64_t>(component_.style.filter_kernel) & 0x7ULL) << 52U;
        key |= (static_cast<std::uint64_t>(component_.style.fit_mode) & 0x3ULL) << 50U;
        key |= (static_cast<std::uint64_t>(component_.style.stabilize & 0x1U)) << 49U;
        key |= (static_cast<std::uint64_t>(component_.style.reverse_z & 0x1U)) << 48U;
        if constexpr (std::same_as<DimensionT, Dim3>) {
            key |= (static_cast<std::uint64_t>(component_.style.cascade_count) & 0x7ULL) << 40U;
            key |= (static_cast<std::uint64_t>(component_.style.face_count) & 0x7ULL) << 36U;
        }
        return key;
    }

    static std::uint64_t ComposeResourceKey(const ShadowType& component_) noexcept {
        std::uint64_t hash = hash_offset_basis;
        HashCombine(hash, static_cast<std::uint64_t>(scene_dimension_v<DimensionT>));
        HashCombine(hash, static_cast<std::uint64_t>(component_.binding.light_component_index));
        HashCombine(hash, static_cast<std::uint64_t>(component_.binding.transform_component_index));
        HashCombine(hash, static_cast<std::uint64_t>(component_.binding.caster_mask));
        HashCombine(hash, static_cast<std::uint64_t>(component_.binding.receiver_mask));
        HashCombine(hash, static_cast<std::uint64_t>(component_.binding.atlas_namespace_id));
        HashCombine(hash, static_cast<std::uint64_t>(component_.binding.atlas_policy));
        if constexpr (std::same_as<DimensionT, Dim3>) {
            HashCombine(hash, static_cast<std::uint64_t>(component_.binding.camera_component_index));
        }
        return hash;
    }

    static std::uint64_t ComposeSortKey(const ShadowType& component_,
                                        std::uint32_t component_index_) noexcept {
        std::uint64_t key = 0U;
        key |= (static_cast<std::uint64_t>(component_.style.projection_kind) & 0x3ULL) << 62U;
        key |= (static_cast<std::uint64_t>(component_.style.filter_kernel) & 0x3ULL) << 60U;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const std::uint32_t layer_shifted = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(component_.style.layer) -
                static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()));
            key |= (static_cast<std::uint64_t>(layer_shifted) & 0xFFFFULL) << 44U;
        } else {
            key |= (static_cast<std::uint64_t>(component_.style.cascade_count) & 0x7ULL) << 44U;
            key |= (static_cast<std::uint64_t>(component_.style.face_count) & 0x7ULL) << 40U;
        }
        key |= static_cast<std::uint64_t>(component_index_) & 0xFFFFFFFFULL;
        return key;
    }

    static std::uint32_t ComposeRecordFlags(const ShadowType& component_) noexcept {
        std::uint32_t flags = 0U;
        flags |= static_cast<std::uint32_t>(component_.style.stabilize & 0x1U) << 0U;
        flags |= static_cast<std::uint32_t>(component_.style.reverse_z & 0x1U) << 1U;
        flags |= static_cast<std::uint32_t>(component_.visibility.visible & 0x1U) << 2U;
        flags |= static_cast<std::uint32_t>(component_.visibility.enabled & 0x1U) << 3U;
        return flags;
    }

    static void EncodeGpuRecord(std::uint32_t component_index_,
                                const ShadowType& component_,
                                const ScratchType& scratch_,
                                GpuRecordType& out_record_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            out_record_.max_distance = component_.style.max_distance;
            out_record_.softness = component_.style.softness;
            out_record_.occluder_height = component_.style.occluder_height;
            out_record_.blur_sigma = component_.style.blur_sigma;

            out_record_.first_view_index = scratch_.component_view_begin[component_index_];
            out_record_.view_count = scratch_.component_view_count[component_index_];
            out_record_.caster_mask = component_.binding.caster_mask;
            out_record_.receiver_mask = component_.binding.receiver_mask;

            out_record_.projection_kind = static_cast<std::uint32_t>(component_.style.projection_kind);
            out_record_.filter_kernel = static_cast<std::uint32_t>(component_.style.filter_kernel);
            out_record_.fit_mode = static_cast<std::uint32_t>(component_.style.fit_mode);
            out_record_.flags = ComposeRecordFlags(component_);

            out_record_.atlas_namespace_id = component_.binding.atlas_namespace_id;
            out_record_.atlas_policy = static_cast<std::uint32_t>(component_.binding.atlas_policy);
            out_record_.layer = component_.style.layer;
            out_record_.reserved0 = 0U;
        } else {
            out_record_.max_distance = component_.style.max_distance;
            out_record_.cascade_lambda = component_.style.cascade_lambda;
            out_record_.near_plane_offset = component_.style.near_plane_offset;
            out_record_.far_plane_offset = component_.style.far_plane_offset;

            out_record_.first_view_index = scratch_.component_view_begin[component_index_];
            out_record_.view_count = scratch_.component_view_count[component_index_];
            out_record_.caster_mask = component_.binding.caster_mask;
            out_record_.receiver_mask = component_.binding.receiver_mask;

            out_record_.projection_kind = static_cast<std::uint32_t>(component_.style.projection_kind);
            out_record_.filter_kernel = static_cast<std::uint32_t>(component_.style.filter_kernel);
            out_record_.fit_mode = static_cast<std::uint32_t>(component_.style.fit_mode);
            out_record_.flags = ComposeRecordFlags(component_);

            out_record_.atlas_namespace_id = component_.binding.atlas_namespace_id;
            out_record_.atlas_policy = static_cast<std::uint32_t>(component_.binding.atlas_policy);
            out_record_.reserved0 = 0U;
            out_record_.reserved1 = 0U;
        }
    }

    static void UpdateRuntimeKeysAndHandle(ShadowType& component_,
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
                                       ShadowHandle{
                                           .index = component_index_,
                                           .generation = generation,
                                       });
    }

    static void BuildFull(ShadowType* components_,
                          const TransformType* transforms_,
                          const CameraType* camera_component_,
                          std::uint32_t component_count_,
                          ScratchType& scratch_,
                          const ShadowRuntimeBuildConfig& build_config_,
                          ShadowRuntimeBuildStats& stats_) {
        scratch_.view_records.clear();
        std::fill(scratch_.component_view_begin.begin(), scratch_.component_view_begin.end(), 0U);
        std::fill(scratch_.component_view_count.begin(), scratch_.component_view_count.end(), 0U);

        const std::uint16_t atlas_layers = std::max<std::uint16_t>(build_config_.atlas_layer_count, 1U);
        std::array<AtlasAllocatorState, 64U> allocators{};
        const std::uint16_t capped_layers = std::min<std::uint16_t>(atlas_layers, static_cast<std::uint16_t>(allocators.size()));

        for (std::uint32_t component_index = 0U; component_index < component_count_; ++component_index) {
            ShadowType& component = components_[component_index];

            UpdateDerivedStyle(component, scratch_.derived_style[component_index]);
            UpdateGeomFromTransform(component_index, transforms_, scratch_.derived_geom[component_index]);

            const bool enabled = SystemType::IsEnabledForBuild(component);
            const std::uint32_t view_count = enabled
                ? DetermineViewCount(component, scratch_.derived_style[component_index])
                : 0U;
            const std::uint32_t view_begin = static_cast<std::uint32_t>(scratch_.view_records.size());

            scratch_.component_view_begin[component_index] = view_begin;
            scratch_.component_view_count[component_index] = static_cast<std::uint16_t>(std::min<std::uint32_t>(view_count, 0xFFFFU));

            std::array<float, max_shadow_cascade_count> cascade_splits{};
            if constexpr (std::same_as<DimensionT, Dim3>) {
                if (enabled &&
                    static_cast<ShadowProjectionKind>(scratch_.derived_style[component_index].projection_kind) == ShadowProjectionKind::directional &&
                    camera_component_ != nullptr &&
                    view_count > 0U) {
                    const float camera_near = std::max(1e-3F, camera_component_->style.near_plane + scratch_.derived_style[component_index].near_plane_offset);
                    const float camera_far = std::max(camera_near + 1e-3F,
                                                      std::min(camera_component_->style.far_plane,
                                                               scratch_.derived_style[component_index].max_distance +
                                                                   scratch_.derived_style[component_index].far_plane_offset));
                    BuildCascadeSplits(camera_near,
                                       camera_far,
                                       view_count,
                                       scratch_.derived_style[component_index].cascade_lambda,
                                       cascade_splits.data());
                }
            }

            AtlasRect first_rect{};
            bool first_rect_assigned = false;

            for (std::uint32_t local_view_index = 0U; local_view_index < view_count; ++local_view_index) {
                AtlasRect atlas_rect{};
                const bool atlas_ok = AllocateAtlasRect(build_config_,
                                                        allocators.data(),
                                                        component.style.map_width,
                                                        component.style.map_height,
                                                        atlas_rect);
                if (!atlas_ok || atlas_rect.layer >= capped_layers) {
                    ++stats_.atlas_allocation_fail_count;
                    atlas_rect = {};
                }

                ShadowViewGpuRecord view_record{};
                if constexpr (std::same_as<DimensionT, Dim2>) {
                    Build2DViewRecord(component,
                                      component_index,
                                      scratch_.derived_geom[component_index],
                                      scratch_.derived_style[component_index],
                                      atlas_rect,
                                      view_record);
                } else {
                    const ShadowProjectionKind kind = static_cast<ShadowProjectionKind>(scratch_.derived_style[component_index].projection_kind);
                    if (kind == ShadowProjectionKind::directional) {
                        const float split_near = (local_view_index == 0U)
                            ? std::max(1e-3F, camera_component_ != nullptr ? camera_component_->style.near_plane : 0.05F)
                            : cascade_splits[local_view_index - 1U];
                        const float split_far = cascade_splits[local_view_index];
                        Build3DViewRecordDirectional(component,
                                                     component_index,
                                                     scratch_.derived_geom[component_index],
                                                     scratch_.derived_style[component_index],
                                                     camera_component_,
                                                     local_view_index,
                                                     view_count,
                                                     split_near,
                                                     split_far,
                                                     atlas_rect,
                                                     view_record);
                    } else if (kind == ShadowProjectionKind::point) {
                        Build3DViewRecordPoint(component,
                                               component_index,
                                               scratch_.derived_geom[component_index],
                                               scratch_.derived_style[component_index],
                                               local_view_index,
                                               atlas_rect,
                                               view_record);
                    } else {
                        Build3DViewRecordSpot(component,
                                              component_index,
                                              scratch_.derived_geom[component_index],
                                              scratch_.derived_style[component_index],
                                              atlas_rect,
                                              view_record);
                    }
                }

                scratch_.view_records.push_back(view_record);
                const std::uint32_t global_view_index = static_cast<std::uint32_t>(scratch_.view_records.size() - 1U);
                scratch_.updated_view_indices.push_back(global_view_index);
                if (!first_rect_assigned) {
                    first_rect = atlas_rect;
                    first_rect_assigned = true;
                }
            }

            if (!first_rect_assigned) {
                first_rect = {};
            }

            SystemType::SetAtlasRuntime(component,
                                        component.binding.atlas_namespace_id,
                                        first_rect.x,
                                        first_rect.y,
                                        first_rect.width,
                                        first_rect.height,
                                        first_rect.layer,
                                        static_cast<std::uint8_t>(view_count));

            component.visibility.caster_mask = component.binding.caster_mask;
            component.visibility.receiver_mask = component.binding.receiver_mask;

            EncodeGpuRecord(component_index,
                            component,
                            scratch_,
                            scratch_.gpu_records[component_index]);
            UpdateRuntimeKeysAndHandle(component, component_index, scratch_);
            SystemType::MarkUploaded(component);
            scratch_.updated_component_indices.push_back(component_index);
            ++stats_.updated_record_count;
        }

        stats_.updated_style_or_binding_count = stats_.updated_record_count;
        stats_.generated_view_count = static_cast<std::uint32_t>(scratch_.view_records.size());
    }

    static void BuildUploadRanges(const ShadowRuntimeMcVector<std::uint32_t>& updated_indices_,
                                  std::uint32_t merge_gap_,
                                  ShadowRuntimeMcVector<ShadowUploadRange>& out_upload_ranges_) {
        out_upload_ranges_.clear();
        if (updated_indices_.empty()) {
            return;
        }

        ShadowRuntimeMcVector<std::uint32_t> sorted_indices = updated_indices_;
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

            out_upload_ranges_.push_back(ShadowUploadRange{
                .begin_index = range_begin,
                .count = range_end - range_begin + 1U,
            });
            range_begin = index;
            range_end = index;
        }

        out_upload_ranges_.push_back(ShadowUploadRange{
            .begin_index = range_begin,
            .count = range_end - range_begin + 1U,
        });
    }

    static void CommitCache(ShadowType* components_,
                            const TransformType* transforms_,
                            const CameraType* camera_component_,
                            std::uint32_t component_count_,
                            const ShadowRuntimeBuildConfig& build_config_,
                            const ShadowRuntimeBuildStats& stats_,
                            ScratchType& scratch_) {
        scratch_.cache.components = components_;
        scratch_.cache.transforms = transforms_;
        scratch_.cache.camera_component = camera_component_;
        scratch_.cache.component_count = component_count_;
        scratch_.cache.style_signature = stats_.style_signature;
        scratch_.cache.binding_signature = stats_.binding_signature;
        scratch_.cache.transform_signature = stats_.transform_signature;
        scratch_.cache.camera_signature = stats_.camera_signature;
        scratch_.cache.build_config = build_config_;
        scratch_.cache.epoch = (scratch_.cache.epoch == (std::numeric_limits<std::uint32_t>::max)())
            ? 1U
            : (scratch_.cache.epoch + 1U);
        scratch_.cache.valid = true;
    }
};

} // namespace vr::ecs
