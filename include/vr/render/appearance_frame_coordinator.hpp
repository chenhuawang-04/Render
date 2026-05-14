#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/appearance_prepare_stage.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace vr::render {

template<typename T>
using AppearanceFrameCoordinatorMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

template<ecs::DimensionTag DimensionT>
struct AppearanceFrameCoordinatorStats final {
    std::uint32_t prepare_call_count = 0U;
    std::uint32_t runtime_build_call_count = 0U;
    std::uint32_t frame_reuse_hit_count = 0U;
    std::uint32_t geometry_link_call_count = 0U;
    std::uint32_t surface_link_call_count = 0U;
    std::uint64_t total_link_scanned_count = 0U;
    std::uint64_t total_link_updated_count = 0U;
    std::uint64_t dirty_hint_input_count = 0U;
    std::uint64_t dirty_hint_unique_count = 0U;
    std::uint64_t dirty_hint_duplicate_drop_count = 0U;
    std::uint64_t dirty_hint_out_of_range_drop_count = 0U;
    std::uint64_t dirty_payload_duplicate_skip_count = 0U;
    std::uint64_t geometry_link_candidate_scan_count = 0U;
    std::uint64_t geometry_link_incremental_call_count = 0U;
    std::uint64_t surface_link_candidate_scan_count = 0U;
    std::uint64_t surface_link_incremental_call_count = 0U;
};

template<ecs::DimensionTag DimensionT>
class AppearanceFrameCoordinator final {
public:
    using AppearanceType = ecs::Appearance<DimensionT>;
    using GeometryType = ecs::Geometry<DimensionT>;
    using SurfaceType = ecs::Surface<DimensionT>;
    using GeometrySystemType = ecs::GeometrySystem<DimensionT>;
    using SurfaceSystemType = ecs::SurfaceSystem<DimensionT>;

    using PrepareStageType = AppearancePrepareStage<DimensionT>;
    using PrepareStageResultType = AppearancePrepareStageResult<DimensionT>;
    using RuntimeScratchType = ecs::AppearanceRuntimeScratch<DimensionT>;

    static constexpr std::uint32_t invalid_frame_index = (std::numeric_limits<std::uint32_t>::max)();
    static constexpr std::uint32_t invalid_component_index = (std::numeric_limits<std::uint32_t>::max)();

private:
    template<typename ComponentT>
    struct LinkCache final {
        void Reset() noexcept {
            components = nullptr;
            component_count = 0U;
            initialized = false;
            cached_handles.clear();
            candidate_component_indices.clear();
            appearance_head_indices.clear();
            component_next_indices.clear();
            component_dedup_stamps.clear();
            component_dedup_epoch = 1U;
            appearance_handle_mutation_serial_seen = 0U;
        }

        void Reserve(std::uint32_t component_count_) {
            if (component_count_ == 0U) {
                return;
            }
            if (component_count_ > cached_handles.capacity()) {
                cached_handles.reserve(component_count_);
            }
            if (component_count_ > candidate_component_indices.capacity()) {
                candidate_component_indices.reserve(component_count_);
            }
            if (component_count_ > component_next_indices.capacity()) {
                component_next_indices.reserve(component_count_);
            }
            if (component_count_ > component_dedup_stamps.size()) {
                component_dedup_stamps.resize(component_count_, 0U);
            }
        }

        [[nodiscard]] std::uint32_t NextComponentDedupEpoch() {
            if (component_dedup_epoch == 0U ||
                component_dedup_epoch == (std::numeric_limits<std::uint32_t>::max)()) {
                for (std::size_t i = 0U; i < component_dedup_stamps.size(); ++i) {
                    component_dedup_stamps[i] = 0U;
                }
                component_dedup_epoch = 1U;
            }
            const std::uint32_t epoch = component_dedup_epoch;
            ++component_dedup_epoch;
            return epoch;
        }

        void EnsureComponentDedupCapacity(std::uint32_t component_count_) {
            if (component_count_ > component_dedup_stamps.size()) {
                component_dedup_stamps.resize(component_count_, 0U);
            }
        }

