#pragma once

#include "vr/render/render_view.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/scene/background/background_traits.hpp"

namespace vr::scene {

template<ecs::DimensionTag DimT, typename BackgroundT>
struct SceneTraits final {
    using Dimension = DimT;
    using Background = BackgroundT;
    using BackgroundTraits = SceneBackgroundTraits<DimT, BackgroundT>;
    using RenderState = typename BackgroundTraits::RenderState;
    using View = render::RenderView<DimT>;
    using Packet = render::RenderScenePacket<DimT>;
};

} // namespace vr::scene
