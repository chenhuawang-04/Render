#pragma once

#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::scene {

template<ecs::DimensionTag DimT>
struct SceneRoot final {
    std::uint32_t active_camera_entity;
    std::uint32_t background_entity;
    std::uint32_t environment_entity;
    std::uint32_t flags;
    std::uint32_t revision;
};

static_assert(std::is_standard_layout_v<SceneRoot<ecs::Dim2>>);
static_assert(std::is_trivial_v<SceneRoot<ecs::Dim2>>);
static_assert(std::is_standard_layout_v<SceneRoot<ecs::Dim3>>);
static_assert(std::is_trivial_v<SceneRoot<ecs::Dim3>>);

} // namespace vr::scene
