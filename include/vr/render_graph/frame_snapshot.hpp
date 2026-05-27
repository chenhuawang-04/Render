#pragma once

#include "vr/render_graph/frame_history_contract.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render_graph/render_graph_types.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
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

struct FrameViewCameraData final {
    ecs::CameraRuntimeData runtime{};
};

template<ecs::DimensionTag DimensionT>
struct FrameViewSchema final {
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
    render::RenderViewBackgroundOverride<DimensionT> background_override{};
    FrameViewCameraData camera{};
    std::uint32_t camera_transform_world_revision = 0U;
    std::uint32_t reserved3 = 0U;
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
    render::RenderViewBackgroundOverride<DimensionT> background_override{};
    FrameViewCameraData camera{};
    std::uint32_t camera_transform_world_revision = 0U;
    std::uint32_t reserved3 = 0U;
    std::uint64_t signature = 0U;
};

template<ecs::DimensionTag DimensionT>
struct FrameSnapshot final {
    using ViewType = FrameViewSnapshot<DimensionT>;
    using PayloadType = render::SceneSubmissionPayload<DimensionT>;

    std::uint64_t frame_index = 0U;
    Extent3D reference_extent{};
    render::RenderScenePacketKind kind = render::RenderScenePacketKind::world;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
    render::SceneSubmissionViewSelection selection{};
    std::uint32_t flags = render::render_scene_packet_none_flag;
    std::uint32_t render_layer_mask = 0xFFFF'FFFFU;
    std::uint32_t debug_flags = render::render_view_debug_none_flag;
    render::RenderPostProcessPolicy postprocess_policy = render::RenderPostProcessPolicy::inherit;
    std::uint8_t reserved2 = 0U;
    std::uint16_t reserved3 = 0U;
    PayloadType extra{};
    render::SceneSubmissionId submission_id{};
    FrameTemporalContract temporal{};
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

    [[nodiscard]] render::SceneSubmissionMetadata Metadata() const noexcept {
        return render::SceneSubmissionMetadata{
            .kind = kind,
            .flags = flags,
            .render_layer_mask = render_layer_mask,
            .debug_flags = debug_flags,
            .postprocess_policy = postprocess_policy,
            .submission_id = submission_id,
        };
    }

    [[nodiscard]] const PayloadType& Payload() const noexcept {
        return extra;
    }

    [[nodiscard]] PayloadType& Payload() noexcept {
        return extra;
    }
};

using FrameViewSchema2D = FrameViewSchema<ecs::Dim2>;
using FrameViewSchema3D = FrameViewSchema<ecs::Dim3>;
using FrameViewSnapshot2D = FrameViewSnapshot<ecs::Dim2>;
using FrameViewSnapshot3D = FrameViewSnapshot<ecs::Dim3>;

template<ecs::DimensionTag DimensionT>
struct FrameSnapshotSchema final {
    using ViewType = FrameViewSchema<DimensionT>;
    using SubmissionType = render::SceneSubmissionSchema<DimensionT>;
    using PayloadType = render::SceneSubmissionPayload<DimensionT>;

    std::uint64_t frame_index = 0U;
    Extent3D reference_extent{};
    SubmissionType submission{};
    FrameTemporalContract temporal{};
    std::vector<ViewType> views{};

    [[nodiscard]] const SubmissionType& Submission() const noexcept {
        return submission;
    }

    [[nodiscard]] SubmissionType& Submission() noexcept {
        return submission;
    }

    [[nodiscard]] render::SceneSubmissionMetadata Metadata() const noexcept {
        return submission.metadata;
    }

    [[nodiscard]] const PayloadType& Payload() const noexcept {
        return submission.payload;
    }

    [[nodiscard]] PayloadType& Payload() noexcept {
        return submission.payload;
    }

    [[nodiscard]] const render::SceneSubmissionViewSelection& Selection() const noexcept {
        return submission.selection;
    }

    [[nodiscard]] render::SceneSubmissionViewSelection& Selection() noexcept {
        return submission.selection;
    }

    [[nodiscard]] const ViewType* ActiveView() const noexcept {
        return ViewAt(submission.selection.active_view_index);
    }

    [[nodiscard]] const ViewType* SceneView() const noexcept {
        return ViewAt(submission.selection.scene_view_index);
    }

    [[nodiscard]] const ViewType* OverlayView() const noexcept {
        return ViewAt(submission.selection.overlay_view_index);
    }

    [[nodiscard]] const ViewType* ViewAt(const std::uint32_t view_index_) const noexcept {
        if (view_index_ == render::invalid_scene_view_index || view_index_ >= views.size()) {
            return nullptr;
        }
        return views.data() + view_index_;
    }

    [[nodiscard]] bool HasSceneView() const noexcept {
        return submission.selection.HasSceneView();
    }

    [[nodiscard]] bool HasOverlayView() const noexcept {
        return submission.selection.HasOverlayView();
    }

    [[nodiscard]] std::size_t ViewCount() const noexcept {
        return views.size();
    }
};

