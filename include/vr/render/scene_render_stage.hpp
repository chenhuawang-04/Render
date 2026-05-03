#pragma once

#include <cstdint>

namespace vr::render {

enum class SceneRenderStage : std::uint8_t {
    opaque = 0U,
    transparent = 1U,
};

using SceneRecorder3DSceneStage = SceneRenderStage;

[[nodiscard]] constexpr std::uint32_t SceneRenderStagePassHintValue(
    SceneRenderStage stage_) noexcept {
    switch (stage_) {
    case SceneRenderStage::opaque:
        return 1U;
    case SceneRenderStage::transparent:
        return 2U;
    default:
        break;
    }
    return 1U;
}

} // namespace vr::render
