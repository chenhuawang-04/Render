#include "vr/text/glyph_atlas_host.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace vr::text {

namespace {

template<typename LookupVectorT>
void InsertLookupNode(LookupVectorT& lookup_,
                      std::uint64_t hash_,
                      std::uint32_t entry_index_) {
    typename LookupVectorT::value_type node{};
    node.hash = hash_;
    node.entry_index = entry_index_;

    std::uint32_t first = 0U;
    std::uint32_t count = static_cast<std::uint32_t>(lookup_.size());
    while (count > 0U) {
        const std::uint32_t step = count / 2U;
        const std::uint32_t it = first + step;
        if (lookup_[it].hash < hash_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }

    lookup_.push_back(node);
    for (std::uint32_t i = static_cast<std::uint32_t>(lookup_.size() - 1U); i > first; --i) {
        lookup_[i] = lookup_[i - 1U];
    }
    lookup_[first] = node;
}

[[nodiscard]] std::uint32_t ClampToU16(std::uint32_t value_) noexcept {
    return std::min<std::uint32_t>(value_, std::numeric_limits<std::uint16_t>::max());
}

[[nodiscard]] std::uint8_t ExpandGray2(std::uint8_t value_) noexcept {
    return static_cast<std::uint8_t>((value_ * 255U) / 3U);
}

[[nodiscard]] std::uint8_t ExpandGray4(std::uint8_t value_) noexcept {
    return static_cast<std::uint8_t>((value_ * 255U) / 15U);
}

} // namespace

void GlyphAtlasHost::Initialize(FreeTypeHost& freetype_host_,
                                const GlyphAtlasCreateInfo& create_info_) {
    Shutdown();

    if (!freetype_host_.IsInitialized()) {
        throw std::runtime_error("GlyphAtlasHost::Initialize requires initialized FreeTypeHost");
    }
    if (create_info_.page_width == 0U || create_info_.page_height == 0U) {
        throw std::runtime_error("GlyphAtlasHost::Initialize requires non-zero page size");
    }
    if (create_info_.max_page_count == 0U) {
        throw std::runtime_error("GlyphAtlasHost::Initialize requires max_page_count > 0");
    }

    create_info_cache = create_info_;
    freetype_host = &freetype_host_;

    if (create_info_cache.reserve_page_count > 0U) {
        pages.reserve(create_info_cache.reserve_page_count);
    }
    if (create_info_cache.reserve_glyph_count > 0U) {
        glyph_entries.reserve(create_info_cache.reserve_glyph_count);
        glyph_lookup.reserve(create_info_cache.reserve_glyph_count);
    }

    stats = {};
    initialized = true;
}

void GlyphAtlasHost::Shutdown() noexcept {
    font_map.clear();
    glyph_entries.clear();
    glyph_lookup.clear();
    pages.clear();
    freetype_host = nullptr;
    create_info_cache = {};
    stats = {};
    initialized = false;
}

void GlyphAtlasHost::MapFont(std::uint32_t font_id_, FontFaceId face_id_) {
    if (!initialized || freetype_host == nullptr) {
        throw std::runtime_error("GlyphAtlasHost::MapFont called before Initialize");
    }
    if (font_id_ == 0U) {
        throw std::runtime_error("GlyphAtlasHost::MapFont requires non-zero font_id");
    }
    if (!face_id_.IsValid() || !freetype_host->IsFaceIdValid(face_id_)) {
        throw std::runtime_error("GlyphAtlasHost::MapFont received invalid face_id");
    }

    const std::uint32_t insert_pos = LowerBoundFontMap(font_map, font_id_);
    if (insert_pos < font_map.size() && font_map[insert_pos].font_id == font_id_) {
        font_map[insert_pos].face_id = face_id_;
    } else {
        font_map.push_back(FontMapEntry{.font_id = font_id_, .face_id = face_id_});
        for (std::uint32_t i = static_cast<std::uint32_t>(font_map.size() - 1U); i > insert_pos; --i) {
            font_map[i] = font_map[i - 1U];
        }
        font_map[insert_pos] = FontMapEntry{.font_id = font_id_, .face_id = face_id_};
    }
    stats.mapped_font_count = static_cast<std::uint32_t>(font_map.size());
}

bool GlyphAtlasHost::UnmapFont(std::uint32_t font_id_) noexcept {
    if (!initialized || font_id_ == 0U || font_map.empty()) {
        return false;
    }

    const std::uint32_t pos = LowerBoundFontMap(font_map, font_id_);
    if (pos >= font_map.size() || font_map[pos].font_id != font_id_) {
        return false;
    }

    for (std::uint32_t i = pos + 1U; i < font_map.size(); ++i) {
        font_map[i - 1U] = font_map[i];
    }
    font_map.pop_back();
    stats.mapped_font_count = static_cast<std::uint32_t>(font_map.size());
    return true;
}

FontFaceId GlyphAtlasHost::ResolveMappedFace(std::uint32_t font_id_) const {
    if (!initialized || freetype_host == nullptr || font_id_ == 0U) {
        return {};
    }

    const std::uint32_t pos = LowerBoundFontMap(font_map, font_id_);
    if (pos < font_map.size() && font_map[pos].font_id == font_id_) {
        return font_map[pos].face_id;
    }

    FontFaceId fallback_face_id{};
    fallback_face_id.value = font_id_;
    if (freetype_host->IsFaceIdValid(fallback_face_id)) {
        return fallback_face_id;
    }
    return {};
}

GlyphAtlasResolvedEntry GlyphAtlasHost::ResolveGlyph(const GlyphAtlasResolveRequest& request_) {
    if (!initialized || freetype_host == nullptr) {
        throw std::runtime_error("GlyphAtlasHost::ResolveGlyph called before Initialize");
    }
    if (request_.font_id == 0U) {
        throw std::runtime_error("GlyphAtlasHost::ResolveGlyph requires non-zero font_id");
    }

    const FontFaceId face_id = ResolveMappedFace(request_.font_id);
    if (!face_id.IsValid()) {
        throw std::runtime_error("GlyphAtlasHost::ResolveGlyph cannot resolve mapped face");
    }

    GlyphKey key{};
    key.face_id_value = face_id.value;
    key.face_revision = freetype_host->FaceRevision(face_id);
    key.codepoint = request_.codepoint;
    key.load_flags = request_.load_flags;
    key.render_mode = static_cast<std::uint8_t>(request_.render_mode);
    const std::uint64_t hash = HashGlyphKey(key);

    const std::uint32_t begin = LowerBoundLookup(glyph_lookup, hash);
    for (std::uint32_t i = begin; i < glyph_lookup.size(); ++i) {
        const GlyphLookupNode& node = glyph_lookup[i];
        if (node.hash != hash) {
            break;
        }
        if (node.entry_index >= glyph_entries.size()) {
            continue;
        }
        const GlyphEntry& glyph_entry = glyph_entries[node.entry_index];
        if (EqualGlyphKey(glyph_entry.key, key)) {
            ++stats.cache_hits;
            GlyphAtlasResolvedEntry cached = glyph_entry.value;
            cached.font_id = request_.font_id;
            return cached;
        }
    }

    ++stats.cache_misses;

    GlyphRasterRequest raster_request{};
    raster_request.face_id = face_id;
    raster_request.codepoint = request_.codepoint;
    raster_request.load_flags = request_.load_flags;
    raster_request.render_mode = request_.render_mode;
    const GlyphBitmapView bitmap = freetype_host->RasterizeGlyph(raster_request);

    GlyphAtlasResolvedEntry resolved{};
    resolved.codepoint = request_.codepoint;
    resolved.font_id = request_.font_id;
    resolved.face_id = face_id;
    resolved.glyph_index = bitmap.glyph_index;
    resolved.has_glyph = bitmap.glyph_index != 0U;
    resolved.region.page_index = k_invalid_glyph_page_index;
    resolved.pixel_mode = bitmap.pixel_mode;
    resolved.metrics = {
        .bearing_left = bitmap.bearing_left,
        .bearing_top = bitmap.bearing_top,
        .advance_x_26_6 = bitmap.advance_x_26_6,
        .advance_y_26_6 = bitmap.advance_y_26_6
    };

    if (resolved.has_glyph &&
        bitmap.width > 0U &&
        bitmap.rows > 0U &&
        bitmap.pixels != nullptr) {
        AllocationResult allocation = AllocateRect(bitmap.width, bitmap.rows);
        if (!allocation.allocated) {
            ++stats.allocation_failures;
            throw std::runtime_error("GlyphAtlasHost atlas allocation failed: out of pages or space");
        }

        AtlasPage& page = pages[allocation.page_index];
        BlitBitmapToAtlas(page, allocation.rect, bitmap);
        MarkDirtyRect(page, allocation.rect);

        resolved.region.page_index = allocation.page_index;
        resolved.region.pixel_rect = allocation.rect;
        resolved.region.uv_rect = allocation.uv;

        stats.used_pixels += static_cast<std::uint64_t>(bitmap.width) * bitmap.rows;
        stats.uploaded_pixels += static_cast<std::uint64_t>(bitmap.width) * bitmap.rows;
    }

    glyph_entries.push_back({
        .hash = hash,
        .key = key,
        .value = resolved
    });
    const std::uint32_t new_entry_index = static_cast<std::uint32_t>(glyph_entries.size() - 1U);
    InsertLookupNode(glyph_lookup, hash, new_entry_index);
    RefreshStatsDerived();
    return resolved;
}

GlyphAtlasResolvedEntry GlyphAtlasHost::ResolveGlyphWithFace(FontFaceId face_id_,
                                                             std::uint32_t codepoint_,
                                                             std::int32_t load_flags_,
                                                             GlyphRenderMode render_mode_) {
    if (!initialized || freetype_host == nullptr) {
        throw std::runtime_error("GlyphAtlasHost::ResolveGlyphWithFace called before Initialize");
    }
    if (!face_id_.IsValid() || !freetype_host->IsFaceIdValid(face_id_)) {
        throw std::runtime_error("GlyphAtlasHost::ResolveGlyphWithFace received invalid face_id");
    }

    GlyphKey key{};
    key.face_id_value = face_id_.value;
    key.face_revision = freetype_host->FaceRevision(face_id_);
    key.codepoint = codepoint_;
    key.load_flags = load_flags_;
    key.render_mode = static_cast<std::uint8_t>(render_mode_);
    const std::uint64_t hash = HashGlyphKey(key);

    const std::uint32_t begin = LowerBoundLookup(glyph_lookup, hash);
    for (std::uint32_t i = begin; i < glyph_lookup.size(); ++i) {
        const GlyphLookupNode& node = glyph_lookup[i];
        if (node.hash != hash) {
            break;
        }
        if (node.entry_index >= glyph_entries.size()) {
            continue;
        }
        const GlyphEntry& glyph_entry = glyph_entries[node.entry_index];
        if (EqualGlyphKey(glyph_entry.key, key)) {
            ++stats.cache_hits;
            GlyphAtlasResolvedEntry cached = glyph_entry.value;
            cached.font_id = 0U;
            return cached;
        }
    }

    ++stats.cache_misses;

    GlyphRasterRequest raster_request{};
    raster_request.face_id = face_id_;
    raster_request.codepoint = codepoint_;
    raster_request.load_flags = load_flags_;
    raster_request.render_mode = render_mode_;
    const GlyphBitmapView bitmap = freetype_host->RasterizeGlyph(raster_request);

    GlyphAtlasResolvedEntry resolved{};
    resolved.codepoint = codepoint_;
    resolved.font_id = 0U;
    resolved.face_id = face_id_;
    resolved.glyph_index = bitmap.glyph_index;
    resolved.has_glyph = bitmap.glyph_index != 0U;
    resolved.region.page_index = k_invalid_glyph_page_index;
    resolved.pixel_mode = bitmap.pixel_mode;
    resolved.metrics = {
        .bearing_left = bitmap.bearing_left,
        .bearing_top = bitmap.bearing_top,
        .advance_x_26_6 = bitmap.advance_x_26_6,
        .advance_y_26_6 = bitmap.advance_y_26_6
    };

    if (resolved.has_glyph &&
        bitmap.width > 0U &&
        bitmap.rows > 0U &&
        bitmap.pixels != nullptr) {
        AllocationResult allocation = AllocateRect(bitmap.width, bitmap.rows);
        if (!allocation.allocated) {
            ++stats.allocation_failures;
            throw std::runtime_error("GlyphAtlasHost atlas allocation failed: out of pages or space");
        }

        AtlasPage& page = pages[allocation.page_index];
        BlitBitmapToAtlas(page, allocation.rect, bitmap);
        MarkDirtyRect(page, allocation.rect);

        resolved.region.page_index = allocation.page_index;
        resolved.region.pixel_rect = allocation.rect;
        resolved.region.uv_rect = allocation.uv;

        stats.used_pixels += static_cast<std::uint64_t>(bitmap.width) * bitmap.rows;
        stats.uploaded_pixels += static_cast<std::uint64_t>(bitmap.width) * bitmap.rows;
    }

    glyph_entries.push_back({
        .hash = hash,
        .key = key,
        .value = resolved
    });
    const std::uint32_t new_entry_index = static_cast<std::uint32_t>(glyph_entries.size() - 1U);
    InsertLookupNode(glyph_lookup, hash, new_entry_index);
    RefreshStatsDerived();
    return resolved;
}

std::uint32_t GlyphAtlasHost::PageCount() const noexcept {
    return static_cast<std::uint32_t>(pages.size());
}

GlyphAtlasPageView GlyphAtlasHost::Page(std::uint32_t page_index_) const {
    if (!initialized) {
        throw std::runtime_error("GlyphAtlasHost::Page called before Initialize");
    }
    if (page_index_ >= pages.size()) {
        throw std::out_of_range("GlyphAtlasHost::Page index out of range");
    }

    const AtlasPage& page = pages[page_index_];
    GlyphAtlasPageView view{};
    view.pixels = page.pixels.empty() ? nullptr : page.pixels.data();
    view.width = page.width;
    view.height = page.height;
    view.generation = page.generation;
    return view;
}

const GlyphAtlasMcVector<GlyphRectU16>& GlyphAtlasHost::PageDirtyRects(std::uint32_t page_index_) const {
    if (!initialized) {
        throw std::runtime_error("GlyphAtlasHost::PageDirtyRects called before Initialize");
    }
    if (page_index_ >= pages.size()) {
        throw std::out_of_range("GlyphAtlasHost::PageDirtyRects index out of range");
    }
    return pages[page_index_].dirty_rects;
}

void GlyphAtlasHost::ClearPageDirtyRects(std::uint32_t page_index_) {
    if (!initialized) {
        throw std::runtime_error("GlyphAtlasHost::ClearPageDirtyRects called before Initialize");
    }
    if (page_index_ >= pages.size()) {
        throw std::out_of_range("GlyphAtlasHost::ClearPageDirtyRects index out of range");
    }
    pages[page_index_].dirty_rects.clear();
    RefreshStatsDerived();
}

void GlyphAtlasHost::ClearAllDirtyRects() {
    if (!initialized) {
        throw std::runtime_error("GlyphAtlasHost::ClearAllDirtyRects called before Initialize");
    }
    for (auto& page : pages) {
        page.dirty_rects.clear();
    }
    RefreshStatsDerived();
}

bool GlyphAtlasHost::IsInitialized() const noexcept {
    return initialized;
}

const GlyphAtlasHostStats& GlyphAtlasHost::Stats() const noexcept {
    return stats;
}

std::uint64_t GlyphAtlasHost::HashGlyphKey(const GlyphKey& key_) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    hash ^= static_cast<std::uint64_t>(key_.face_id_value);
    hash *= 0x100000001b3ULL;
    hash ^= static_cast<std::uint64_t>(key_.face_revision);
    hash *= 0x100000001b3ULL;
    hash ^= static_cast<std::uint64_t>(key_.codepoint);
    hash *= 0x100000001b3ULL;
    hash ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(key_.load_flags));
    hash *= 0x100000001b3ULL;
    hash ^= static_cast<std::uint64_t>(key_.render_mode);
    hash *= 0x100000001b3ULL;
    return hash;
}

