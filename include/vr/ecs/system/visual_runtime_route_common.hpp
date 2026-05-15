#pragma once

#include "vr/ecs/component/appearance_component.hpp"

#include <cstdint>

namespace vr::ecs {

#define VR_ECS_VISUAL_RUNTIME_ROUTE_SORT_KEY_FIELD() \
    std::uint64_t sort_key

#define VR_ECS_VISUAL_RUNTIME_ROUTE_TRAILING_FIELDS(PassHintT) \
    std::uint32_t authoring_visual_resource_id; \
    std::uint32_t batch_tag; \
    std::uint32_t user_data; \
    AppearanceHandle appearance_handle; \
    std::uint32_t appearance_pipeline_bucket; \
    std::uint32_t linked_visual_resource_id; \
    std::uint16_t depth_bin; \
    std::uint8_t visible; \
    PassHintT pass_hint; \
    std::uint32_t dirty_flags

template<typename RouteT>
[[nodiscard]] constexpr bool HasAppearanceHandleChanged(const RouteT& route_,
                                                        AppearanceHandle appearance_handle_) noexcept;

template<typename RouteT>
constexpr void ClearAppearanceRuntimeRoute(RouteT& route_) noexcept;

template<typename RouteT>
[[nodiscard]] constexpr bool HasLinkedAppearanceHandle(const RouteT& route_) noexcept {
    return route_.appearance_handle.index != invalid_appearance_handle.index &&
           route_.appearance_handle.generation != invalid_appearance_handle.generation;
}

template<typename RouteT, typename PassHintT>
constexpr void InitializeVisualRuntimeRouteCommon(RouteT& route_,
                                                  PassHintT pass_hint_,
                                                  std::uint32_t dirty_flags_) noexcept {
    route_.sort_key = 0U;
    route_.authoring_visual_resource_id = 0U;
    route_.batch_tag = 0U;
    route_.user_data = 0U;
    ClearAppearanceRuntimeRoute(route_);
    route_.depth_bin = 0U;
    route_.visible = 1U;
    route_.pass_hint = pass_hint_;
    route_.dirty_flags = dirty_flags_;
}

template<typename RouteT>
[[nodiscard]] constexpr std::uint32_t VisualRuntimeRouteDirtyFlags(const RouteT& route_) noexcept {
    return route_.dirty_flags;
}

template<typename RouteT>
[[nodiscard]] constexpr bool HasVisualRuntimeRouteDirtyFlags(const RouteT& route_,
                                                             std::uint32_t dirty_mask_) noexcept {
    return (route_.dirty_flags & dirty_mask_) != 0U;
}

template<typename RouteT>
constexpr void MarkVisualRuntimeRouteDirty(RouteT& route_, std::uint32_t dirty_mask_) noexcept {
    route_.dirty_flags |= dirty_mask_;
}

template<typename RouteT>
constexpr void ClearVisualRuntimeRouteDirtyFlags(RouteT& route_,
                                                 std::uint32_t clear_mask_) noexcept {
    route_.dirty_flags &= ~clear_mask_;
}

template<typename RouteT>
[[nodiscard]] constexpr bool SetVisualRuntimeRouteVisible(RouteT& route_, bool visible_) noexcept {
    const std::uint8_t visible_value = visible_ ? 1U : 0U;
    if (route_.visible == visible_value) {
        return false;
    }
    route_.visible = visible_value;
    return true;
}

template<typename RouteT, typename PassHintT>
[[nodiscard]] constexpr bool SetVisualRuntimeRoutePassHint(RouteT& route_,
                                                           PassHintT pass_hint_) noexcept {
    if (route_.pass_hint == pass_hint_) {
        return false;
    }
    route_.pass_hint = pass_hint_;
    return true;
}

template<typename RouteT>
[[nodiscard]] constexpr bool SetVisualRuntimeRouteAuthoringVisualResourceId(
    RouteT& route_,
    std::uint32_t authoring_visual_resource_id_) noexcept {
    if (route_.authoring_visual_resource_id == authoring_visual_resource_id_) {
        return false;
    }
    route_.authoring_visual_resource_id = authoring_visual_resource_id_;
    return true;
}

template<typename RouteT>
[[nodiscard]] constexpr bool SetVisualRuntimeRouteBatchTag(RouteT& route_,
                                                           std::uint32_t batch_tag_) noexcept {
    if (route_.batch_tag == batch_tag_) {
        return false;
    }
    route_.batch_tag = batch_tag_;
    return true;
}

template<typename RouteT>
[[nodiscard]] constexpr bool SetVisualRuntimeRouteUserData(RouteT& route_,
                                                           std::uint32_t user_data_) noexcept {
    if (route_.user_data == user_data_) {
        return false;
    }
    route_.user_data = user_data_;
    return true;
}

template<typename RouteT>
[[nodiscard]] constexpr bool SetVisualRuntimeRouteAppearanceHandle(
    RouteT& route_,
    AppearanceHandle appearance_handle_) noexcept {
    if (!HasAppearanceHandleChanged(route_, appearance_handle_)) {
        return false;
    }
    route_.appearance_handle = appearance_handle_;
    return true;
}

template<typename RouteT>
[[nodiscard]] constexpr bool SetVisualRuntimeRouteDepthBin(RouteT& route_,
                                                           std::uint16_t depth_bin_) noexcept {
    if (route_.depth_bin == depth_bin_) {
        return false;
    }
    route_.depth_bin = depth_bin_;
    return true;
}

struct VisualRuntimeRouteLinkMutation final {
    bool route_changed = false;
    bool handle_changed = false;
};

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
    std::uint32_t linked_visual_resource_id_) noexcept {
    return HasAppearanceHandleChanged(route_, appearance_handle_) ||
           route_.appearance_pipeline_bucket != pipeline_bucket_ ||
           route_.linked_visual_resource_id != linked_visual_resource_id_;
}

