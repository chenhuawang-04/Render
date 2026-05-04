#pragma once

#include "vr/ecs/concept/dimension.hpp"
#include "vr/render/render_view.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::render {

enum class RenderScenePacketKind : std::uint8_t {
    world = 0U,
    ui = 1U,
    mixed = 2U,
    custom = 255U,
};

enum RenderScenePacketFlags : std::uint32_t {
    render_scene_packet_none_flag = 0U,
    render_scene_packet_allow_postprocess_flag = 1U << 0U,
    render_scene_packet_allow_shadow_flag = 1U << 1U,
    render_scene_packet_allow_overlay_flag = 1U << 2U,
};

template<ecs::DimensionTag DimensionT>
struct RenderScenePacket final {
    using ViewType = RenderView<DimensionT>;

    RenderScenePacketKind kind = RenderScenePacketKind::world;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
    std::uint32_t active_view_index = 0U;
    std::uint32_t flags = render_scene_packet_allow_postprocess_flag |
                          render_scene_packet_allow_shadow_flag |
                          render_scene_packet_allow_overlay_flag;
    const ViewType* views = nullptr;
    std::uint32_t view_count = 0U;
    std::uint32_t render_layer_mask = 0xFFFF'FFFFU;
    std::uint32_t debug_flags = render_view_debug_none_flag;
    RenderPostProcessPolicy postprocess_policy = RenderPostProcessPolicy::inherit;
    std::uint8_t reserved2 = 0U;
    std::uint16_t reserved3 = 0U;
    std::uint64_t submission_id = 0U;
    std::uint64_t signature = 0U;

    [[nodiscard]] const ViewType* ActiveView() const noexcept {
        if (views == nullptr || view_count == 0U || active_view_index >= view_count) {
            return nullptr;
        }
        return views + active_view_index;
    }

    [[nodiscard]] const ViewType* ViewAt(std::uint32_t view_index_) const noexcept {
        if (views == nullptr || view_index_ >= view_count) {
            return nullptr;
        }
        return views + view_index_;
    }
};

using RenderScenePacket2D = RenderScenePacket<ecs::Dim2>;
using RenderScenePacket3D = RenderScenePacket<ecs::Dim3>;

static constexpr std::uint32_t invalid_scene_view_index = 0xFFFF'FFFFU;

template<ecs::DimensionTag DimensionT>
struct ResolvedSceneViewSelection final {
    const RenderView<DimensionT>* active_view = nullptr;
    const RenderView<DimensionT>* scene_view = nullptr;
    const RenderView<DimensionT>* overlay_view = nullptr;
    std::uint32_t active_view_index = invalid_scene_view_index;
    std::uint32_t scene_view_index = invalid_scene_view_index;
    std::uint32_t overlay_view_index = invalid_scene_view_index;
};

namespace detail {

template<ecs::DimensionTag DimensionT>
[[nodiscard]] inline std::uint64_t ComposeRenderScenePacketSignature(
    const RenderScenePacket<DimensionT>& packet_) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.kind));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.active_view_index));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.flags));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.view_count));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.render_layer_mask));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.debug_flags));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.postprocess_policy));
    RenderViewHashCombine(hash, packet_.submission_id);
    for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
        const RenderView<DimensionT>& view = packet_.views[view_index];
        RenderViewHashCombine(hash, view.signature);
    }
    return hash;
}

} // namespace detail

[[nodiscard]] constexpr bool IsSceneSubmissionViewKind(RenderViewKind kind_) noexcept {
    switch (kind_) {
    case RenderViewKind::world:
    case RenderViewKind::shadow:
    case RenderViewKind::reflection:
    case RenderViewKind::custom:
        return true;
    case RenderViewKind::ui:
    default:
        break;
    }
    return false;
}