bool GlyphAtlasHost::EqualGlyphKey(const GlyphKey& lhs_, const GlyphKey& rhs_) noexcept {
    return lhs_.face_id_value == rhs_.face_id_value &&
           lhs_.face_revision == rhs_.face_revision &&
           lhs_.codepoint == rhs_.codepoint &&
           lhs_.load_flags == rhs_.load_flags &&
           lhs_.render_mode == rhs_.render_mode;
}

std::uint32_t GlyphAtlasHost::LowerBoundFontMap(const GlyphAtlasMcVector<FontMapEntry>& font_map_,
                                                std::uint32_t font_id_) noexcept {
    std::uint32_t first = 0U;
    std::uint32_t count = static_cast<std::uint32_t>(font_map_.size());
    while (count > 0U) {
        const std::uint32_t step = count / 2U;
        const std::uint32_t it = first + step;
        if (font_map_[it].font_id < font_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

std::uint32_t GlyphAtlasHost::LowerBoundLookup(const GlyphAtlasMcVector<GlyphLookupNode>& lookup_,
                                               std::uint64_t hash_) noexcept {
    std::uint32_t first = 0U;
    std::uint32_t count = static_cast<std::uint32_t>(lookup_.size());
    while (count > 0U) {
        const std::uint32_t step = count / 2U;
        const std::uint32_t it = first + step;
        if (lookup_[it].hash < hash_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

GlyphAtlasHost::AllocationResult GlyphAtlasHost::AllocateRect(std::uint32_t width_,
                                                              std::uint32_t height_) {
    AllocationResult result{};
    if (width_ == 0U || height_ == 0U) {
        result.allocated = true;
        return result;
    }

    for (std::uint32_t page_index = 0U; page_index < pages.size(); ++page_index) {
        if (TryAllocateInPage(pages[page_index], page_index, width_, height_, result)) {
            return result;
        }
    }

    if (pages.size() >= create_info_cache.max_page_count) {
        return result;
    }

    const std::uint32_t new_page_index = CreatePage();
    if (!TryAllocateInPage(pages[new_page_index], new_page_index, width_, height_, result)) {
        return result;
    }
    return result;
}

bool GlyphAtlasHost::TryAllocateInPage(AtlasPage& page_,
                                       std::uint32_t page_index_,
                                       std::uint32_t width_,
                                       std::uint32_t height_,
                                       AllocationResult& out_result_) {
    const std::uint32_t padded_width = width_ + create_info_cache.glyph_padding * 2U;
    const std::uint32_t padded_height = height_ + create_info_cache.glyph_padding * 2U;
    if (padded_width > page_.width || padded_height > page_.height) {
        return false;
    }

    std::uint32_t best_shelf_index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t best_waste = std::numeric_limits<std::uint32_t>::max();

    for (std::uint32_t i = 0U; i < page_.shelves.size(); ++i) {
        const Shelf& shelf = page_.shelves[i];
        if (padded_height > shelf.height) {
            continue;
        }
        if (static_cast<std::uint32_t>(shelf.x_cursor) + padded_width > page_.width) {
            continue;
        }
        const std::uint32_t waste = static_cast<std::uint32_t>(shelf.height) - padded_height;
        if (waste < best_waste) {
            best_waste = waste;
            best_shelf_index = i;
        }
    }

    if (best_shelf_index == std::numeric_limits<std::uint32_t>::max()) {
        std::uint32_t next_y = 0U;
        if (!page_.shelves.empty()) {
            const Shelf& last_shelf = page_.shelves.back();
            next_y = static_cast<std::uint32_t>(last_shelf.y) + last_shelf.height;
        }
        if (next_y + padded_height > page_.height) {
            return false;
        }

        Shelf new_shelf{};
        new_shelf.y = static_cast<std::uint16_t>(ClampToU16(next_y));
        new_shelf.height = static_cast<std::uint16_t>(ClampToU16(padded_height));
        new_shelf.x_cursor = 0U;
        page_.shelves.push_back(new_shelf);
        best_shelf_index = static_cast<std::uint32_t>(page_.shelves.size() - 1U);
    }

    Shelf& shelf = page_.shelves[best_shelf_index];
    const std::uint32_t x = static_cast<std::uint32_t>(shelf.x_cursor) + create_info_cache.glyph_padding;
    const std::uint32_t y = static_cast<std::uint32_t>(shelf.y) + create_info_cache.glyph_padding;
    shelf.x_cursor = static_cast<std::uint16_t>(ClampToU16(
        static_cast<std::uint32_t>(shelf.x_cursor) + padded_width));

    out_result_.allocated = true;
    out_result_.page_index = page_index_;
    out_result_.rect.x = static_cast<std::uint16_t>(ClampToU16(x));
    out_result_.rect.y = static_cast<std::uint16_t>(ClampToU16(y));
    out_result_.rect.width = static_cast<std::uint16_t>(ClampToU16(width_));
    out_result_.rect.height = static_cast<std::uint16_t>(ClampToU16(height_));
    out_result_.uv.u0 = static_cast<float>(x) / static_cast<float>(page_.width);
    out_result_.uv.v0 = static_cast<float>(y) / static_cast<float>(page_.height);
    out_result_.uv.u1 = static_cast<float>(x + width_) / static_cast<float>(page_.width);
    out_result_.uv.v1 = static_cast<float>(y + height_) / static_cast<float>(page_.height);
    return true;
}

std::uint32_t GlyphAtlasHost::CreatePage() {
    AtlasPage page{};
    page.width = create_info_cache.page_width;
    page.height = create_info_cache.page_height;
    page.generation = 1U;

    const std::size_t pixel_count = static_cast<std::size_t>(page.width) * page.height;
    page.pixels.resize(pixel_count);
    std::memset(page.pixels.data(), 0, pixel_count);
    page.shelves.reserve(64U);
    if (create_info_cache.reserve_page_dirty_rect_count > 0U) {
        page.dirty_rects.reserve(create_info_cache.reserve_page_dirty_rect_count);
    }

    pages.push_back(std::move(page));
    stats.page_count = static_cast<std::uint32_t>(pages.size());
    return static_cast<std::uint32_t>(pages.size() - 1U);
}

void GlyphAtlasHost::BlitBitmapToAtlas(AtlasPage& page_,
                                       const GlyphRectU16& dst_rect_,
                                       const GlyphBitmapView& bitmap_) {
    if (dst_rect_.width == 0U || dst_rect_.height == 0U || bitmap_.pixels == nullptr) {
        return;
    }

    const std::uint32_t dst_x = dst_rect_.x;
    const std::uint32_t dst_y = dst_rect_.y;
    const std::uint32_t width = dst_rect_.width;
    const std::uint32_t height = dst_rect_.height;

    if (dst_x + width > page_.width || dst_y + height > page_.height) {
        throw std::runtime_error("GlyphAtlasHost::BlitBitmapToAtlas destination out of page bounds");
    }

    for (std::uint32_t row = 0U; row < height; ++row) {
        std::uint8_t* dst = page_.pixels.data() +
            static_cast<std::size_t>(dst_y + row) * page_.width + dst_x;
        const std::uint8_t* src = bitmap_.pixels + static_cast<std::size_t>(row) * bitmap_.pitch;

        switch (bitmap_.pixel_mode) {
            case GlyphPixelMode::gray:
            case GlyphPixelMode::lcd:
            case GlyphPixelMode::lcd_v:
            case GlyphPixelMode::sdf: {
                std::memcpy(dst, src, width);
                break;
            }
            case GlyphPixelMode::mono: {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const std::uint8_t src_byte = src[x >> 3U];
                    const std::uint8_t bit_mask = static_cast<std::uint8_t>(0x80U >> (x & 7U));
                    dst[x] = (src_byte & bit_mask) != 0U ? 255U : 0U;
                }
                break;
            }
            case GlyphPixelMode::gray2: {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const std::uint8_t packed = src[x >> 2U];
                    const std::uint8_t shift = static_cast<std::uint8_t>(6U - ((x & 3U) * 2U));
                    dst[x] = ExpandGray2(static_cast<std::uint8_t>((packed >> shift) & 0x03U));
                }
                break;
            }
            case GlyphPixelMode::gray4: {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const std::uint8_t packed = src[x >> 1U];
                    const std::uint8_t value = ((x & 1U) == 0U)
                        ? static_cast<std::uint8_t>((packed >> 4U) & 0x0FU)
                        : static_cast<std::uint8_t>(packed & 0x0FU);
                    dst[x] = ExpandGray4(value);
                }
                break;
            }
            case GlyphPixelMode::bgra: {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const std::uint8_t* pixel = src + x * 4U;
                    dst[x] = pixel[3U];
                }
                break;
            }
            case GlyphPixelMode::none:
            case GlyphPixelMode::unknown:
            default: {
                std::memset(dst, 0, width);
                break;
            }
        }
    }
}

void GlyphAtlasHost::MarkDirtyRect(AtlasPage& page_, const GlyphRectU16& rect_) {
    if (rect_.width == 0U || rect_.height == 0U) {
        return;
    }

    std::uint32_t merged_x = rect_.x;
    std::uint32_t merged_y = rect_.y;
    std::uint32_t merged_x1 = rect_.x + rect_.width;
    std::uint32_t merged_y1 = rect_.y + rect_.height;

    for (std::uint32_t i = 0U; i < page_.dirty_rects.size();) {
        const GlyphRectU16& existing = page_.dirty_rects[i];
        const std::uint32_t ex0 = existing.x;
        const std::uint32_t ey0 = existing.y;
        const std::uint32_t ex1 = existing.x + existing.width;
        const std::uint32_t ey1 = existing.y + existing.height;

        const bool separated =
            merged_x > ex1 || ex0 > merged_x1 ||
            merged_y > ey1 || ey0 > merged_y1;
        if (separated) {
            ++i;
            continue;
        }

        merged_x = std::min(merged_x, ex0);
        merged_y = std::min(merged_y, ey0);
        merged_x1 = std::max(merged_x1, ex1);
        merged_y1 = std::max(merged_y1, ey1);

        for (std::uint32_t j = i + 1U; j < page_.dirty_rects.size(); ++j) {
            page_.dirty_rects[j - 1U] = page_.dirty_rects[j];
        }
        page_.dirty_rects.pop_back();
    }

    GlyphRectU16 merged{};
    merged.x = static_cast<std::uint16_t>(ClampToU16(merged_x));
    merged.y = static_cast<std::uint16_t>(ClampToU16(merged_y));
    merged.width = static_cast<std::uint16_t>(ClampToU16(merged_x1 - merged_x));
    merged.height = static_cast<std::uint16_t>(ClampToU16(merged_y1 - merged_y));
    page_.dirty_rects.push_back(merged);
    RefreshStatsDerived();
}

void GlyphAtlasHost::RefreshStatsDerived() noexcept {
    stats.page_count = static_cast<std::uint32_t>(pages.size());
    stats.glyph_entry_count = static_cast<std::uint32_t>(glyph_entries.size());
    std::uint32_t dirty_total = 0U;
    for (const auto& page : pages) {
        dirty_total += static_cast<std::uint32_t>(page.dirty_rects.size());
    }
    stats.dirty_rect_count = dirty_total;
}

} // namespace vr::text
