#pragma once

#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::ecs {

struct Rgba8 final {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

enum class TextHorizontalAlign : std::uint8_t {
    left = 0U,
    center = 1U,
    right = 2U,
};

enum class TextVerticalAlign : std::uint8_t {
    top = 0U,
    center = 1U,
    bottom = 2U,
};

enum class TextRenderPassHint : std::uint8_t {
    overlay = 0U,
    opaque = 1U,
    transparent = 2U,
};

enum TextDirtyFlags : std::uint32_t {
    text_dirty_flag = 1U << 0U,
    style_dirty_flag = 1U << 1U,
    runtime_dirty_flag = 1U << 2U,
};

struct TextBufferInlineUtf8 final {
    static constexpr std::uint32_t inline_capacity_bytes = 240U;

    std::uint32_t size_bytes;
    std::uint32_t capacity_bytes;
    std::uint32_t revision;
    std::uint32_t reserved;
    char utf8[inline_capacity_bytes];
};

struct TextRuntimeBatchData final {
    std::uint64_t sort_key;
    std::uint32_t font_id;
    std::uint32_t material_id;
    std::uint32_t atlas_page_id;
    std::uint32_t glyph_begin;
    std::uint32_t glyph_count;
    std::uint32_t batch_tag;
    std::uint32_t user_data;
    std::uint16_t depth_bin;
    std::uint8_t visible;
    TextRenderPassHint pass_hint;
    std::uint32_t dirty_flags;
};

struct TextStyle2D final {
    float pixel_size;
    float line_spacing;
    float letter_spacing;
    TextHorizontalAlign horizontal_align;
    TextVerticalAlign vertical_align;
    Rgba8 color;
    std::int16_t layer;
    std::uint8_t enable_sdf;
    std::uint8_t enable_outline;
    std::uint8_t outline_width_px;
    std::uint8_t reserved0;
    Rgba8 outline_color;
};

struct TextStyle3D final {
    float world_size;
    float max_screen_size_px;
    float line_spacing;
    float letter_spacing;
    TextHorizontalAlign horizontal_align;
    TextVerticalAlign vertical_align;
    Rgba8 color;
    std::uint8_t billboard;
    std::uint8_t depth_test;
    std::uint8_t depth_write;
    std::uint8_t enable_sdf;
    std::uint8_t enable_outline;
    std::uint8_t outline_width_px;
    std::uint8_t reserved0;
    std::uint8_t reserved1;
    Rgba8 outline_color;
};

template<DimensionTag DimensionT>
struct TextComponent;

template<>
struct TextComponent<Dim2> final {
    using StyleType = TextStyle2D;

    TextBufferInlineUtf8 text;
    StyleType style;
    TextRuntimeBatchData runtime;
};

template<>
struct TextComponent<Dim3> final {
    using StyleType = TextStyle3D;

    TextBufferInlineUtf8 text;
    StyleType style;
    TextRuntimeBatchData runtime;
};

template<DimensionTag DimensionT>
using Text = TextComponent<DimensionT>;

template<typename T>
concept PurePodComponent = std::is_standard_layout_v<T> &&
                           std::is_trivial_v<T>;

static_assert(PurePodComponent<Rgba8>);
static_assert(PurePodComponent<TextBufferInlineUtf8>);
static_assert(PurePodComponent<TextRuntimeBatchData>);
static_assert(PurePodComponent<TextStyle2D>);
static_assert(PurePodComponent<TextStyle3D>);
static_assert(PurePodComponent<Text<Dim2>>);
static_assert(PurePodComponent<Text<Dim3>>);
static_assert(sizeof(TextRuntimeBatchData) <= 64U);
static_assert(alignof(TextRuntimeBatchData) <= 8U);

} // namespace vr::ecs