        ComponentT* components = nullptr;
        std::uint32_t component_count = 0U;
        bool initialized = false;
        AppearanceFrameCoordinatorMcVector<ecs::AppearanceHandle> cached_handles{};
        AppearanceFrameCoordinatorMcVector<std::uint32_t> candidate_component_indices{};
        AppearanceFrameCoordinatorMcVector<std::uint32_t> appearance_head_indices{};
        AppearanceFrameCoordinatorMcVector<std::uint32_t> component_next_indices{};
        AppearanceFrameCoordinatorMcVector<std::uint32_t> component_dedup_stamps{};
        std::uint32_t component_dedup_epoch = 1U;
        std::uint64_t appearance_handle_mutation_serial_seen = 0U;
    };

public:
    AppearanceFrameCoordinator() = default;
    ~AppearanceFrameCoordinator() = default;

    AppearanceFrameCoordinator(const AppearanceFrameCoordinator&) = delete;
    AppearanceFrameCoordinator& operator=(const AppearanceFrameCoordinator&) = delete;
    AppearanceFrameCoordinator(AppearanceFrameCoordinator&&) = delete;
    AppearanceFrameCoordinator& operator=(AppearanceFrameCoordinator&&) = delete;

    void SetAppearanceData(AppearanceType* appearance_components_,
                           std::uint32_t appearance_component_count_) noexcept {
        if (appearance_components == appearance_components_ &&
            appearance_component_count == appearance_component_count_) {
            return;
        }
        appearance_components = appearance_components_;
        appearance_component_count = appearance_component_count_;
        accumulated_dirty_component_indices.clear();
        last_applied_dirty_component_indices.clear();
        ++source_revision;
        ResetFrameCache();
    }

    void SetDirtyHint(const std::uint32_t* dirty_component_indices_,
                      std::uint32_t dirty_component_count_) noexcept {
        if (dirty_component_indices_ == nullptr || dirty_component_count_ == 0U) {
            return;
        }
        stats.dirty_hint_input_count += static_cast<std::uint64_t>(dirty_component_count_);

        const bool is_duplicate_dirty_payload =
            frame_cache_valid &&
            accumulated_dirty_component_indices.empty() &&
            IsSameAsLastAppliedDirtyPayload(dirty_component_indices_, dirty_component_count_);
        if (is_duplicate_dirty_payload) {
            ++stats.dirty_payload_duplicate_skip_count;
            return;
        }

        const std::size_t previous_size = accumulated_dirty_component_indices.size();
        accumulated_dirty_component_indices.resize(previous_size + dirty_component_count_);
        for (std::uint32_t i = 0U; i < dirty_component_count_; ++i) {
            accumulated_dirty_component_indices[previous_size + i] = dirty_component_indices_[i];
        }
        ++dirty_revision;
        if (frame_cache_valid) {
            frame_cache_valid = false;
        }
    }

    void Reserve(std::uint32_t appearance_component_count_) {
        PrepareStageType::Reserve(runtime_scratch, appearance_component_count_);
        if (appearance_component_count_ > 0U &&
            appearance_component_count_ > accumulated_dirty_component_indices.capacity()) {
            accumulated_dirty_component_indices.reserve(appearance_component_count_);
        }
        if (appearance_component_count_ > 0U &&
            appearance_component_count_ > normalized_dirty_component_indices.capacity()) {
            normalized_dirty_component_indices.reserve(appearance_component_count_);
        }
        if (appearance_component_count_ > 0U &&
            appearance_component_count_ > dirty_marker_stamps.size()) {
            dirty_marker_stamps.resize(appearance_component_count_, 0U);
        }
        if (appearance_component_count_ > 0U &&
            appearance_component_count_ > appearance_dirty_marker_stamps.size()) {
            appearance_dirty_marker_stamps.resize(appearance_component_count_, 0U);
        }
    }

    void ResetFrameCache() noexcept {
        frame_cache_valid = false;
        frame_cache_index = invalid_frame_index;
        frame_cache_components = nullptr;
        frame_cache_component_count = 0U;
        frame_cache_source_revision = 0U;
        frame_cache_dirty_revision = 0U;
        frame_prepare_result = {};
    }