[[nodiscard]] constexpr bool IsOverlaySubmissionViewKind(RenderViewKind kind_) noexcept {
    switch (kind_) {
    case RenderViewKind::ui:
    case RenderViewKind::custom:
        return true;
    case RenderViewKind::world:
    case RenderViewKind::shadow:
    case RenderViewKind::reflection:
    default:
        break;
    }
    return false;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] RenderScenePacket<DimensionT> MakeSingleViewScenePacket(
    const RenderView<DimensionT>& view_,
    std::uint64_t submission_id_ = 0U,
    RenderScenePacketKind kind_ = RenderScenePacketKind::world) noexcept {
    RenderScenePacket<DimensionT> packet{};
    packet.kind = kind_;
    packet.views = &view_;
    packet.view_count = 1U;
    packet.active_view_index = 0U;
    packet.submission_id = submission_id_;
    packet.render_layer_mask = view_.layer_mask;
    packet.signature = detail::ComposeRenderScenePacketSignature(packet);
    return packet;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] RenderScenePacket<DimensionT> MakeScenePacketFromViewRange(
    const RenderView<DimensionT>* views_,
    std::uint32_t view_count_,
    std::uint32_t active_view_index_ = 0U,
    std::uint64_t submission_id_ = 0U,
    RenderScenePacketKind kind_ = RenderScenePacketKind::world) noexcept {
    RenderScenePacket<DimensionT> packet{};
    packet.kind = kind_;
    packet.views = views_;
    packet.view_count = view_count_;
    packet.active_view_index = active_view_index_;
    packet.submission_id = submission_id_;
    packet.render_layer_mask = 0U;
    for (std::uint32_t view_index = 0U; view_index < view_count_; ++view_index) {
        if (const RenderView<DimensionT>* view = packet.ViewAt(view_index);
            view != nullptr) {
            packet.render_layer_mask |= view->layer_mask;
        }
    }
    packet.signature = detail::ComposeRenderScenePacketSignature(packet);
    return packet;
}

