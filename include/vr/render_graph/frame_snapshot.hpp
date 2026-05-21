#pragma once

#include "vr/render/scene_submission.hpp"
#include "vr/render_graph/render_graph_types.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace vr::render_graph {

namespace detail {

[[nodiscard]] inline std::uint32_t FrameSnapshotFloatBits(const float value_) noexcept {
    std::uint32_t out = 0U;
    std::memcpy(&out, &value_, sizeof(out));
    return out;
}

inline void FrameSnapshotHashCombine(std::uint64_t& hash_,
                                     const std::uint64_t value_) noexcept {
    hash_ ^= value_;
    hash_ *= 1099511628211ULL;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] Extent3D MakeViewExtent(const render::RenderView<DimensionT>& view_) noexcept {
    if (view_.scissor.width != 0U && view_.scissor.height != 0U) {
        return Extent3D{
            .width = view_.scissor.width,
            .height = view_.scissor.height,
            .depth = 1U,
        };
    }

    const float width = (view_.viewport.width > 0.0F) ? view_.viewport.width : 1.0F;
    const float height = (view_.viewport.height > 0.0F) ? view_.viewport.height : 1.0F;
    return Extent3D{
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .depth = 1U,
    };
}

[[nodiscard]] constexpr bool HasValidExtent(const Extent3D& extent_) noexcept {
    return extent_.width != 0U && extent_.height != 0U && extent_.depth != 0U;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] Extent3D ResolveReferenceExtent(const render::RenderScenePacket<DimensionT>& packet_,
                                              const Extent3D& fallback_extent_) noexcept {
    if (HasValidExtent(fallback_extent_)) {
        return fallback_extent_;
    }

    if (const auto* active_view = packet_.ActiveView(); active_view != nullptr) {
        return MakeViewExtent(*active_view);
    }

    for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
        if (const auto* view = packet_.ViewAt(view_index); view != nullptr) {
            return MakeViewExtent(*view);
        }
    }

    return Extent3D{};
}

} // namespace detail

struct ResolvedFrameViewSelection final {
    std::uint32_t active_view_index = render::invalid_scene_view_index;
    std::uint32_t scene_view_index = render::invalid_scene_view_index;
    std::uint32_t overlay_view_index = render::invalid_scene_view_index;

    [[nodiscard]] constexpr bool HasActiveView() const noexcept {
        return active_view_index != render::invalid_scene_view_index;
    }

    [[nodiscard]] constexpr bool HasSceneView() const noexcept {
        return scene_view_index != render::invalid_scene_view_index;
    }

    [[nodiscard]] constexpr bool HasOverlayView() const noexcept {
        return overlay_view_index != render::invalid_scene_view_index;
    }
};

struct FrameViewCameraData final {
    ecs::CameraRuntimeData runtime{};
};

template<ecs::DimensionTag DimensionT>
struct FrameViewSnapshot final {
    render::RenderViewKind kind = render::RenderViewKind::world;
    std::uint8_t has_camera = 0U;
    std::uint8_t has_camera_transform = 0U;
    std::uint16_t reserved0 = 0U;
    std::uint32_t view_index = 0U;
    std::uint32_t flags = render::render_view_none_flag;
    std::uint32_t culling_mask = 0xFFFF'FFFFU;
    std::uint32_t layer_mask = 0xFFFF'FFFFU;
    std::uint32_t debug_flags = render::render_view_debug_none_flag;
    render::RenderPostProcessPolicy postprocess_policy = render::RenderPostProcessPolicy::inherit;
    std::uint8_t reserved1 = 0U;
    std::uint16_t reserved2 = 0U;
    render::RenderViewViewport viewport{};
    render::RenderViewScissor scissor{};
    render::RenderViewTargetRefs targets{};
    render::RenderViewBackgroundOverride<DimensionT> background_override{};
    FrameViewCameraData camera{};
    std::uint32_t camera_transform_world_revision = 0U;
    std::uint32_t reserved3 = 0U;
    std::uint64_t signature = 0U;
};

template<ecs::DimensionTag DimensionT>
struct FrameSnapshot final {
    using ViewType = FrameViewSnapshot<DimensionT>;