using FrameSnapshot2D = FrameSnapshot<ecs::Dim2>;
using FrameSnapshot3D = FrameSnapshot<ecs::Dim3>;
using FrameSnapshotSchema2D = FrameSnapshotSchema<ecs::Dim2>;
using FrameSnapshotSchema3D = FrameSnapshotSchema<ecs::Dim3>;

static_assert(std::is_standard_layout_v<FrameViewCameraData>);
static_assert(std::is_standard_layout_v<FrameViewSchema2D>);
static_assert(std::is_standard_layout_v<FrameViewSchema3D>);

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
            static_cast<std::uint64_t>(snapshot_.Payload().background.revision));
    } else {
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(snapshot_.Payload().environment.revision));
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(snapshot_.Payload().environment_gpu.index));
        detail::FrameSnapshotHashCombine(
            hash,
            static_cast<std::uint64_t>(snapshot_.Payload().environment_gpu.generation));
        detail::FrameSnapshotHashCombine(hash, snapshot_.Payload().ibl_environment_id.value);
    }
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.color.previous_available ? 1U : 0U));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.color.current_writable ? 1U : 0U));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.color.invalidation_reason));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.depth.previous_available ? 1U : 0U));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.depth.current_writable ? 1U : 0U));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.depth.invalidation_reason));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.motion.previous_available ? 1U : 0U));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.motion.current_writable ? 1U : 0U));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.motion.invalidation_reason));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.reprojection.current_available ? 1U : 0U));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.reprojection.previous_available ? 1U : 0U));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.reprojection.invalidation_reason));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.jitter.current_available ? 1U : 0U));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.jitter.previous_available ? 1U : 0U));
    detail::FrameSnapshotHashCombine(
        hash,
        static_cast<std::uint64_t>(snapshot_.temporal.jitter.invalidation_reason));
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
void ApplySceneSubmissionSchema(FrameSnapshot<DimensionT>& snapshot_,
                                const render::SceneSubmissionSchema<DimensionT>& schema_) noexcept {
    snapshot_.kind = schema_.metadata.kind;
    snapshot_.flags = schema_.metadata.flags;
    snapshot_.render_layer_mask = schema_.metadata.render_layer_mask;
    snapshot_.debug_flags = schema_.metadata.debug_flags;
    snapshot_.postprocess_policy = schema_.metadata.postprocess_policy;
    snapshot_.extra = schema_.payload;
    snapshot_.submission_id = schema_.metadata.submission_id;
    snapshot_.selection = schema_.selection;
}

template<ecs::DimensionTag DimensionT>
void ApplySceneSubmissionSchema(FrameSnapshotSchema<DimensionT>& schema_,
                                const render::SceneSubmissionSchema<DimensionT>& submission_) noexcept {
    schema_.submission = submission_;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] render::SceneSubmissionSchema<DimensionT> MakeSceneSubmissionSchema(
    const FrameSnapshotSchema<DimensionT>& schema_) noexcept {
    return schema_.Submission();
}

