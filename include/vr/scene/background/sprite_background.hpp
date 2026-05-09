#pragma once

#include "vr/ecs/component/spatial_types.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::scene {

enum class Background2DMode : std::uint8_t {
    none = 0U,
    solid_color = 1U,
    gradient = 2U,
    sprite = 3U,
    surface_entity = 4U,
};

enum class BackgroundScaleMode : std::uint8_t {
    stretch = 0U,
    contain = 1U,
    cover = 2U,
    native_pixels = 3U,
};

struct SpriteBackground final {
    Background2DMode mode;
    BackgroundScaleMode scale_mode;

    std::uint32_t image_id;
    std::uint32_t sprite_id;
    std::uint32_t surface_entity_id;

    ecs::Float4 color0;
    ecs::Float4 color1;

    float opacity;
    float parallax;

    std::int16_t layer;
    std::uint16_t flags;
    std::uint32_t revision;
};

struct Background2DRenderState final {
    Background2DMode mode;

    std::uint32_t image_id;
    std::uint32_t sprite_id;
    std::uint32_t surface_entity_id;

    ecs::Float4 color0;
    ecs::Float4 color1;

    float opacity;
    std::int16_t layer;
    std::uint16_t flags;
    std::uint32_t revision;
};

static_assert(std::is_standard_layout_v<SpriteBackground>);
static_assert(std::is_trivial_v<SpriteBackground>);
static_assert(std::is_standard_layout_v<Background2DRenderState>);
static_assert(std::is_trivial_v<Background2DRenderState>);

} // namespace vr::scene