template<ecs::DimensionTag DimensionT>
void RefreshRenderScenePacketSignature(RenderScenePacket<DimensionT>& packet_) noexcept {
    packet_.signature = detail::ComposeRenderScenePacketSignature(packet_);
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr ResolvedSceneViewSelection<DimensionT> ResolveSceneViewSelection(
    const RenderScenePacket<DimensionT>& packet_) noexcept {
    ResolvedSceneViewSelection<DimensionT> selection{};
    selection.active_view = packet_.ActiveView();
    selection.active_view_index =
        (selection.active_view != nullptr) ? packet_.active_view_index : invalid_scene_view_index;

    if (selection.active_view != nullptr &&
        IsSceneSubmissionViewKind(selection.active_view->kind)) {
        selection.scene_view = selection.active_view;
        selection.scene_view_index = selection.active_view_index;
    }

    for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
        const RenderView<DimensionT>* view = packet_.ViewAt(view_index);
        if (view == nullptr) {
            continue;
        }

        if (selection.scene_view == nullptr &&
            IsSceneSubmissionViewKind(view->kind)) {
            selection.scene_view = view;
            selection.scene_view_index = view_index;
        }

        if (selection.overlay_view == nullptr &&
            view->kind == RenderViewKind::ui) {
            selection.overlay_view = view;
            selection.overlay_view_index = view_index;
        }
    }

    if (selection.overlay_view == nullptr &&
        selection.active_view != nullptr &&
        IsOverlaySubmissionViewKind(selection.active_view->kind)) {
        selection.overlay_view = selection.active_view;
        selection.overlay_view_index = selection.active_view_index;
    }

    if (selection.overlay_view == nullptr) {
        for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
            const RenderView<DimensionT>* view = packet_.ViewAt(view_index);
            if (view != nullptr &&
                IsOverlaySubmissionViewKind(view->kind)) {
                selection.overlay_view = view;
                selection.overlay_view_index = view_index;
                break;
            }
        }
    }

    return selection;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr std::uint32_t CountSceneViews(
    const RenderScenePacket<DimensionT>& packet_,
    RenderViewKind kind_) noexcept {
    std::uint32_t count = 0U;
    for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
        const RenderView<DimensionT>* view = packet_.ViewAt(view_index);
        if (view != nullptr && view->kind == kind_) {
            ++count;
        }
    }
    return count;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr const RenderView<DimensionT>* FindSceneView(
    const RenderScenePacket<DimensionT>& packet_,
    RenderViewKind kind_,
    std::uint32_t ordinal_ = 0U) noexcept {
    std::uint32_t match_index = 0U;
    for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
        const RenderView<DimensionT>* view = packet_.ViewAt(view_index);
        if (view == nullptr || view->kind != kind_) {
            continue;
        }
        if (match_index == ordinal_) {
            return view;
        }
        ++match_index;
    }
    return nullptr;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr bool SetActiveSceneViewIndex(
    RenderScenePacket<DimensionT>& packet_,
    std::uint32_t active_view_index_) noexcept {
    if (packet_.views == nullptr || active_view_index_ >= packet_.view_count) {
        return false;
    }
    if (packet_.active_view_index == active_view_index_) {
        return true;
    }
    packet_.active_view_index = active_view_index_;
    RefreshRenderScenePacketSignature(packet_);
    return true;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr bool SetActiveSceneViewByKind(
    RenderScenePacket<DimensionT>& packet_,
    RenderViewKind kind_,
    std::uint32_t ordinal_ = 0U) noexcept {
    std::uint32_t match_index = 0U;
    for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
        const RenderView<DimensionT>* view = packet_.ViewAt(view_index);
        if (view == nullptr || view->kind != kind_) {
            continue;
        }
        if (match_index == ordinal_) {
            return SetActiveSceneViewIndex(packet_, view_index);
        }
        ++match_index;
    }
    return false;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr std::uint32_t ResolveSceneLayerMask(
    const RenderScenePacket<DimensionT>& packet_) noexcept {
    return ResolveSceneLayerMaskForView(packet_, packet_.ActiveView());
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr std::uint32_t ResolveSceneLayerMaskForView(
    const RenderScenePacket<DimensionT>& packet_,
    const RenderView<DimensionT>* view_) noexcept {
    const std::uint32_t view_layer_mask =
        (view_ != nullptr) ? view_->layer_mask : 0xFFFF'FFFFU;
    return packet_.render_layer_mask & view_layer_mask;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr std::uint32_t ResolveSceneDebugFlagsForView(
    const RenderScenePacket<DimensionT>& packet_,
    const RenderView<DimensionT>* view_) noexcept {
    const std::uint32_t view_debug_flags =
        (view_ != nullptr) ? view_->debug_flags : render_view_debug_none_flag;
    return packet_.debug_flags | view_debug_flags;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr std::uint32_t ResolveSceneDebugFlags(
    const RenderScenePacket<DimensionT>& packet_) noexcept {
    return ResolveSceneDebugFlagsForView(packet_, packet_.ActiveView());
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr bool ResolveSceneShadowEnabledForView(
    const RenderScenePacket<DimensionT>& packet_,
    const RenderView<DimensionT>* view_) noexcept {
    return (packet_.flags & render_scene_packet_allow_shadow_flag) != 0U &&
           view_ != nullptr &&
           HasRenderViewFlag(view_->flags, render_view_shadow_enabled_flag);
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr bool ResolveSceneShadowEnabled(
    const RenderScenePacket<DimensionT>& packet_) noexcept {
    return ResolveSceneShadowEnabledForView(packet_, packet_.ActiveView());
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr bool ResolveSceneOverlayEnabledForView(
    const RenderScenePacket<DimensionT>& packet_,
    const RenderView<DimensionT>* view_) noexcept {
    return (packet_.flags & render_scene_packet_allow_overlay_flag) != 0U &&
           view_ != nullptr &&
           HasRenderViewFlag(view_->flags, render_view_overlay_enabled_flag);
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr bool ResolveSceneOverlayEnabled(
    const RenderScenePacket<DimensionT>& packet_) noexcept {
    return ResolveSceneOverlayEnabledForView(packet_, packet_.ActiveView());
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr bool ResolveScenePostProcessEnabledForView(
    const RenderScenePacket<DimensionT>& packet_,
    const RenderView<DimensionT>* view_) noexcept {
    if ((packet_.flags & render_scene_packet_allow_postprocess_flag) == 0U || view_ == nullptr) {
        return false;
    }

    const RenderPostProcessPolicy effective_policy =
        (packet_.postprocess_policy != RenderPostProcessPolicy::inherit)
            ? packet_.postprocess_policy
            : view_->postprocess_policy;
    if (effective_policy == RenderPostProcessPolicy::enabled) {
        return true;
    }
    if (effective_policy == RenderPostProcessPolicy::disabled) {
        return false;
    }
    return HasRenderViewFlag(view_->flags, render_view_postprocess_enabled_flag);
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr bool ResolveScenePostProcessEnabled(
    const RenderScenePacket<DimensionT>& packet_) noexcept {
    return ResolveScenePostProcessEnabledForView(packet_, packet_.ActiveView());
}

static_assert(std::is_standard_layout_v<RenderScenePacket2D>);
static_assert(std::is_standard_layout_v<RenderScenePacket3D>);

} // namespace vr::render