template<ecs::DimensionTag DimensionT>
void ApplyFrameViewSchema(FrameViewSnapshot<DimensionT>& snapshot_,
                          const FrameViewSchema<DimensionT>& schema_) noexcept {
    snapshot_.kind = schema_.kind;
    snapshot_.has_camera = schema_.has_camera;
    snapshot_.has_camera_transform = schema_.has_camera_transform;
    snapshot_.reserved0 = schema_.reserved0;
    snapshot_.view_index = schema_.view_index;
    snapshot_.flags = schema_.flags;
    snapshot_.culling_mask = schema_.culling_mask;
    snapshot_.layer_mask = schema_.layer_mask;
    snapshot_.debug_flags = schema_.debug_flags;
    snapshot_.postprocess_policy = schema_.postprocess_policy;
    snapshot_.reserved1 = schema_.reserved1;
    snapshot_.reserved2 = schema_.reserved2;
    snapshot_.viewport = schema_.viewport;
    snapshot_.scissor = schema_.scissor;
    snapshot_.background_override = schema_.background_override;
    snapshot_.camera = schema_.camera;
    snapshot_.camera_transform_world_revision = schema_.camera_transform_world_revision;
    snapshot_.reserved3 = schema_.reserved3;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] FrameViewSchema<DimensionT> MakeFrameViewSchema(
    const render::RenderView<DimensionT>& view_) noexcept {
    FrameViewSchema<DimensionT> schema{};
    schema.kind = view_.kind;
    schema.has_camera = (view_.camera != nullptr) ? 1U : 0U;
    schema.has_camera_transform = (view_.camera_transform != nullptr) ? 1U : 0U;
    schema.view_index = view_.view_index;
    schema.flags = view_.flags;
    schema.culling_mask = view_.culling_mask;
    schema.layer_mask = view_.layer_mask;
    schema.debug_flags = view_.debug_flags;
    schema.postprocess_policy = view_.postprocess_policy;
    schema.viewport = view_.viewport;
    schema.scissor = view_.scissor;
    schema.background_override = view_.background_override;
    if (view_.camera != nullptr) {
        schema.camera.runtime = view_.camera->runtime;
    }
    if (view_.camera_transform != nullptr) {
        schema.camera_transform_world_revision = view_.camera_transform->runtime.world_revision;
    }
    return schema;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] FrameViewSchema<DimensionT> MakeFrameViewSchema(
    const FrameViewSnapshot<DimensionT>& snapshot_) noexcept {
    return FrameViewSchema<DimensionT>{
        .kind = snapshot_.kind,
        .has_camera = snapshot_.has_camera,
        .has_camera_transform = snapshot_.has_camera_transform,
        .reserved0 = snapshot_.reserved0,
        .view_index = snapshot_.view_index,
        .flags = snapshot_.flags,
        .culling_mask = snapshot_.culling_mask,
        .layer_mask = snapshot_.layer_mask,
        .debug_flags = snapshot_.debug_flags,
        .postprocess_policy = snapshot_.postprocess_policy,
        .reserved1 = snapshot_.reserved1,
        .reserved2 = snapshot_.reserved2,
        .viewport = snapshot_.viewport,
        .scissor = snapshot_.scissor,
        .background_override = snapshot_.background_override,
        .camera = snapshot_.camera,
        .camera_transform_world_revision = snapshot_.camera_transform_world_revision,
        .reserved3 = snapshot_.reserved3,
    };
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] FrameViewSnapshot<DimensionT> MakeFrameViewSnapshot(
    const FrameViewSchema<DimensionT>& schema_) noexcept {
    FrameViewSnapshot<DimensionT> snapshot{};
    ApplyFrameViewSchema(snapshot, schema_);
    RefreshFrameViewSnapshotSignature(snapshot);
    return snapshot;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] FrameViewSnapshot<DimensionT> MakeFrameViewSnapshot(
    const render::RenderView<DimensionT>& view_) noexcept {
    return MakeFrameViewSnapshot(MakeFrameViewSchema(view_));
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] FrameSnapshotSchema<DimensionT> MakeFrameSnapshotSchema(
    const render::RenderScenePacket<DimensionT>& packet_,
    const std::uint64_t frame_index_ = 0U,
    const Extent3D& reference_extent_ = {}) {
    FrameSnapshotSchema<DimensionT> schema{};
    schema.frame_index = frame_index_;
    schema.reference_extent = detail::ResolveReferenceExtent(packet_, reference_extent_);
    ApplySceneSubmissionSchema(schema, render::MakeSceneSubmissionSchema(packet_));
    schema.temporal = {};
    schema.views.reserve(packet_.view_count);
    for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
        if (const auto* view_ = packet_.ViewAt(view_index); view_ != nullptr) {
            schema.views.push_back(MakeFrameViewSchema(*view_));
        }
    }
    return schema;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] FrameSnapshotSchema<DimensionT> MakeFrameSnapshotSchema(
    const FrameSnapshot<DimensionT>& snapshot_) {
    return FrameSnapshotSchema<DimensionT>{
        .frame_index = snapshot_.frame_index,
        .reference_extent = snapshot_.reference_extent,
        .submission = MakeSceneSubmissionSchema(snapshot_),
        .temporal = snapshot_.temporal,
        .views = [&snapshot_]() {
            std::vector<FrameViewSchema<DimensionT>> views{};
            views.reserve(snapshot_.views.size());
            for (const auto& view_ : snapshot_.views) {
                views.push_back(MakeFrameViewSchema(view_));
            }
            return views;
        }(),
    };
}

template<ecs::DimensionTag DimensionT>
void ApplyFrameSnapshotSchema(FrameSnapshot<DimensionT>& snapshot_,
                              const FrameSnapshotSchema<DimensionT>& schema_) {
    snapshot_.frame_index = schema_.frame_index;
    snapshot_.reference_extent = schema_.reference_extent;
    ApplySceneSubmissionSchema(snapshot_, MakeSceneSubmissionSchema(schema_));
    snapshot_.temporal = schema_.temporal;
    snapshot_.views.clear();
    snapshot_.views.reserve(schema_.views.size());
    for (const auto& view_ : schema_.views) {
        snapshot_.views.push_back(MakeFrameViewSnapshot(view_));
    }
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] FrameSnapshot<DimensionT> MakeFrameSnapshot(
    FrameSnapshotSchema<DimensionT> schema_) {
    FrameSnapshot<DimensionT> snapshot{};
    ApplyFrameSnapshotSchema(snapshot, schema_);
    snapshot.signature = ComposeFrameSnapshotSignature(snapshot);
    return snapshot;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] FrameSnapshot<DimensionT> MakeFrameSnapshot(
    const render::RenderScenePacket<DimensionT>& packet_,
    const std::uint64_t frame_index_ = 0U,
    const Extent3D& reference_extent_ = {}) {
    return MakeFrameSnapshot(MakeFrameSnapshotSchema(packet_, frame_index_, reference_extent_));
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] render::SceneSubmissionSchema<DimensionT> MakeSceneSubmissionSchema(
    const FrameSnapshot<DimensionT>& snapshot_) noexcept {
    return render::SceneSubmissionSchema<DimensionT>{
        .metadata = snapshot_.Metadata(),
        .selection = snapshot_.selection,
        .payload = snapshot_.Payload(),
    };
}

} // namespace vr::render_graph