template<typename RouteT>
[[nodiscard]] constexpr bool IsAppearanceRuntimeRouteClear(const RouteT& route_) noexcept {
    return !HasLinkedAppearanceHandle(route_) &&
           route_.appearance_pipeline_bucket == 0U &&
           route_.linked_visual_resource_id == 0U;
}

template<typename RouteT>
constexpr void StoreAppearanceRuntimeRoute(RouteT& route_,
                                           AppearanceHandle appearance_handle_,
                                           std::uint32_t pipeline_bucket_,
                                           std::uint32_t linked_visual_resource_id_) noexcept {
    route_.appearance_handle = appearance_handle_;
    route_.appearance_pipeline_bucket = pipeline_bucket_;
    route_.linked_visual_resource_id = linked_visual_resource_id_;
}

template<typename RouteT>
constexpr void ClearAppearanceRuntimeRoute(RouteT& route_) noexcept {
    route_.appearance_handle = invalid_appearance_handle;
    route_.appearance_pipeline_bucket = 0U;
    route_.linked_visual_resource_id = 0U;
}

template<typename RouteT>
[[nodiscard]] constexpr VisualRuntimeRouteLinkMutation UpdateVisualRuntimeRouteLink(
    RouteT& route_,
    AppearanceHandle appearance_handle_,
    std::uint64_t appearance_pipeline_key_,
    std::uint64_t appearance_resource_key_) noexcept {
    const std::uint32_t pipeline_bucket = static_cast<std::uint32_t>(appearance_pipeline_key_);
    const std::uint32_t linked_visual_resource_id =
        static_cast<std::uint32_t>(appearance_resource_key_);
    const bool route_changed = HasAppearanceRuntimeRouteChanged(route_,
                                                                appearance_handle_,
                                                                pipeline_bucket,
                                                                linked_visual_resource_id);
    if (!route_changed) {
        return {};
    }

    const bool handle_changed = HasAppearanceHandleChanged(route_, appearance_handle_);
    StoreAppearanceRuntimeRoute(route_,
                                appearance_handle_,
                                pipeline_bucket,
                                linked_visual_resource_id);
    return {
        .route_changed = true,
        .handle_changed = handle_changed,
    };
}

template<typename RuntimeT>
[[nodiscard]] constexpr bool WriteAppearanceRuntimeBridgeState(
    RuntimeT& runtime_,
    const AppearanceRuntimeBridge2D& bridge_) noexcept {
    if (HasSameAppearanceRuntimeBridge2D(runtime_, bridge_)) {
        return false;
    }
    StoreAppearanceRuntimeBridge2D(runtime_, bridge_);
    return true;
}

template<typename RuntimeT>
[[nodiscard]] constexpr bool WriteAppearanceRuntimeBridgeState(
    RuntimeT& runtime_,
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    if (HasSameAppearanceRuntimeBridge3D(runtime_, bridge_)) {
        return false;
    }
    StoreAppearanceRuntimeBridge3D(runtime_, bridge_);
    return true;
}

template<typename RuntimeT>
[[nodiscard]] constexpr bool WriteAppearanceRuntimeBridgeState(
    RuntimeT& runtime_,
    const AppearanceStyle2D* appearance_style_) noexcept {
    return WriteAppearanceRuntimeBridgeState(runtime_,
                                            MakeAppearanceRuntimeBridge2D(appearance_style_));
}

template<typename RuntimeT>
[[nodiscard]] constexpr bool WriteAppearanceRuntimeBridgeState(
    RuntimeT& runtime_,
    const AppearanceStyle3D* appearance_style_) noexcept {
    return WriteAppearanceRuntimeBridgeState(runtime_,
                                            MakeAppearanceRuntimeBridge3D(appearance_style_));
}

} // namespace vr::ecs