    std::uint64_t frame_index = 0U;
    Extent3D reference_extent{};
    render::RenderScenePacketKind kind = render::RenderScenePacketKind::world;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
    ResolvedFrameViewSelection selection{};
    std::uint32_t flags = render::render_scene_packet_none_flag;
    std::uint32_t render_layer_mask = 0xFFFF'FFFFU;
    std::uint32_t debug_flags = render::render_view_debug_none_flag;
    render::RenderPostProcessPolicy postprocess_policy = render::RenderPostProcessPolicy::inherit;
    std::uint8_t reserved2 = 0U;
    std::uint16_t reserved3 = 0U;
    render::RenderScenePacketExtra<DimensionT> extra{};
    std::uint64_t submission_id = 0U;
    std::uint64_t signature = 0U;
    std::vector<ViewType> views{};

    [[nodiscard]] const ViewType* ActiveView() const noexcept {
        return ViewAt(selection.active_view_index);
    }

    [[nodiscard]] const ViewType* SceneView() const noexcept {
        return ViewAt(selection.scene_view_index);
    }

    [[nodiscard]] const ViewType* OverlayView() const noexcept {
        return ViewAt(selection.overlay_view_index);
    }

    [[nodiscard]] const ViewType* ViewAt(const std::uint32_t view_index_) const noexcept {
        if (view_index_ == render::invalid_scene_view_index || view_index_ >= views.size()) {
            return nullptr;
        }
        return views.data() + view_index_;
    }

    [[nodiscard]] bool HasSceneView() const noexcept {
        return selection.HasSceneView();
    }

    [[nodiscard]] bool HasOverlayView() const noexcept {
        return selection.HasOverlayView();
    }

    [[nodiscard]] std::size_t ViewCount() const noexcept {
        return views.size();
    }
};

using FrameViewSnapshot2D = FrameViewSnapshot<ecs::Dim2>;
using FrameViewSnapshot3D = FrameViewSnapshot<ecs::Dim3>;
using FrameSnapshot2D = FrameSnapshot<ecs::Dim2>;
using FrameSnapshot3D = FrameSnapshot<ecs::Dim3>;

static_assert(std::is_standard_layout_v<ResolvedFrameViewSelection>);
static_assert(std::is_standard_layout_v<FrameViewCameraData>);

template<ecs::DimensionTag DimensionT>
[[nodiscard]] inline std::uint64_t ComposeFrameViewSnapshotSignature(
    const FrameViewSnapshot<DimensionT>& view_) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.kind));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.has_camera));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.has_camera_transform));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.view_index));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.flags));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.culling_mask));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.layer_mask));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.debug_flags));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.postprocess_policy));
    detail::FrameSnapshotHashCombine(
        hash, static_cast<std::uint64_t>(detail::FrameSnapshotFloatBits(view_.viewport.x)));
    detail::FrameSnapshotHashCombine(
        hash, static_cast<std::uint64_t>(detail::FrameSnapshotFloatBits(view_.viewport.y)));
    detail::FrameSnapshotHashCombine(
        hash, static_cast<std::uint64_t>(detail::FrameSnapshotFloatBits(view_.viewport.width)));
    detail::FrameSnapshotHashCombine(
        hash, static_cast<std::uint64_t>(detail::FrameSnapshotFloatBits(view_.viewport.height)));
    detail::FrameSnapshotHashCombine(
        hash, static_cast<std::uint64_t>(detail::FrameSnapshotFloatBits(view_.viewport.min_depth)));
    detail::FrameSnapshotHashCombine(
        hash, static_cast<std::uint64_t>(detail::FrameSnapshotFloatBits(view_.viewport.max_depth)));
    detail::FrameSnapshotHashCombine(
        hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(view_.scissor.x)));
    detail::FrameSnapshotHashCombine(
        hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(view_.scissor.y)));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.scissor.width));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.scissor.height));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.targets.color_target.index));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.targets.color_target.generation));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.targets.depth_target.index));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.targets.depth_target.generation));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.background_override.mode));
    if constexpr (std::is_same_v<DimensionT, ecs::Dim2>) {
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(view_.background_override.state.revision));
    } else {
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(view_.background_override.state.revision));
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(view_.background_override.gpu.index));
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(view_.background_override.gpu.generation));
    }
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.camera.runtime.culling_mask));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(view_.camera.runtime.revision));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(view_.camera_transform_world_revision));
    return hash;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] inline std::uint64_t ComposeFrameSnapshotSignature(
    const FrameSnapshot<DimensionT>& snapshot_) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.reference_extent.width));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.reference_extent.height));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.reference_extent.depth));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.kind));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.selection.active_view_index));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.selection.scene_view_index));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.selection.overlay_view_index));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.flags));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.render_layer_mask));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.debug_flags));
    detail::FrameSnapshotHashCombine(hash, static_cast<std::uint64_t>(snapshot_.postprocess_policy));
    if constexpr (std::is_same_v<DimensionT, ecs::Dim2>) {
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(snapshot_.extra.background.revision));
    } else {
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(snapshot_.extra.environment.revision));
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(snapshot_.extra.environment_gpu.index));
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(snapshot_.extra.environment_gpu.generation));
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(snapshot_.extra.ibl_environment_id));
    }
    for (const auto& view_ : snapshot_.views) {
        detail::FrameSnapshotHashCombine(hash, view_.signature);
    }
    return hash;
}

