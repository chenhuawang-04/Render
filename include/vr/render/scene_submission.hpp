#pragma once

#include "vr/ecs/concept/dimension.hpp"
#include "vr/render/render_view.hpp"
#include "vr/runtime/runtime_ingress_ids.hpp"
#include "vr/scene/background/background_traits.hpp"

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

static constexpr std::uint32_t invalid_scene_view_index = 0xFFFF'FFFFU;

template<ecs::DimensionTag DimensionT>
struct SceneSubmissionPayload;

template<>
struct SceneSubmissionPayload<ecs::Dim2> final {
    scene::Background2DRenderState background{};
};

template<>
struct SceneSubmissionPayload<ecs::Dim3> final {
    scene::SkyEnvironmentRenderState environment{};
    scene::SkyEnvironmentGpuHandle environment_gpu{};
    IblEnvironmentId ibl_environment_id{};
};

template<ecs::DimensionTag DimensionT>
using RenderScenePacketExtra = SceneSubmissionPayload<DimensionT>;

struct SceneSubmissionMetadata final {
    RenderScenePacketKind kind = RenderScenePacketKind::world;
    std::uint32_t flags = render_scene_packet_allow_postprocess_flag |
                          render_scene_packet_allow_shadow_flag |
                          render_scene_packet_allow_overlay_flag;
    std::uint32_t render_layer_mask = 0xFFFF'FFFFU;
    std::uint32_t debug_flags = render_view_debug_none_flag;
    RenderPostProcessPolicy postprocess_policy = RenderPostProcessPolicy::inherit;
    SceneSubmissionId submission_id{};
};

struct SceneSubmissionViewSelection final {
    std::uint32_t active_view_index = invalid_scene_view_index;
    std::uint32_t scene_view_index = invalid_scene_view_index;
    std::uint32_t overlay_view_index = invalid_scene_view_index;

    [[nodiscard]] constexpr bool HasActiveView() const noexcept {
        return active_view_index != invalid_scene_view_index;
    }

    [[nodiscard]] constexpr bool HasSceneView() const noexcept {
        return scene_view_index != invalid_scene_view_index;
    }

    [[nodiscard]] constexpr bool HasOverlayView() const noexcept {
        return overlay_view_index != invalid_scene_view_index;
    }
};

template<ecs::DimensionTag DimensionT>
struct SceneSubmissionSchema final {
    SceneSubmissionMetadata metadata{};
    SceneSubmissionViewSelection selection{};
    SceneSubmissionPayload<DimensionT> payload{};
};

template<ecs::DimensionTag DimensionT>
struct RenderScenePacket final {
    using ViewType = RenderView<DimensionT>;
    using PayloadType = SceneSubmissionPayload<DimensionT>;

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
    PayloadType extra{};
    SceneSubmissionId submission_id{};
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

    [[nodiscard]] constexpr SceneSubmissionMetadata Metadata() const noexcept {
        return SceneSubmissionMetadata{
            .kind = kind,
            .flags = flags,
            .render_layer_mask = render_layer_mask,
            .debug_flags = debug_flags,
            .postprocess_policy = postprocess_policy,
            .submission_id = submission_id,
        };
    }

    [[nodiscard]] constexpr const PayloadType& Payload() const noexcept {
        return extra;
    }

    [[nodiscard]] constexpr PayloadType& Payload() noexcept {
        return extra;
    }
};

using RenderScenePacket2D = RenderScenePacket<ecs::Dim2>;
using RenderScenePacket3D = RenderScenePacket<ecs::Dim3>;

static_assert(std::is_standard_layout_v<SceneSubmissionPayload<ecs::Dim2>>);
static_assert(std::is_standard_layout_v<SceneSubmissionPayload<ecs::Dim3>>);
static_assert(std::is_standard_layout_v<SceneSubmissionMetadata>);
static_assert(std::is_standard_layout_v<SceneSubmissionViewSelection>);
static_assert(std::is_standard_layout_v<SceneSubmissionSchema<ecs::Dim2>>);
static_assert(std::is_standard_layout_v<SceneSubmissionSchema<ecs::Dim3>>);

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
    if constexpr (std::is_same_v<DimensionT, ecs::Dim2>) {
        RenderViewHashCombine(hash,
                              static_cast<std::uint64_t>(packet_.Payload().background.revision));
    } else {
        RenderViewHashCombine(hash,
                              static_cast<std::uint64_t>(packet_.Payload().environment.revision));
        RenderViewHashCombine(hash,
                              static_cast<std::uint64_t>(packet_.Payload().environment_gpu.index));
        RenderViewHashCombine(hash,
                              static_cast<std::uint64_t>(packet_.Payload().environment_gpu.generation));
        RenderViewHashCombine(hash, packet_.Payload().ibl_environment_id.value);
    }
    RenderViewHashCombine(hash, packet_.Metadata().submission_id.value);
    for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
        const RenderView<DimensionT>& view = packet_.views[view_index];
        RenderViewHashCombine(hash, view.signature);
    }
    return hash;
}

} // namespace detail

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr std::uint32_t ComputeSceneSubmissionViewLayerMask(
    const RenderView<DimensionT>* views_,
    const std::uint32_t view_count_) noexcept {
    std::uint32_t layer_mask = 0U;
    if (views_ == nullptr) {
        return layer_mask;
    }
    for (std::uint32_t view_index = 0U; view_index < view_count_; ++view_index) {
        layer_mask |= views_[view_index].layer_mask;
    }
    return layer_mask;
}

