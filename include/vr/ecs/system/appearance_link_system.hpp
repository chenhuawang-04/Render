#pragma once

#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/surface_system.hpp"

#include <concepts>
#include <cstdint>

namespace vr::ecs {

struct AppearanceLinkStats final {
    std::uint32_t scanned_count;
    std::uint32_t resolved_count;
    std::uint32_t updated_count;
    std::uint32_t missing_handle_count;
    std::uint32_t out_of_range_handle_count;
    std::uint32_t generation_mismatch_count;
    std::uint32_t invisible_appearance_count;
    std::uint32_t out_of_range_component_index_count;
};

static_assert(PurePodAppearanceComponent<AppearanceLinkStats>);

template<DimensionTag DimensionT>
class AppearanceLinkSystem final {
public:
    using AppearanceType = Appearance<DimensionT>;
    using AppearanceSystemType = AppearanceSystem<DimensionT>;
    using GeometryType = Geometry<DimensionT>;
    using GeometrySystemType = GeometrySystem<DimensionT>;
    using SurfaceType = Surface<DimensionT>;
    using SurfaceSystemType = SurfaceSystem<DimensionT>;

    [[nodiscard]] static AppearanceLinkStats ApplyToGeometryAligned(
        GeometryType* geometry_components_,
        std::uint32_t geometry_component_count_,
        const AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_) noexcept {
        AppearanceLinkStats stats{};
        if (geometry_components_ == nullptr || geometry_component_count_ == 0U) {
            return stats;
        }

        for (std::uint32_t i = 0U; i < geometry_component_count_; ++i) {
            LinkOneGeometry(geometry_components_[i],
                            appearance_components_,
                            appearance_component_count_,
                            stats);
        }
        return stats;
    }

    [[nodiscard]] static AppearanceLinkStats ApplyToGeometryAligned(
        GeometryType* geometry_components_,
        std::uint32_t geometry_component_count_,
        const AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_,
        const std::uint32_t* geometry_component_indices_,
        std::uint32_t geometry_component_index_count_) noexcept {
        AppearanceLinkStats stats{};
        if (geometry_components_ == nullptr || geometry_component_count_ == 0U) {
            return stats;
        }
        if (geometry_component_indices_ == nullptr || geometry_component_index_count_ == 0U) {
            return ApplyToGeometryAligned(geometry_components_,
                                          geometry_component_count_,
                                          appearance_components_,
                                          appearance_component_count_);
        }

        for (std::uint32_t i = 0U; i < geometry_component_index_count_; ++i) {
            const std::uint32_t component_index = geometry_component_indices_[i];
            if (component_index >= geometry_component_count_) {
                ++stats.out_of_range_component_index_count;
                continue;
            }
            LinkOneGeometry(geometry_components_[component_index],
                            appearance_components_,
                            appearance_component_count_,
                            stats);
        }
        return stats;
    }

    [[nodiscard]] static AppearanceLinkStats ApplyToSurfaceAligned(
        SurfaceType* surface_components_,
        std::uint32_t surface_component_count_,
        const AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_) noexcept {
        AppearanceLinkStats stats{};
        if (surface_components_ == nullptr || surface_component_count_ == 0U) {
            return stats;
        }

        for (std::uint32_t i = 0U; i < surface_component_count_; ++i) {
            LinkOneSurface(surface_components_[i],
                           appearance_components_,
                           appearance_component_count_,
                           stats);
        }
        return stats;
    }

    [[nodiscard]] static AppearanceLinkStats ApplyToSurfaceAligned(
        SurfaceType* surface_components_,
        std::uint32_t surface_component_count_,
        const AppearanceType* appearance_components_,
        std::uint32_t appearance_component_count_,
        const std::uint32_t* surface_component_indices_,
        std::uint32_t surface_component_index_count_) noexcept {
        AppearanceLinkStats stats{};
        if (surface_components_ == nullptr || surface_component_count_ == 0U) {
            return stats;
        }
        if (surface_component_indices_ == nullptr || surface_component_index_count_ == 0U) {
            return ApplyToSurfaceAligned(surface_components_,
                                         surface_component_count_,
                                         appearance_components_,
                                         appearance_component_count_);
        }

        for (std::uint32_t i = 0U; i < surface_component_index_count_; ++i) {
            const std::uint32_t component_index = surface_component_indices_[i];
            if (component_index >= surface_component_count_) {
                ++stats.out_of_range_component_index_count;
                continue;
            }
            LinkOneSurface(surface_components_[component_index],
                           appearance_components_,
                           appearance_component_count_,
                           stats);
        }
        return stats;
    }

private:
    static void LinkOneGeometry(GeometryType& geometry_component_,
                                const AppearanceType* appearance_components_,
                                std::uint32_t appearance_component_count_,
                                AppearanceLinkStats& stats_) noexcept {
        ++stats_.scanned_count;

        const AppearanceHandle handle = geometry_component_.runtime.route.appearance_handle;
        if (handle.index == invalid_appearance_index || handle.generation == 0U) {
            ++stats_.missing_handle_count;
            return;
        }
        if (appearance_components_ == nullptr || handle.index >= appearance_component_count_) {
            ++stats_.out_of_range_handle_count;
            return;
        }

        const AppearanceType& appearance_component = appearance_components_[handle.index];
        if (appearance_component.runtime.gpu_record_handle.index != handle.index ||
            appearance_component.runtime.gpu_record_handle.generation != handle.generation) {
            ++stats_.generation_mismatch_count;
            return;
        }
        if (!AppearanceSystemType::IsVisibleForBatch(appearance_component)) {
            ++stats_.invisible_appearance_count;
            return;
        }

        ++stats_.resolved_count;
        if (GeometrySystemType::SetAppearanceRuntimeLink(geometry_component_,
                                                         handle,
                                                         appearance_component.runtime.sort_key,
                                                         appearance_component.runtime.pipeline_key,
                                                         appearance_component.runtime.resource_key)) {
            ++stats_.updated_count;
        }
    }

    static void LinkOneSurface(SurfaceType& surface_component_,
                               const AppearanceType* appearance_components_,
                               std::uint32_t appearance_component_count_,
                               AppearanceLinkStats& stats_) noexcept {
        ++stats_.scanned_count;

        const AppearanceHandle handle = surface_component_.runtime.route.appearance_handle;
        if (handle.index == invalid_appearance_index || handle.generation == 0U) {
            ++stats_.missing_handle_count;
            return;
        }
        if (appearance_components_ == nullptr || handle.index >= appearance_component_count_) {
            ++stats_.out_of_range_handle_count;
            return;
        }

        const AppearanceType& appearance_component = appearance_components_[handle.index];
        if (appearance_component.runtime.gpu_record_handle.index != handle.index ||
            appearance_component.runtime.gpu_record_handle.generation != handle.generation) {
            ++stats_.generation_mismatch_count;
            return;
        }
        if (!AppearanceSystemType::IsVisibleForBatch(appearance_component)) {
            ++stats_.invisible_appearance_count;
            return;
        }

        ++stats_.resolved_count;
        if (SurfaceSystemType::SetAppearanceRuntimeLink(surface_component_,
                                                        handle,
                                                        appearance_component.runtime.sort_key,
                                                        appearance_component.runtime.pipeline_key,
                                                        appearance_component.runtime.resource_key)) {
            ++stats_.updated_count;
        }
    }
};

} // namespace vr::ecs

