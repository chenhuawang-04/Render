#pragma once

#include "vr/ecs/component/spatial_types.hpp"
#include "vr/render/render_target_types.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render_graph/render_graph_types.hpp"

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace vr::render_graph {

enum class FrameHistoryInvalidationReason : std::uint8_t {
    none = 0U,
    first_frame = 1U,
    reset_requested = 2U,
    extent_changed = 3U,
    dimension_changed = 4U,
    source_unavailable = 5U,
};

[[nodiscard]] constexpr std::string_view FrameHistoryInvalidationReasonName(
    const FrameHistoryInvalidationReason reason_) noexcept {
    switch (reason_) {
    case FrameHistoryInvalidationReason::none:
        return "none";
    case FrameHistoryInvalidationReason::first_frame:
        return "first_frame";
    case FrameHistoryInvalidationReason::reset_requested:
        return "reset_requested";
    case FrameHistoryInvalidationReason::extent_changed:
        return "extent_changed";
    case FrameHistoryInvalidationReason::dimension_changed:
        return "dimension_changed";
    case FrameHistoryInvalidationReason::source_unavailable:
        return "source_unavailable";
    }
    return "unknown";
}

struct FrameHistoryResourceIdentity final {
    render::RenderTargetHandle handle{};
    std::uint32_t resource_revision = 0U;
};

[[nodiscard]] constexpr bool IsValidFrameHistoryResourceIdentity(
    const FrameHistoryResourceIdentity& identity_) noexcept {
    return render::IsValidRenderTargetHandle(identity_.handle) &&
           identity_.resource_revision != 0U;
}

struct FrameHistorySurfaceContract final {
    TextureDesc desc{};
    FrameHistoryResourceIdentity previous{};
    FrameHistoryResourceIdentity current{};
    render::SceneSubmissionId previous_submission_id{};
    std::uint64_t previous_frame_index = 0U;
    FrameHistoryInvalidationReason invalidation_reason =
        FrameHistoryInvalidationReason::none;
    bool previous_available = false;
    bool current_writable = false;
};

struct FrameTemporalReprojectionContract final {
    ecs::Matrix4x4 current_clip_to_previous_clip{};
    render::SceneSubmissionId previous_submission_id{};
    std::uint64_t previous_frame_index = 0U;
    FrameHistoryInvalidationReason invalidation_reason =
        FrameHistoryInvalidationReason::none;
    bool current_available = false;
    bool previous_available = false;
};

struct FrameTemporalJitterContract final {
    float current_uv_x = 0.0F;
    float current_uv_y = 0.0F;
    float previous_uv_x = 0.0F;
    float previous_uv_y = 0.0F;
    render::SceneSubmissionId previous_submission_id{};
    std::uint64_t previous_frame_index = 0U;
    FrameHistoryInvalidationReason invalidation_reason =
        FrameHistoryInvalidationReason::none;
    bool current_available = false;
    bool previous_available = false;
};

struct FrameTemporalContract final {
    FrameHistorySurfaceContract color{};
    FrameHistorySurfaceContract depth{};
    FrameHistorySurfaceContract motion{};
    FrameTemporalReprojectionContract reprojection{};
    FrameTemporalJitterContract jitter{};
};

static_assert(std::is_standard_layout_v<FrameHistoryResourceIdentity>);
static_assert(std::is_trivially_copyable_v<FrameHistoryResourceIdentity>);

} // namespace vr::render_graph