    void ResetAll() noexcept {
        appearance_components = nullptr;
        appearance_component_count = 0U;
        accumulated_dirty_component_indices.clear();
        normalized_dirty_component_indices.clear();
        dirty_marker_stamps.clear();
        dirty_marker_epoch = 1U;
        last_applied_dirty_component_indices.clear();
        appearance_dirty_marker_stamps.clear();
        appearance_dirty_marker_epoch = 1U;
        runtime_scratch = {};
        stats = {};
        source_revision = 0U;
        dirty_revision = 0U;
        geometry_link_cache.Reset();
        surface_link_cache.Reset();
        ResetFrameCache();
    }

    [[nodiscard]] PrepareStageResultType PrepareFrame(std::uint32_t frame_index_) {
        ++stats.prepare_call_count;
        if (appearance_components == nullptr || appearance_component_count == 0U) {
            ResetFrameCache();
            return PrepareStageResultType{};
        }

        const bool can_reuse_frame_cache =
            frame_cache_valid &&
            frame_cache_index == frame_index_ &&
            frame_cache_components == appearance_components &&
            frame_cache_component_count == appearance_component_count &&
            frame_cache_source_revision == source_revision &&
            frame_cache_dirty_revision == dirty_revision;
        if (can_reuse_frame_cache) {
            ++stats.frame_reuse_hit_count;
            frame_prepare_result.build_invoked = false;
            frame_prepare_result.runtime_stats.full_rebuild = 0U;
            PrepareStageResultType reused_result = frame_prepare_result;
            return reused_result;
        }

        const bool can_reuse_cross_frame_cache =
            frame_cache_valid &&
            frame_cache_components == appearance_components &&
            frame_cache_component_count == appearance_component_count &&
            frame_cache_source_revision == source_revision &&
            frame_cache_dirty_revision == dirty_revision &&
            accumulated_dirty_component_indices.empty();
        if (can_reuse_cross_frame_cache) {
            ++stats.frame_reuse_hit_count;
            frame_cache_index = frame_index_;
            frame_prepare_result.build_invoked = false;
            frame_prepare_result.runtime_stats.full_rebuild = 0U;
            PrepareStageResultType reused_result = frame_prepare_result;
            return reused_result;
        }

        const std::uint32_t* dirty_indices = nullptr;
        std::uint32_t dirty_count = 0U;
        NormalizeDirtyHintPayload(dirty_indices, dirty_count);

        PrepareStageResultType result = PrepareStageType::BuildRuntimeOnly(
            appearance_components,
            appearance_component_count,
            dirty_indices,
            dirty_count,
            runtime_scratch);
        if (result.build_invoked) {
            ++stats.runtime_build_call_count;
        }

        if (dirty_count > 0U && dirty_indices != nullptr) {
            last_applied_dirty_component_indices.resize(dirty_count);
            for (std::uint32_t i = 0U; i < dirty_count; ++i) {
                last_applied_dirty_component_indices[i] = dirty_indices[i];
            }
        } else {
            last_applied_dirty_component_indices.clear();
        }
        accumulated_dirty_component_indices.clear();
        frame_prepare_result = result;
        frame_cache_valid = result.has_appearance_data;
        frame_cache_index = frame_index_;
        frame_cache_components = appearance_components;
        frame_cache_component_count = appearance_component_count;
        frame_cache_source_revision = source_revision;
        frame_cache_dirty_revision = dirty_revision;
        return result;
    }

    [[nodiscard]] ecs::AppearanceLinkStats LinkGeometry(GeometryType* geometry_components_,
                                                        std::uint32_t geometry_component_count_,
                                                        std::uint32_t frame_index_) {
        EnsurePrepared(frame_index_);
        ++stats.geometry_link_call_count;
        if (!frame_cache_valid || frame_cache_components == nullptr || frame_cache_component_count == 0U) {
            return ecs::AppearanceLinkStats{};
        }
        if (geometry_components_ == nullptr || geometry_component_count_ == 0U) {
            return ecs::AppearanceLinkStats{};
        }

        ecs::AppearanceLinkStats link_stats = LinkGeometryIncremental(geometry_components_,
                                                                       geometry_component_count_,
                                                                       frame_cache_components,
                                                                       frame_cache_component_count);
        AccumulateLinkStats(link_stats);
        return link_stats;
    }

