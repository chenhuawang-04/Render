#pragma once

#include "vr/render/appearance_frame_coordinator.hpp"
#include "vr/render/appearance_prepare_stage.hpp"

#include <cstdint>

namespace vr::render {

template<ecs::DimensionTag DimensionT>
class AppearancePrepareBridge final {
public:
    using AppearanceType = ecs::Appearance<DimensionT>;
    using GeometryType = ecs::Geometry<DimensionT>;
    using SurfaceType = ecs::Surface<DimensionT>;

    using RuntimeScratchType = ecs::AppearanceRuntimeScratch<DimensionT>;
    using RuntimeStatsType = ecs::AppearanceRuntimeBuildStats;
    using LinkStatsType = ecs::AppearanceLinkStats;
    using PrepareStageType = AppearancePrepareStage<DimensionT>;
    using PrepareStageResultType = AppearancePrepareStageResult<DimensionT>;
    using FrameCoordinatorType = AppearanceFrameCoordinator<DimensionT>;

    struct PrepareResult final {
        RuntimeStatsType runtime_stats{};
        LinkStatsType link_stats{};
        bool has_appearance_data = false;
        bool build_invoked = false;
        bool cache_reused = false;
    };

public:
    AppearancePrepareBridge() = default;
    ~AppearancePrepareBridge() = default;

    AppearancePrepareBridge(const AppearancePrepareBridge&) = delete;
    AppearancePrepareBridge& operator=(const AppearancePrepareBridge&) = delete;
    AppearancePrepareBridge(AppearancePrepareBridge&&) = delete;
    AppearancePrepareBridge& operator=(AppearancePrepareBridge&&) = delete;

    void Reset() noexcept {
        appearance_components = nullptr;
        appearance_component_count = 0U;
        pending_dirty_component_indices = nullptr;
        pending_dirty_component_count = 0U;
        runtime_scratch = {};
        appearance_frame_coordinator = nullptr;
    }

    void SetAppearanceData(AppearanceType* appearance_components_,
                           std::uint32_t appearance_component_count_) noexcept {
        appearance_components = appearance_components_;
        appearance_component_count = appearance_component_count_;
    }

    void SetDirtyHint(const std::uint32_t* dirty_component_indices_,
                      std::uint32_t dirty_component_count_) noexcept {
        pending_dirty_component_indices = dirty_component_indices_;
        pending_dirty_component_count = dirty_component_count_;
    }

    void SetCoordinator(FrameCoordinatorType* appearance_frame_coordinator_) noexcept {
        appearance_frame_coordinator = appearance_frame_coordinator_;
    }

    void Reserve(std::uint32_t appearance_component_count_) {
        if (appearance_frame_coordinator != nullptr) {
            appearance_frame_coordinator->Reserve(appearance_component_count_);
            return;
        }
        PrepareStageType::Reserve(runtime_scratch, appearance_component_count_);
    }

    [[nodiscard]] PrepareResult PrepareGeometry(GeometryType* geometry_components_,
                                                std::uint32_t geometry_component_count_,
                                                std::uint32_t frame_index_) {
        PrepareResult result = PrepareCore(frame_index_);
        if (result.has_appearance_data &&
            geometry_components_ != nullptr &&
            geometry_component_count_ > 0U) {
            if (appearance_frame_coordinator != nullptr) {
                result.link_stats = appearance_frame_coordinator->LinkGeometry(geometry_components_,
                                                                                geometry_component_count_,
                                                                                frame_index_);
            } else {
                result.link_stats = PrepareStageType::LinkGeometry(geometry_components_,
                                                                   geometry_component_count_,
                                                                   appearance_components,
                                                                   appearance_component_count);
            }
        }
        return result;
    }

    [[nodiscard]] PrepareResult PrepareSurface(SurfaceType* surface_components_,
                                               std::uint32_t surface_component_count_,
                                               std::uint32_t frame_index_) {
        PrepareResult result = PrepareCore(frame_index_);
        if (result.has_appearance_data &&
            surface_components_ != nullptr &&
            surface_component_count_ > 0U) {
            if (appearance_frame_coordinator != nullptr) {
                result.link_stats = appearance_frame_coordinator->LinkSurface(surface_components_,
                                                                               surface_component_count_,
                                                                               frame_index_);
            } else {
                result.link_stats = PrepareStageType::LinkSurface(surface_components_,
                                                                  surface_component_count_,
                                                                  appearance_components,
                                                                  appearance_component_count);
            }
        }
        return result;
    }

    [[nodiscard]] const RuntimeScratchType& RuntimeScratch() const noexcept {
        if (appearance_frame_coordinator != nullptr) {
            return appearance_frame_coordinator->RuntimeScratch();
        }
        return runtime_scratch;
    }

private:
    [[nodiscard]] PrepareResult PrepareCore(std::uint32_t frame_index_) {
        PrepareResult result{};
        if (appearance_components == nullptr || appearance_component_count == 0U) {
            ClearDirtyHint();
            return result;
        }

        if (appearance_frame_coordinator != nullptr) {
            appearance_frame_coordinator->SetAppearanceData(appearance_components, appearance_component_count);
            if (pending_dirty_component_indices != nullptr && pending_dirty_component_count > 0U) {
                appearance_frame_coordinator->SetDirtyHint(pending_dirty_component_indices,
                                                           pending_dirty_component_count);
            }
            const PrepareStageResultType prepare_result =
                appearance_frame_coordinator->PrepareFrame(frame_index_);
            result.runtime_stats = prepare_result.runtime_stats;
            result.has_appearance_data = prepare_result.has_appearance_data;
            result.build_invoked = prepare_result.build_invoked;
            result.cache_reused = (!prepare_result.build_invoked) ||
                                  (prepare_result.runtime_stats.full_rebuild == 0U);
            ClearDirtyHint();
            return result;
        }

        const PrepareStageResultType prepare_result = PrepareStageType::BuildRuntimeOnly(
            appearance_components,
            appearance_component_count,
            pending_dirty_component_indices,
            pending_dirty_component_count,
            runtime_scratch);
        result.runtime_stats = prepare_result.runtime_stats;
        result.has_appearance_data = prepare_result.has_appearance_data;
        result.build_invoked = prepare_result.build_invoked;
        result.cache_reused = (prepare_result.has_appearance_data &&
                               prepare_result.runtime_stats.full_rebuild == 0U);
        ClearDirtyHint();
        return result;
    }

    void ClearDirtyHint() noexcept {
        pending_dirty_component_indices = nullptr;
        pending_dirty_component_count = 0U;
    }

private:
    AppearanceType* appearance_components = nullptr;
    std::uint32_t appearance_component_count = 0U;
    const std::uint32_t* pending_dirty_component_indices = nullptr;
    std::uint32_t pending_dirty_component_count = 0U;

    RuntimeScratchType runtime_scratch{};
    FrameCoordinatorType* appearance_frame_coordinator = nullptr;
};

} // namespace vr::render

