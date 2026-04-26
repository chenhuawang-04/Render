#pragma once

#include <cstdint>
#include <type_traits>

namespace vr::text {

inline constexpr std::uint32_t k_invalid_glyph_page_index = 0xFFFFFFFFU;

struct GlyphRectU16 final {
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t width;
    std::uint16_t height;
};

struct GlyphUvRect final {
    float u0;
    float v0;
    float u1;
    float v1;
};

struct GlyphAtlasRegion final {
    std::uint32_t page_index;
    GlyphRectU16 pixel_rect;
    GlyphUvRect uv_rect;

    [[nodiscard]] bool HasAtlasPixels() const noexcept {
        return page_index != k_invalid_glyph_page_index &&
               pixel_rect.width > 0U &&
               pixel_rect.height > 0U;
    }
};

struct GlyphMetrics26_6 final {
    std::int32_t bearing_left;
    std::int32_t bearing_top;
    std::int32_t advance_x_26_6;
    std::int32_t advance_y_26_6;
};

template<typename T>
concept PodTextType = std::is_standard_layout_v<T> &&
                      std::is_trivial_v<T>;

static_assert(PodTextType<GlyphRectU16>);
static_assert(PodTextType<GlyphUvRect>);
static_assert(PodTextType<GlyphAtlasRegion>);
static_assert(PodTextType<GlyphMetrics26_6>);

} // namespace vr::text