    [[nodiscard]] ecs::AppearanceLinkStats LinkSurface(SurfaceType* surface_components_,
                                                       std::uint32_t surface_component_count_,
                                                       std::uint32_t frame_index_) {
        EnsurePrepared(frame_index_);
        ++stats.surface_link_call_count;
        if (!frame_cache_valid || frame_cache_components == nullptr || frame_cache_component_count == 0U) {
            return ecs::AppearanceLinkStats{};
        }
        if (surface_components_ == nullptr || surface_component_count_ == 0U) {
            return ecs::AppearanceLinkStats{};
        }

        ecs::AppearanceLinkStats link_stats = LinkSurfaceIncremental(surface_components_,
                                                                      surface_component_count_,
                                                                      frame_cache_components,
                                                                      frame_cache_component_count);
        AccumulateLinkStats(link_stats);
        return link_stats;
    }

    [[nodiscard]] const RuntimeScratchType& RuntimeScratch() const noexcept {
        return runtime_scratch;
    }

    [[nodiscard]] const PrepareStageResultType& LastPrepareResult() const noexcept {
        return frame_prepare_result;
    }

    [[nodiscard]] const AppearanceFrameCoordinatorStats<DimensionT>& Stats() const noexcept {
        return stats;
    }

private:
    void EnsurePrepared(std::uint32_t frame_index_) {
        if (!frame_cache_valid || frame_cache_index != frame_index_ ||
            frame_cache_components != appearance_components ||
            frame_cache_component_count != appearance_component_count) {
            (void)PrepareFrame(frame_index_);
        }
    }

    void AccumulateLinkStats(const ecs::AppearanceLinkStats& link_stats_) noexcept {
        stats.total_link_scanned_count += static_cast<std::uint64_t>(link_stats_.scanned_count);
        stats.total_link_updated_count += static_cast<std::uint64_t>(link_stats_.updated_count);
    }

