#pragma once

#include "vr/ecs/concept/dimension.hpp"
#include "vr/scene/background/sprite_background.hpp"
#include "vr/scene/background/sky_environment.hpp"

namespace vr::scene {

template<ecs::DimensionTag DimT, typename BackgroundT>
struct SceneBackgroundTraits;

template<>
struct SceneBackgroundTraits<ecs::Dim2, SpriteBackground> final {
    using RenderState = Background2DRenderState;
    static constexpr bool uses_surface_path = true;
    static constexpr bool uses_environment_gpu = false;
};

template<>
struct SceneBackgroundTraits<ecs::Dim3, SkyEnvironment> final {
    using RenderState = SkyEnvironmentRenderState;
    static constexpr bool uses_surface_path = false;
    static constexpr bool uses_environment_gpu = true;
};

} // namespace vr::scene

