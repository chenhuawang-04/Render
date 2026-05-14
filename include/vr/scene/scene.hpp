#pragma once

#include "vr/scene/background/sprite_background.hpp"
#include "vr/scene/background/sky_environment.hpp"
#include "vr/scene/scene_root_component.hpp"
#include "vr/scene/scene_traits.hpp"

#include <cstdint>

namespace vr::scene {

template<ecs::DimensionTag DimT, typename BackgroundT>
class Scene final {
public:
    using Dimension = DimT;
    using Background = BackgroundT;
    using Traits = SceneTraits<DimT, BackgroundT>;
    using View = typename Traits::View;
    using Packet = typename Traits::Packet;

    SceneRoot<DimT> root{};
    BackgroundT background{};
    std::uint64_t scene_revision = 0U;
};

using Scene2D = Scene<ecs::Dim2, SpriteBackground>;
using Scene3D = Scene<ecs::Dim3, SkyEnvironment>;

} // namespace vr::scene

