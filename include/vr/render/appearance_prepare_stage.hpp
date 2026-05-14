#pragma once

#include "vr/ecs/system/appearance_link_system.hpp"
#include "vr/ecs/system/appearance_runtime_system.hpp"

#include <concepts>
#include <cstdint>

namespace vr::render {

template<ecs::DimensionTag DimensionT>
struct AppearancePrepareStageResult final {
    ecs::AppearanceRuntimeBuildStats runtime_stats{};
    ecs::AppearanceLinkStats link_stats{};
    bool has_appearance_data = false;
    bool build_invoked = false;
};

template<ecs::DimensionTag DimensionT>
class AppearancePrepareStage final {
public:
    using AppearanceType = ecs::Appearance<DimensionT>;
    using AppearanceRuntimeSystemType = ecs::AppearanceRuntimeSystem<DimensionT>;
    using AppearanceRuntimeScratchType = ecs::AppearanceRuntimeScratch<DimensionT>;
    using AppearanceLinkSystemType = ecs::AppearanceLinkSystem<DimensionT>;

    using GeometryType = ecs::Geometry<DimensionT>;
    using SurfaceType = ecs::Surface<DimensionT>;

    static void Reserve(AppearanceRuntimeScratchType& appearance_runtime_scratch_,
                        std::uint32_t appearance_component_count_) {
        if (appearance_component_count_ == 0U) {
            return;
        }
        AppearanceRuntimeSystemType::Reserve(appearance_runtime_scratch_,
                                             appearance_component_count_);
    }

    [[nodiscard]] static AppearancePrepareStageResult<DimensionT> BuildAndLinkGeometry(
        AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        AppearanceRuntimeScratchType& appearance_runtime_scratch_,
        GeometryType* geometry_components_,
        std::uint32_t geometry_component_count_) {
        AppearancePrepareStageResult<DimensionT> result{};
        if (appearance_components_ == nullptr || appearance_component_count_ == 0U) {
            return result;
        }

        result.has_appearance_data = true;
        result.runtime_stats = BuildRuntimeInternal(appearance_components_,
                                                    appearance_component_count_,
                                                    dirty_component_indices_,
                                                    dirty_component_count_,
                                                    appearance_runtime_scratch_);
        result.build_invoked = true;

        if (geometry_components_ != nullptr && geometry_component_count_ > 0U) {
            result.link_stats = LinkGeometry(geometry_components_,
                                             geometry_component_count_,
                                             appearance_components_,
                                             appearance_component_count_);
        }
        return result;
    }

    [[nodiscard]] static AppearancePrepareStageResult<DimensionT> BuildAndLinkSurface(
        AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        AppearanceRuntimeScratchType& appearance_runtime_scratch_,
        SurfaceType* surface_components_,
        std::uint32_t surface_component_count_) {
        AppearancePrepareStageResult<DimensionT> result{};
        if (appearance_components_ == nullptr || appearance_component_count_ == 0U) {
            return result;
        }

        result.has_appearance_data = true;
        result.runtime_stats = BuildRuntimeInternal(appearance_components_,
                                                    appearance_component_count_,
                                                    dirty_component_indices_,
                                                    dirty_component_count_,
                                                    appearance_runtime_scratch_);
        result.build_invoked = true;

        if (surface_components_ != nullptr && surface_component_count_ > 0U) {
            result.link_stats = LinkSurface(surface_components_,
                                            surface_component_count_,
                                            appearance_components_,
                                            appearance_component_count_);
        }
        return result;
    }

    [[nodiscard]] static AppearancePrepareStageResult<DimensionT> BuildRuntimeOnly(
        AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        AppearanceRuntimeScratchType& appearance_runtime_scratch_) {
        AppearancePrepareStageResult<DimensionT> result{};
        if (appearance_components_ == nullptr || appearance_component_count_ == 0U) {
            return result;
        }
        result.has_appearance_data = true;
        result.build_invoked = true;
        result.runtime_stats = BuildRuntimeInternal(appearance_components_,
                                                    appearance_component_count_,
                                                    dirty_component_indices_,
                                                    dirty_component_count_,
                                                    appearance_runtime_scratch_);
        return result;
    }

    [[nodiscard]] static ecs::AppearanceLinkStats LinkGeometry(
        GeometryType* geometry_components_,
        std::uint32_t geometry_component_count_,
        const AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_) noexcept {
        if (geometry_components_ == nullptr || geometry_component_count_ == 0U ||
            appearance_components_ == nullptr || appearance_component_count_ == 0U) {
            return ecs::AppearanceLinkStats{};
        }
        return AppearanceLinkSystemType::ApplyToGeometryAligned(geometry_components_,
                                                                 geometry_component_count_,
                                                                 appearance_components_,
                                                                 appearance_component_count_);
    }

    [[nodiscard]] static ecs::AppearanceLinkStats LinkSurface(
        SurfaceType* surface_components_,
        std::uint32_t surface_component_count_,
        const AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_) noexcept {
        if (surface_components_ == nullptr || surface_component_count_ == 0U ||
            appearance_components_ == nullptr || appearance_component_count_ == 0U) {
            return ecs::AppearanceLinkStats{};
        }
        return AppearanceLinkSystemType::ApplyToSurfaceAligned(surface_components_,
                                                                surface_component_count_,
                                                                appearance_components_,
                                                                appearance_component_count_);
    }

private:
    [[nodiscard]] static ecs::AppearanceRuntimeBuildStats BuildRuntimeInternal(
        AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        AppearanceRuntimeScratchType& appearance_runtime_scratch_) {
        ecs::AppearanceRuntimeBuildHint appearance_build_hint{};
        appearance_build_hint.dirty_component_indices = dirty_component_indices_;
        appearance_build_hint.dirty_component_count = dirty_component_count_;
        appearance_build_hint.use_dirty_component_indices =
            (dirty_component_indices_ != nullptr && dirty_component_count_ > 0U)
                ? 1U
                : 0U;

        return AppearanceRuntimeSystemType::Build(
            appearance_components_,
            appearance_component_count_,
            appearance_runtime_scratch_,
            AppearanceRuntimeSystemType::DefaultPipelinePolicy(),
            AppearanceRuntimeSystemType::DefaultSortPolicy(),
            AppearanceRuntimeSystemType::DefaultBuildConfig(),
            appearance_build_hint);
    }
};

} // namespace vr::render

