#pragma once

#include "vr/ecs/component/spatial_types.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render_graph/frame_history_contract.hpp"

#include <cstdint>

namespace vr::render {

struct SceneTemporalMotionProducerState final {
    ecs::Matrix4x4 current_clip_to_previous_clip{};
    float current_jitter_uv_x = 0.0F;
    float current_jitter_uv_y = 0.0F;
    float previous_jitter_uv_x = 0.0F;
    float previous_jitter_uv_y = 0.0F;
    SceneSubmissionId previous_submission_id{};
    std::uint64_t previous_frame_index = 0U;
    render_graph::FrameHistoryInvalidationReason invalidation_reason =
        render_graph::FrameHistoryInvalidationReason::none;
    bool previous_available = false;
};

} // namespace vr::render
