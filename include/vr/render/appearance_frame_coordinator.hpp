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
};

template<ecs::DimensionTag DimensionT>
class AppearanceFrameCoordinator final {
public:
    using AppearanceType = ecs::Appearance<DimensionT>;
    using GeometryType = ecs::Geometry<DimensionT>;
    using SurfaceType = ecs::Surface<DimensionT>;

    using PrepareStageType = AppearancePrepareStage<DimensionT>;
    using PrepareStageResultType = AppearancePrepareStageResult<DimensionT>;
    using RuntimeScratchType = ecs::AppearanceRuntimeScratch<DimensionT>;

    static constexpr std::uint32_t invalid_frame_index = (std::numeric_limits<std::uint32_t>::max)();

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

        const bool is_duplicate_dirty_payload =
            frame_cache_valid &&
            accumulated_dirty_component_indices.empty() &&
            IsSameAsLastAppliedDirtyPayload(dirty_component_indices_, dirty_component_count_);
        if (is_duplicate_dirty_payload) {
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
        last_applied_dirty_component_indices.clear();
        runtime_scratch = {};
        stats = {};
        source_revision = 0U;
        dirty_revision = 0U;
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
            PrepareStageResultType reused_result = frame_prepare_result;
            reused_result.build_invoked = false;
            return reused_result;
        }

        const std::uint32_t* dirty_indices = nullptr;
        std::uint32_t dirty_count = 0U;
        if (!accumulated_dirty_component_indices.empty()) {
            dirty_indices = accumulated_dirty_component_indices.data();
            dirty_count = static_cast<std::uint32_t>(accumulated_dirty_component_indices.size());
        }

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

        ecs::AppearanceLinkStats link_stats = PrepareStageType::LinkGeometry(geometry_components_,
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

        ecs::AppearanceLinkStats link_stats = PrepareStageType::LinkSurface(surface_components_,
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

private:
    AppearanceType* appearance_components = nullptr;
    std::uint32_t appearance_component_count = 0U;
    AppearanceFrameCoordinatorMcVector<std::uint32_t> accumulated_dirty_component_indices{};
    AppearanceFrameCoordinatorMcVector<std::uint32_t> last_applied_dirty_component_indices{};

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

    AppearanceFrameCoordinatorStats<DimensionT> stats{};
};

} // namespace vr::render