    [[nodiscard]] bool IsSameAsLastAppliedDirtyPayload(const std::uint32_t* dirty_component_indices_,
                                                       std::uint32_t dirty_component_count_) const noexcept {
        if (dirty_component_count_ == 0U) {
            return last_applied_dirty_component_indices.empty();
        }
        if (last_applied_dirty_component_indices.size() != dirty_component_count_) {
            return false;
        }
        for (std::uint32_t i = 0U; i < dirty_component_count_; ++i) {
            if (last_applied_dirty_component_indices[i] != dirty_component_indices_[i]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] ecs::AppearanceLinkStats LinkGeometryIncremental(
        GeometryType* geometry_components_,
        std::uint32_t geometry_component_count_,
        const AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_) {
        PrepareLinkCache(geometry_link_cache, geometry_components_, geometry_component_count_);
        const std::uint64_t handle_mutation_serial = GeometrySystemType::AppearanceHandleMutationSerial();
        const bool require_full_relink =
            (frame_prepare_result.build_invoked && frame_prepare_result.runtime_stats.full_rebuild != 0U) ||
            !geometry_link_cache.initialized;
        if (require_full_relink) {
            SyncCachedHandlesAndRebuildReverseIndex(geometry_link_cache,
                                                    appearance_component_count_,
                                                    handle_mutation_serial);
            geometry_link_cache.initialized = true;
            return PrepareStageType::LinkGeometry(geometry_components_,
                                                  geometry_component_count_,
                                                  appearance_components_,
                                                  appearance_component_count_);
        }

        BuildAppearanceDirtyMarker();
        const bool need_full_candidate_scan =
            geometry_link_cache.appearance_handle_mutation_serial_seen != handle_mutation_serial;
        BuildGeometryLinkCandidates(appearance_component_count_,
                                    need_full_candidate_scan,
                                    handle_mutation_serial);
        if (geometry_link_cache.candidate_component_indices.empty()) {
            return ecs::AppearanceLinkStats{};
        }
        ++stats.geometry_link_incremental_call_count;
        return ecs::AppearanceLinkSystem<DimensionT>::ApplyToGeometryAligned(
            geometry_components_,
            geometry_component_count_,
            appearance_components_,
            appearance_component_count_,
            geometry_link_cache.candidate_component_indices.data(),
            static_cast<std::uint32_t>(geometry_link_cache.candidate_component_indices.size()));
    }

    [[nodiscard]] ecs::AppearanceLinkStats LinkSurfaceIncremental(
        SurfaceType* surface_components_,
        std::uint32_t surface_component_count_,
        const AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_) {
        PrepareLinkCache(surface_link_cache, surface_components_, surface_component_count_);
        const std::uint64_t handle_mutation_serial = SurfaceSystemType::AppearanceHandleMutationSerial();
        const bool require_full_relink =
            (frame_prepare_result.build_invoked && frame_prepare_result.runtime_stats.full_rebuild != 0U) ||
            !surface_link_cache.initialized;
        if (require_full_relink) {
            SyncCachedHandlesAndRebuildReverseIndex(surface_link_cache,
                                                    appearance_component_count_,
                                                    handle_mutation_serial);
            surface_link_cache.initialized = true;
            return PrepareStageType::LinkSurface(surface_components_,
                                                 surface_component_count_,
                                                 appearance_components_,
                                                 appearance_component_count_);
        }

        BuildAppearanceDirtyMarker();
        const bool need_full_candidate_scan =
            surface_link_cache.appearance_handle_mutation_serial_seen != handle_mutation_serial;
        BuildSurfaceLinkCandidates(appearance_component_count_,
                                   need_full_candidate_scan,
                                   handle_mutation_serial);
        if (surface_link_cache.candidate_component_indices.empty()) {
            return ecs::AppearanceLinkStats{};
        }
        ++stats.surface_link_incremental_call_count;
        return ecs::AppearanceLinkSystem<DimensionT>::ApplyToSurfaceAligned(
            surface_components_,
            surface_component_count_,
            appearance_components_,
            appearance_component_count_,
            surface_link_cache.candidate_component_indices.data(),
            static_cast<std::uint32_t>(surface_link_cache.candidate_component_indices.size()));
    }

    template<typename ComponentT>
    void PrepareLinkCache(LinkCache<ComponentT>& link_cache_,
                          ComponentT* components_,
                          std::uint32_t component_count_) {
        if (components_ == nullptr || component_count_ == 0U) {
            link_cache_.Reset();
            return;
        }
        const bool changed_binding =
            link_cache_.components != components_ ||
            link_cache_.component_count != component_count_;
        link_cache_.components = components_;
        link_cache_.component_count = component_count_;
        link_cache_.Reserve(component_count_);
        if (link_cache_.cached_handles.size() != component_count_) {
            link_cache_.cached_handles.resize(component_count_, ecs::invalid_appearance_handle);
            link_cache_.initialized = false;
        }
        if (changed_binding) {
            link_cache_.initialized = false;
        }
    }

    template<typename ComponentT>
    void SyncCachedHandlesAndRebuildReverseIndex(LinkCache<ComponentT>& link_cache_,
                                                 std::uint32_t appearance_component_count_,
                                                 std::uint64_t handle_mutation_serial_) {
        if (link_cache_.components == nullptr || link_cache_.component_count == 0U) {
            link_cache_.appearance_head_indices.clear();
            link_cache_.component_next_indices.clear();
            link_cache_.appearance_handle_mutation_serial_seen = handle_mutation_serial_;
            return;
        }
        if (link_cache_.cached_handles.size() != link_cache_.component_count) {
            link_cache_.cached_handles.resize(link_cache_.component_count,
                                              ecs::invalid_appearance_handle);
        }
        if (link_cache_.component_next_indices.size() != link_cache_.component_count) {
            link_cache_.component_next_indices.resize(link_cache_.component_count,
                                                      invalid_component_index);
        }
        if (link_cache_.appearance_head_indices.size() != appearance_component_count_) {
            link_cache_.appearance_head_indices.resize(appearance_component_count_,
                                                       invalid_component_index);
        } else {
            for (std::uint32_t i = 0U; i < appearance_component_count_; ++i) {
                link_cache_.appearance_head_indices[i] = invalid_component_index;
            }
        }

        for (std::uint32_t i = 0U; i < link_cache_.component_count; ++i) {
            const ecs::AppearanceHandle handle = link_cache_.components[i].runtime.route.appearance_handle;
            link_cache_.cached_handles[i] = handle;
            link_cache_.component_next_indices[i] = invalid_component_index;
            if (handle.index >= appearance_component_count_) {
                continue;
            }
            link_cache_.component_next_indices[i] = link_cache_.appearance_head_indices[handle.index];
            link_cache_.appearance_head_indices[handle.index] = i;
        }
        link_cache_.appearance_handle_mutation_serial_seen = handle_mutation_serial_;
    }

    void BuildAppearanceDirtyMarker() {
        appearance_dirty_marker_has_entries = false;
        if (last_applied_dirty_component_indices.empty() || appearance_component_count == 0U) {
            return;
        }
        EnsureAppearanceDirtyMarkerCapacity();
        const std::uint32_t marker_epoch = NextAppearanceDirtyMarkerEpoch();
        for (const std::uint32_t appearance_index : last_applied_dirty_component_indices) {
            if (appearance_index >= appearance_component_count) {
                continue;
            }
            appearance_dirty_marker_stamps[appearance_index] = marker_epoch;
            appearance_dirty_marker_has_entries = true;
        }
        appearance_dirty_marker_epoch_active = marker_epoch;
    }

    void BuildGeometryLinkCandidates(std::uint32_t appearance_component_count_,
                                     bool force_component_scan_,
                                     std::uint64_t handle_mutation_serial_) {
        BuildLinkCandidatesForCache(geometry_link_cache,
                                    appearance_component_count_,
                                    stats.geometry_link_candidate_scan_count,
                                    force_component_scan_,
                                    handle_mutation_serial_);
    }

    void BuildSurfaceLinkCandidates(std::uint32_t appearance_component_count_,
                                    bool force_component_scan_,
                                    std::uint64_t handle_mutation_serial_) {
        BuildLinkCandidatesForCache(surface_link_cache,
                                    appearance_component_count_,
                                    stats.surface_link_candidate_scan_count,
                                    force_component_scan_,
                                    handle_mutation_serial_);
    }

    template<typename ComponentT>
    void BuildLinkCandidatesForCache(LinkCache<ComponentT>& link_cache_,
                                     std::uint32_t appearance_component_count_,
                                     std::uint64_t& scan_counter_,
                                     bool force_component_scan_,
                                     std::uint64_t handle_mutation_serial_) {
        link_cache_.candidate_component_indices.clear();
        if (link_cache_.components == nullptr || link_cache_.component_count == 0U) {
            return;
        }
        if (!appearance_dirty_marker_has_entries &&
            !force_component_scan_ &&
            link_cache_.appearance_handle_mutation_serial_seen == handle_mutation_serial_) {
            return;
        }

        link_cache_.EnsureComponentDedupCapacity(link_cache_.component_count);
        const std::uint32_t dedup_epoch = link_cache_.NextComponentDedupEpoch();
        const bool need_component_scan =
            force_component_scan_ ||
            link_cache_.appearance_handle_mutation_serial_seen != handle_mutation_serial_;
        if (need_component_scan) {
            if (link_cache_.cached_handles.size() != link_cache_.component_count) {
                link_cache_.cached_handles.resize(link_cache_.component_count,
                                                  ecs::invalid_appearance_handle);
            }
            if (link_cache_.component_next_indices.size() != link_cache_.component_count) {
                link_cache_.component_next_indices.resize(link_cache_.component_count,
                                                          invalid_component_index);
            }
            if (link_cache_.appearance_head_indices.size() != appearance_component_count_) {
                link_cache_.appearance_head_indices.resize(appearance_component_count_,
                                                           invalid_component_index);
            } else {
                for (std::uint32_t i = 0U; i < appearance_component_count_; ++i) {
                    link_cache_.appearance_head_indices[i] = invalid_component_index;
                }
            }

            for (std::uint32_t i = 0U; i < link_cache_.component_count; ++i) {
                ++scan_counter_;
                const ecs::AppearanceHandle current_handle =
                    link_cache_.components[i].runtime.route.appearance_handle;
                const ecs::AppearanceHandle previous_handle = link_cache_.cached_handles[i];
                const bool handle_changed =
                    current_handle.index != previous_handle.index ||
                    current_handle.generation != previous_handle.generation;

                link_cache_.cached_handles[i] = current_handle;
                link_cache_.component_next_indices[i] = invalid_component_index;
                if (current_handle.index < appearance_component_count_) {
                    link_cache_.component_next_indices[i] =
                        link_cache_.appearance_head_indices[current_handle.index];
                    link_cache_.appearance_head_indices[current_handle.index] = i;
                }

                const bool dirty_match =
                    appearance_dirty_marker_has_entries &&
                    current_handle.index < appearance_component_count_ &&
                    appearance_dirty_marker_stamps[current_handle.index] == appearance_dirty_marker_epoch_active;
                if (handle_changed || dirty_match) {
                    AppendCandidateComponentIndex(link_cache_, dedup_epoch, i);
                }
            }
            link_cache_.appearance_handle_mutation_serial_seen = handle_mutation_serial_;
            return;
        }

        if (!appearance_dirty_marker_has_entries || appearance_component_count_ == 0U) {
            return;
        }

        for (const std::uint32_t appearance_index : last_applied_dirty_component_indices) {
            if (appearance_index >= appearance_component_count_) {
                continue;
            }
            if (appearance_index >= link_cache_.appearance_head_indices.size()) {
                continue;
            }
            if (appearance_dirty_marker_stamps[appearance_index] != appearance_dirty_marker_epoch_active) {
                continue;
            }
            std::uint32_t component_index = link_cache_.appearance_head_indices[appearance_index];
            while (component_index != invalid_component_index) {
                AppendCandidateComponentIndex(link_cache_, dedup_epoch, component_index);
                if (component_index >= link_cache_.component_next_indices.size()) {
                    break;
                }
                component_index = link_cache_.component_next_indices[component_index];
            }
        }
    }

    template<typename ComponentT>
    void AppendCandidateComponentIndex(LinkCache<ComponentT>& link_cache_,
                                       std::uint32_t dedup_epoch_,
                                       std::uint32_t component_index_) {
        if (link_cache_.component_dedup_stamps[component_index_] == dedup_epoch_) {
            return;
        }
        link_cache_.component_dedup_stamps[component_index_] = dedup_epoch_;
        link_cache_.candidate_component_indices.push_back(component_index_);
    }

    void NormalizeDirtyHintPayload(const std::uint32_t*& out_dirty_indices_,
                                   std::uint32_t& out_dirty_count_) {
        out_dirty_indices_ = nullptr;
        out_dirty_count_ = 0U;
        if (accumulated_dirty_component_indices.empty()) {
            return;
        }
        if (accumulated_dirty_component_indices.size() == 1U) {
            const std::uint32_t single_component_index = accumulated_dirty_component_indices[0U];
            if (single_component_index < appearance_component_count) {
                out_dirty_indices_ = accumulated_dirty_component_indices.data();
                out_dirty_count_ = 1U;
                ++stats.dirty_hint_unique_count;
            } else {
                ++stats.dirty_hint_out_of_range_drop_count;
            }
            return;
        }

        normalized_dirty_component_indices.clear();
        normalized_dirty_component_indices.reserve(accumulated_dirty_component_indices.size());
        EnsureDirtyMarkerCapacity();
        const std::uint32_t marker_epoch = NextDirtyMarkerEpoch();

        for (const std::uint32_t component_index : accumulated_dirty_component_indices) {
            if (component_index >= appearance_component_count) {
                ++stats.dirty_hint_out_of_range_drop_count;
                continue;
            }
            if (dirty_marker_stamps[component_index] == marker_epoch) {
                ++stats.dirty_hint_duplicate_drop_count;
                continue;
            }
            dirty_marker_stamps[component_index] = marker_epoch;
            normalized_dirty_component_indices.push_back(component_index);
        }

        stats.dirty_hint_unique_count += static_cast<std::uint64_t>(normalized_dirty_component_indices.size());
        if (normalized_dirty_component_indices.empty()) {
            return;
        }
        out_dirty_indices_ = normalized_dirty_component_indices.data();
        out_dirty_count_ = static_cast<std::uint32_t>(normalized_dirty_component_indices.size());
    }

    void EnsureDirtyMarkerCapacity() {
        if (appearance_component_count == 0U) {
            return;
        }
        if (dirty_marker_stamps.size() >= appearance_component_count) {
            return;
        }
        dirty_marker_stamps.resize(appearance_component_count, 0U);
    }

    [[nodiscard]] std::uint32_t NextDirtyMarkerEpoch() {
        if (dirty_marker_epoch == 0U || dirty_marker_epoch == (std::numeric_limits<std::uint32_t>::max)()) {
            for (std::size_t i = 0U; i < dirty_marker_stamps.size(); ++i) {
                dirty_marker_stamps[i] = 0U;
            }
            dirty_marker_epoch = 1U;
        }
        const std::uint32_t marker_epoch = dirty_marker_epoch;
        ++dirty_marker_epoch;
        return marker_epoch;
    }

    void EnsureAppearanceDirtyMarkerCapacity() {
        if (appearance_component_count == 0U) {
            return;
        }
        if (appearance_dirty_marker_stamps.size() >= appearance_component_count) {
            return;
        }
        appearance_dirty_marker_stamps.resize(appearance_component_count, 0U);
    }

    [[nodiscard]] std::uint32_t NextAppearanceDirtyMarkerEpoch() {
        if (appearance_dirty_marker_epoch == 0U ||
            appearance_dirty_marker_epoch == (std::numeric_limits<std::uint32_t>::max)()) {
            for (std::size_t i = 0U; i < appearance_dirty_marker_stamps.size(); ++i) {
                appearance_dirty_marker_stamps[i] = 0U;
            }
            appearance_dirty_marker_epoch = 1U;
        }
        const std::uint32_t marker_epoch = appearance_dirty_marker_epoch;
        ++appearance_dirty_marker_epoch;
        return marker_epoch;
    }

private:
    AppearanceType* appearance_components = nullptr;
    std::uint32_t appearance_component_count = 0U;
    AppearanceFrameCoordinatorMcVector<std::uint32_t> accumulated_dirty_component_indices{};
    AppearanceFrameCoordinatorMcVector<std::uint32_t> normalized_dirty_component_indices{};
    AppearanceFrameCoordinatorMcVector<std::uint32_t> dirty_marker_stamps{};
    AppearanceFrameCoordinatorMcVector<std::uint32_t> last_applied_dirty_component_indices{};
    std::uint32_t dirty_marker_epoch = 1U;
    AppearanceFrameCoordinatorMcVector<std::uint32_t> appearance_dirty_marker_stamps{};
    std::uint32_t appearance_dirty_marker_epoch = 1U;
    std::uint32_t appearance_dirty_marker_epoch_active = 0U;
    bool appearance_dirty_marker_has_entries = false;

    RuntimeScratchType runtime_scratch{};
    PrepareStageResultType frame_prepare_result{};

    AppearanceType* frame_cache_components = nullptr;
    std::uint32_t frame_cache_component_count = 0U;
    std::uint32_t frame_cache_index = invalid_frame_index;
    std::uint64_t frame_cache_source_revision = 0U;
    std::uint64_t frame_cache_dirty_revision = 0U;
    std::uint64_t source_revision = 0U;
    std::uint64_t dirty_revision = 0U;
    bool frame_cache_valid = false;
    LinkCache<GeometryType> geometry_link_cache{};
    LinkCache<SurfaceType> surface_link_cache{};

    AppearanceFrameCoordinatorStats<DimensionT> stats{};
};

} // namespace vr::render

