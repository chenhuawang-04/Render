#pragma once

#include "vr/ecs/component/appearance_component.hpp"

#include <cstdint>

namespace vr::ecs {

#define VR_ECS_VISUAL_RUNTIME_ROUTE_SORT_KEY_FIELD() \
    std::uint64_t sort_key

#define VR_ECS_VISUAL_RUNTIME_ROUTE_TRAILING_FIELDS(PassHintT) \
    std::uint32_t visual_resource_id; \
    std::uint32_t batch_tag; \
    std::uint32_t user_data; \
    AppearanceHandle appearance_handle; \
    std::uint32_t appearance_pipeline_bucket; \
    std::uint32_t appearance_visual_resource_id; \
    std::uint16_t depth_bin; \
    std::uint8_t visible; \
    PassHintT pass_hint; \
    std::uint32_t dirty_flags

template<typename RouteT>
[[nodiscard]] constexpr bool HasLinkedAppearanceHandle(const RouteT& route_) noexcept {
    return route_.appearance_handle.index != invalid_appearance_handle.index &&
           route_.appearance_handle.generation != invalid_appearance_handle.generation;
}

template<typename RouteT>
[[nodiscard]] constexpr bool HasAppearanceHandleChanged(const RouteT& route_,
                                                        AppearanceHandle appearance_handle_) noexcept {
    return route_.appearance_handle.index != appearance_handle_.index ||
           route_.appearance_handle.generation != appearance_handle_.generation;
}

template<typename RouteT>
[[nodiscard]] constexpr bool HasAppearanceRuntimeRouteChanged(
    const RouteT& route_,
    AppearanceHandle appearance_handle_,
    std::uint32_t pipeline_bucket_,
    std::uint32_t appearance_visual_resource_id_) noexcept {
    return HasAppearanceHandleChanged(route_, appearance_handle_) ||
           route_.appearance_pipeline_bucket != pipeline_bucket_ ||
           route_.appearance_visual_resource_id != appearance_visual_resource_id_;
}

template<typename RouteT>
[[nodiscard]] constexpr bool IsAppearanceRuntimeRouteClear(const RouteT& route_) noexcept {
    return !HasLinkedAppearanceHandle(route_) &&
           route_.appearance_pipeline_bucket == 0U &&
           route_.appearance_visual_resource_id == 0U;
}

template<typename RouteT>
constexpr void StoreAppearanceRuntimeRoute(RouteT& route_,
                                           AppearanceHandle appearance_handle_,
                                           std::uint32_t pipeline_bucket_,
                                           std::uint32_t appearance_visual_resource_id_) noexcept {
    route_.appearance_handle = appearance_handle_;
    route_.appearance_pipeline_bucket = pipeline_bucket_;
    route_.appearance_visual_resource_id = appearance_visual_resource_id_;
}

template<typename RouteT>
constexpr void ClearAppearanceRuntimeRoute(RouteT& route_) noexcept {
    route_.appearance_handle = invalid_appearance_handle;
    route_.appearance_pipeline_bucket = 0U;
    route_.appearance_visual_resource_id = 0U;
}

} // namespace vr::ecs
