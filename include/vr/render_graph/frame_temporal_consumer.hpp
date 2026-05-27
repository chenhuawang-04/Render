#pragma once

#include "vr/render_graph/frame_history_contract.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <string_view>
#include <utility>

namespace vr::render_graph {

struct ImportedTemporalSurface final {
    ResourceHandle resource{};
    FrameHistoryResourceIdentity identity{};
    render::SceneSubmissionId previous_submission_id{};
    std::uint64_t previous_frame_index = 0U;
    FrameHistoryInvalidationReason invalidation_reason =
        FrameHistoryInvalidationReason::none;
    bool available = false;
};

struct TemporalConsumerRequirements final {
    bool requires_previous_color = true;
    bool requires_previous_depth = false;
    bool requires_previous_motion = false;
    bool requires_current_motion = false;
    bool requires_temporal_jitter = false;
};

struct TemporalConsumerAvailability final {
    bool ready = false;
    bool fallback_to_current = true;
    bool previous_color_available = false;
    bool previous_depth_available = false;
    bool previous_motion_available = false;
    bool current_motion_available = false;
    bool temporal_jitter_available = false;
    FrameHistoryInvalidationReason invalidation_reason =
        FrameHistoryInvalidationReason::none;
};

[[nodiscard]] constexpr TemporalConsumerAvailability EvaluateTemporalConsumerAvailability(
    const FrameTemporalContract& temporal_,
    const TemporalConsumerRequirements requirements_ = {}) noexcept {
    TemporalConsumerAvailability availability{};
    availability.previous_color_available =
        temporal_.color.previous_available &&
        IsValidFrameHistoryResourceIdentity(temporal_.color.previous);
    availability.previous_depth_available =
        temporal_.depth.previous_available &&
        IsValidFrameHistoryResourceIdentity(temporal_.depth.previous);
    availability.previous_motion_available =
        temporal_.motion.previous_available &&
        IsValidFrameHistoryResourceIdentity(temporal_.motion.previous);
    availability.current_motion_available =
        temporal_.motion.current_writable &&
        IsValidFrameHistoryResourceIdentity(temporal_.motion.current) &&
        temporal_.motion.invalidation_reason ==
            FrameHistoryInvalidationReason::none &&
        temporal_.reprojection.current_available &&
        temporal_.reprojection.previous_available;
    availability.temporal_jitter_available =
        temporal_.jitter.current_available &&
        temporal_.jitter.previous_available;

    availability.ready =
        (!requirements_.requires_previous_color ||
         availability.previous_color_available) &&
        (!requirements_.requires_previous_depth ||
         availability.previous_depth_available) &&
        (!requirements_.requires_previous_motion ||
         availability.previous_motion_available) &&
        (!requirements_.requires_current_motion ||
         availability.current_motion_available) &&
        (!requirements_.requires_temporal_jitter ||
         availability.temporal_jitter_available);
    availability.fallback_to_current = !availability.ready;

    if (requirements_.requires_previous_color &&
        !availability.previous_color_available) {
        availability.invalidation_reason =
            temporal_.color.invalidation_reason;
        return availability;
    }
    if (requirements_.requires_previous_depth &&
        !availability.previous_depth_available) {
        availability.invalidation_reason =
            temporal_.depth.invalidation_reason;
        return availability;
    }
    if (requirements_.requires_previous_motion &&
        !availability.previous_motion_available) {
        availability.invalidation_reason =
            temporal_.motion.invalidation_reason;
        return availability;
    }
    if (requirements_.requires_current_motion &&
        !availability.current_motion_available) {
        availability.invalidation_reason =
            temporal_.reprojection.invalidation_reason !=
                FrameHistoryInvalidationReason::none
            ? temporal_.reprojection.invalidation_reason
            : temporal_.motion.invalidation_reason;
        return availability;
    }
    if (requirements_.requires_temporal_jitter &&
        !availability.temporal_jitter_available) {
        availability.invalidation_reason =
            temporal_.jitter.invalidation_reason;
        return availability;
    }

    availability.invalidation_reason =
        availability.ready ? FrameHistoryInvalidationReason::none
                           : temporal_.color.invalidation_reason;
    return availability;
}

template<typename RegisterImportedTextureFn>
[[nodiscard]] ImportedTemporalSurface ImportCurrentTemporalSurface(
    RenderGraphBuilder& builder_,
    std::string_view debug_name_,
    const FrameHistorySurfaceContract& contract_,
    RegisterImportedTextureFn&& register_imported_texture_) {
    ImportedTemporalSurface imported{};
    imported.identity = contract_.current;
    imported.invalidation_reason = contract_.invalidation_reason;

    if (!contract_.current_writable ||
        !IsValidFrameHistoryResourceIdentity(contract_.current)) {
        return imported;
    }

    imported.resource = builder_.CreateTexture(debug_name_,
                                               contract_.desc,
                                               ResourceLifetime::imported);
    std::forward<RegisterImportedTextureFn>(register_imported_texture_)(
        imported.resource,
        contract_.current.handle);
    imported.available = true;
    imported.invalidation_reason = FrameHistoryInvalidationReason::none;
    return imported;
}

template<typename RegisterImportedTextureFn>
[[nodiscard]] ImportedTemporalSurface ImportPreviousTemporalSurface(
    RenderGraphBuilder& builder_,
    std::string_view debug_name_,
    const FrameHistorySurfaceContract& contract_,
    RegisterImportedTextureFn&& register_imported_texture_) {
    ImportedTemporalSurface imported{};
    imported.identity = contract_.previous;
    imported.previous_submission_id = contract_.previous_submission_id;
    imported.previous_frame_index = contract_.previous_frame_index;
    imported.invalidation_reason = contract_.invalidation_reason;

    if (!contract_.previous_available ||
        !IsValidFrameHistoryResourceIdentity(contract_.previous)) {
        return imported;
    }

    imported.resource = builder_.CreateTexture(debug_name_,
                                               contract_.desc,
                                               ResourceLifetime::imported);
    std::forward<RegisterImportedTextureFn>(register_imported_texture_)(
        imported.resource,
        contract_.previous.handle);
    imported.available = true;
    imported.invalidation_reason = FrameHistoryInvalidationReason::none;
    return imported;
}

} // namespace vr::render_graph
