#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/text/freetype_host.hpp"
#include "vr/text/text_types.hpp"

#include <cstdint>

namespace vr::text {

template<typename T>
using GlyphAtlasMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct GlyphAtlasCreateInfo {
    std::uint32_t page_width = 2048U;
    std::uint32_t page_height = 2048U;
    std::uint32_t max_page_count = 16U;
    std::uint32_t glyph_padding = 1U;
    std::uint32_t reserve_page_count = 4U;
    std::uint32_t reserve_glyph_count = 4096U;
    std::uint32_t reserve_page_dirty_rect_count = 256U;
};

struct GlyphAtlasResolveRequest {
    std::uint32_t font_id = 0U;
    std::uint32_t codepoint = 0U;
    std::int32_t load_flags = 0;
    GlyphRenderMode render_mode = GlyphRenderMode::normal;
};

struct GlyphAtlasResolvedEntry {
    bool has_glyph = false;
    std::uint32_t glyph_index = 0U;
    std::uint32_t codepoint = 0U;
    std::uint32_t font_id = 0U;
    FontFaceId face_id{};
    GlyphPixelMode pixel_mode = GlyphPixelMode::none;
    GlyphMetrics26_6 metrics{};
    GlyphAtlasRegion region{};
};

struct GlyphAtlasPageView {
    const std::uint8_t* pixels = nullptr;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t generation = 0U;
};

struct GlyphAtlasHostStats {
    std::uint32_t mapped_font_count = 0U;
    std::uint32_t page_count = 0U;
    std::uint32_t glyph_entry_count = 0U;
    std::uint32_t cache_hits = 0U;
    std::uint32_t cache_misses = 0U;
    std::uint32_t allocation_failures = 0U;
    std::uint32_t dirty_rect_count = 0U;
    std::uint64_t used_pixels = 0U;
    std::uint64_t uploaded_pixels = 0U;
};

class GlyphAtlasHost final {
public:
    GlyphAtlasHost() = default;
    ~GlyphAtlasHost() = default;

    GlyphAtlasHost(const GlyphAtlasHost&) = delete;
    GlyphAtlasHost& operator=(const GlyphAtlasHost&) = delete;

    GlyphAtlasHost(GlyphAtlasHost&&) = delete;
    GlyphAtlasHost& operator=(GlyphAtlasHost&&) = delete;

    void Initialize(FreeTypeHost& freetype_host_,
                    const GlyphAtlasCreateInfo& create_info_ = {});
    void Shutdown() noexcept;

    void MapFont(std::uint32_t font_id_, FontFaceId face_id_);
    [[nodiscard]] bool UnmapFont(std::uint32_t font_id_) noexcept;
    [[nodiscard]] FontFaceId ResolveMappedFace(std::uint32_t font_id_) const;

    [[nodiscard]] GlyphAtlasResolvedEntry ResolveGlyph(const GlyphAtlasResolveRequest& request_);
    [[nodiscard]] GlyphAtlasResolvedEntry ResolveGlyphWithFace(FontFaceId face_id_,
                                                               std::uint32_t codepoint_,
                                                               std::int32_t load_flags_ = 0,
                                                               GlyphRenderMode render_mode_ = GlyphRenderMode::normal);

    [[nodiscard]] std::uint32_t PageCount() const noexcept;
    [[nodiscard]] GlyphAtlasPageView Page(std::uint32_t page_index_) const;
    [[nodiscard]] const GlyphAtlasMcVector<GlyphRectU16>& PageDirtyRects(std::uint32_t page_index_) const;

    void ClearPageDirtyRects(std::uint32_t page_index_);
    void ClearAllDirtyRects();

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GlyphAtlasHostStats& Stats() const noexcept;

private:
    struct FontMapEntry {
        std::uint32_t font_id = 0U;
        FontFaceId face_id{};
    };

    struct GlyphKey {
        std::uint32_t face_id_value = 0U;
        std::uint32_t face_revision = 0U;
        std::uint32_t codepoint = 0U;
        std::int32_t load_flags = 0;
        std::uint8_t render_mode = 0U;
        std::uint8_t reserved0 = 0U;
        std::uint8_t reserved1 = 0U;
        std::uint8_t reserved2 = 0U;
    };

    struct GlyphEntry {
        std::uint64_t hash = 0U;
        GlyphKey key{};
        GlyphAtlasResolvedEntry value{};
    };

    struct GlyphLookupNode {
        std::uint64_t hash = 0U;
        std::uint32_t entry_index = 0U;
    };

    struct Shelf {
        std::uint16_t y = 0U;
        std::uint16_t height = 0U;
        std::uint16_t x_cursor = 0U;
    };

    struct AtlasPage {
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::uint32_t generation = 1U;
        GlyphAtlasMcVector<std::uint8_t> pixels{};
        GlyphAtlasMcVector<Shelf> shelves{};
        GlyphAtlasMcVector<GlyphRectU16> dirty_rects{};
    };

    struct AllocationResult {
        bool allocated = false;
        std::uint32_t page_index = 0U;
        GlyphRectU16 rect{};
        GlyphUvRect uv{};
    };

    [[nodiscard]] static std::uint64_t HashGlyphKey(const GlyphKey& key_) noexcept;
    [[nodiscard]] static bool EqualGlyphKey(const GlyphKey& lhs_, const GlyphKey& rhs_) noexcept;

    [[nodiscard]] static std::uint32_t LowerBoundFontMap(const GlyphAtlasMcVector<FontMapEntry>& font_map_,
                                                         std::uint32_t font_id_) noexcept;
    [[nodiscard]] static std::uint32_t LowerBoundLookup(const GlyphAtlasMcVector<GlyphLookupNode>& lookup_,
                                                        std::uint64_t hash_) noexcept;

    [[nodiscard]] AllocationResult AllocateRect(std::uint32_t width_, std::uint32_t height_);
    [[nodiscard]] bool TryAllocateInPage(AtlasPage& page_,
                                         std::uint32_t page_index_,
                                         std::uint32_t width_,
                                         std::uint32_t height_,
                                         AllocationResult& out_result_);
    [[nodiscard]] std::uint32_t CreatePage();

    void BlitBitmapToAtlas(AtlasPage& page_,
                           const GlyphRectU16& dst_rect_,
                           const GlyphBitmapView& bitmap_);
    void MarkDirtyRect(AtlasPage& page_, const GlyphRectU16& rect_);
    void RefreshStatsDerived() noexcept;

private:
    FreeTypeHost* freetype_host = nullptr;
    GlyphAtlasCreateInfo create_info_cache{};

    GlyphAtlasMcVector<FontMapEntry> font_map{};
    GlyphAtlasMcVector<GlyphEntry> glyph_entries{};
    GlyphAtlasMcVector<GlyphLookupNode> glyph_lookup{};
    GlyphAtlasMcVector<AtlasPage> pages{};

    GlyphAtlasHostStats stats{};
    bool initialized = false;
};

} // namespace vr::text