template<ecs::DimensionTag DimensionT>
void RefreshFrameViewSnapshotSignature(FrameViewSnapshot<DimensionT>& view_) noexcept {
    view_.signature = ComposeFrameViewSnapshotSignature(view_);
}

template<ecs::DimensionTag DimensionT>
void RefreshFrameSnapshotSignature(FrameSnapshot<DimensionT>& snapshot_) noexcept {
    for (auto& view_ : snapshot_.views) {
        RefreshFrameViewSnapshotSignature(view_);
    }
    snapshot_.signature = ComposeFrameSnapshotSignature(snapshot_);
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] FrameViewSnapshot<DimensionT> MakeFrameViewSnapshot(
    const render::RenderView<DimensionT>& view_) noexcept {
    FrameViewSnapshot<DimensionT> snapshot{};
    snapshot.kind = view_.kind;
    snapshot.has_camera = (view_.camera != nullptr) ? 1U : 0U;
    snapshot.has_camera_transform = (view_.camera_transform != nullptr) ? 1U : 0U;
    snapshot.view_index = view_.view_index;
    snapshot.flags = view_.flags;
    snapshot.culling_mask = view_.culling_mask;
    snapshot.layer_mask = view_.layer_mask;
    snapshot.debug_flags = view_.debug_flags;
    snapshot.postprocess_policy = view_.postprocess_policy;
    snapshot.viewport = view_.viewport;
    snapshot.scissor = view_.scissor;
    snapshot.targets = view_.targets;
    snapshot.background_override = view_.background_override;
    if (view_.camera != nullptr) {
        snapshot.camera.runtime = view_.camera->runtime;
    }
    if (view_.camera_transform != nullptr) {
        snapshot.camera_transform_world_revision = view_.camera_transform->runtime.world_revision;
    }
    RefreshFrameViewSnapshotSignature(snapshot);
    return snapshot;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] FrameSnapshot<DimensionT> MakeFrameSnapshot(
    const render::RenderScenePacket<DimensionT>& packet_,
    const std::uint64_t frame_index_ = 0U,
    const Extent3D& reference_extent_ = {}) {
    FrameSnapshot<DimensionT> snapshot{};
    snapshot.frame_index = frame_index_;
    snapshot.reference_extent = detail::ResolveReferenceExtent(packet_, reference_extent_);
    snapshot.kind = packet_.kind;
    snapshot.flags = packet_.flags;
    snapshot.render_layer_mask = packet_.render_layer_mask;
    snapshot.debug_flags = packet_.debug_flags;
    snapshot.postprocess_policy = packet_.postprocess_policy;
    snapshot.extra = packet_.extra;
    snapshot.submission_id = packet_.submission_id;

    const auto resolved_selection = render::ResolveSceneViewSelection(packet_);
    snapshot.selection = ResolvedFrameViewSelection{
        .active_view_index = resolved_selection.active_view_index,
        .scene_view_index = resolved_selection.scene_view_index,
        .overlay_view_index = resolved_selection.overlay_view_index,
    };

    snapshot.views.reserve(packet_.view_count);
    for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
        if (const auto* view_ = packet_.ViewAt(view_index); view_ != nullptr) {
            snapshot.views.push_back(MakeFrameViewSnapshot(*view_));
        }
    }
    RefreshFrameSnapshotSignature(snapshot);
    return snapshot;
}

} // namespace vr::render_graph