template<ecs::DimensionTag DimensionT>
constexpr void BindSceneSubmissionViews(RenderScenePacket<DimensionT>& packet_,
                                        const RenderView<DimensionT>* views_,
                                        const std::uint32_t view_count_,
                                        const std::uint32_t active_view_index_ = 0U) noexcept {
    packet_.views = views_;
    packet_.view_count = view_count_;
    packet_.active_view_index = active_view_index_;
}

template<ecs::DimensionTag DimensionT>
constexpr void ApplySceneSubmissionMetadata(RenderScenePacket<DimensionT>& packet_,
                                            const SceneSubmissionMetadata& metadata_) noexcept {
    packet_.kind = metadata_.kind;
    packet_.flags = metadata_.flags;
    packet_.render_layer_mask = metadata_.render_layer_mask;
    packet_.debug_flags = metadata_.debug_flags;
    packet_.postprocess_policy = metadata_.postprocess_policy;
    packet_.submission_id = metadata_.submission_id;
}

template<ecs::DimensionTag DimensionT>
constexpr void ApplySceneSubmissionPayload(RenderScenePacket<DimensionT>& packet_,
                                           const SceneSubmissionPayload<DimensionT>& payload_) noexcept {
    packet_.extra = payload_;
}

template<ecs::DimensionTag DimensionT>
constexpr void ApplySceneSubmissionSchema(RenderScenePacket<DimensionT>& packet_,
                                          const SceneSubmissionSchema<DimensionT>& schema_) noexcept {
    ApplySceneSubmissionMetadata(packet_, schema_.metadata);
    ApplySceneSubmissionPayload(packet_, schema_.payload);
    packet_.active_view_index =
        schema_.selection.HasActiveView() ? schema_.selection.active_view_index : 0U;
}

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
    const SceneSubmissionMetadata& metadata_) noexcept {
    RenderScenePacket<DimensionT> packet{};
    BindSceneSubmissionViews(packet, &view_, 1U, 0U);
    ApplySceneSubmissionMetadata(packet, metadata_);
    packet.signature = detail::ComposeRenderScenePacketSignature(packet);
    return packet;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] RenderScenePacket<DimensionT> MakeSingleViewScenePacket(
    const RenderView<DimensionT>& view_,
    SceneSubmissionId submission_id_ = {},
    RenderScenePacketKind kind_ = RenderScenePacketKind::world) noexcept {
    return MakeSingleViewScenePacket(
        view_,
        SceneSubmissionMetadata{
            .kind = kind_,
            .render_layer_mask = view_.layer_mask,
            .submission_id = submission_id_,
        });
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] RenderScenePacket<DimensionT> MakeScenePacketFromViewRange(
    const RenderView<DimensionT>* views_,
    std::uint32_t view_count_,
    std::uint32_t active_view_index_,
    const SceneSubmissionMetadata& metadata_) noexcept {
    RenderScenePacket<DimensionT> packet{};
    BindSceneSubmissionViews(packet, views_, view_count_, active_view_index_);
    ApplySceneSubmissionMetadata(packet, metadata_);
    packet.signature = detail::ComposeRenderScenePacketSignature(packet);
    return packet;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] RenderScenePacket<DimensionT> MakeScenePacketFromViewRange(
    const RenderView<DimensionT>* views_,
    std::uint32_t view_count_,
    std::uint32_t active_view_index_ = 0U,
    SceneSubmissionId submission_id_ = {},
    RenderScenePacketKind kind_ = RenderScenePacketKind::world) noexcept {
    return MakeScenePacketFromViewRange(
        views_,
        view_count_,
        active_view_index_,
        SceneSubmissionMetadata{
            .kind = kind_,
            .render_layer_mask = ComputeSceneSubmissionViewLayerMask(views_, view_count_),
            .submission_id = submission_id_,
        });
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
[[nodiscard]] constexpr SceneSubmissionViewSelection ResolveSceneSubmissionViewSelection(
    const RenderScenePacket<DimensionT>& packet_) noexcept {
    const ResolvedSceneViewSelection<DimensionT> resolved = ResolveSceneViewSelection(packet_);
    return SceneSubmissionViewSelection{
        .active_view_index = resolved.active_view_index,
        .scene_view_index = resolved.scene_view_index,
        .overlay_view_index = resolved.overlay_view_index,
    };
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] constexpr SceneSubmissionSchema<DimensionT> MakeSceneSubmissionSchema(
    const RenderScenePacket<DimensionT>& packet_) noexcept {
    return SceneSubmissionSchema<DimensionT>{
        .metadata = packet_.Metadata(),
        .selection = ResolveSceneSubmissionViewSelection(packet_),
        .payload = packet_.Payload(),
    };
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

