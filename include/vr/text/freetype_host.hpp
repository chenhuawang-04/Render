#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"

#include <cstdint>
#include <string>
#include <string_view>

struct FT_LibraryRec_;
struct FT_FaceRec_;
using FT_Library = FT_LibraryRec_*;
using FT_Face = FT_FaceRec_*;

namespace vr::text {

template<typename T>
using TextMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

enum class GlyphRenderMode : std::uint8_t {
    normal = 0U,
    light = 1U,
    mono = 2U,
    lcd = 3U,
    lcd_v = 4U,
    sdf = 5U,
};

enum class GlyphPixelMode : std::uint8_t {
    none = 0U,
    mono = 1U,
    gray = 2U,
    gray2 = 3U,
    gray4 = 4U,
    lcd = 5U,
    lcd_v = 6U,
    bgra = 7U,
    sdf = 8U,
    unknown = 255U,
};

struct FontFaceId {
    std::uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

struct FontFaceCreateInfo {
    std::string_view file_path{};
    std::int32_t face_index = 0;
    std::uint32_t pixel_width = 0U;
    std::uint32_t pixel_height = 32U;
};

struct FontFaceDescriptorView {
    std::string_view file_path{};
    std::int32_t face_index = 0;
    std::uint32_t pixel_width = 0U;
    std::uint32_t pixel_height = 0U;
};

struct FontFaceMetrics {
    std::int32_t ascender_26_6 = 0;
    std::int32_t descender_26_6 = 0;
    std::int32_t height_26_6 = 0;
    std::int32_t max_advance_26_6 = 0;
    std::uint16_t x_ppem = 0U;
    std::uint16_t y_ppem = 0U;
    std::uint16_t units_per_em = 0U;
    std::int16_t underline_position = 0;
    std::int16_t underline_thickness = 0;
};

struct GlyphRasterRequest {
    FontFaceId face_id{};
    std::uint32_t codepoint = 0U;
    std::int32_t load_flags = 0;
    GlyphRenderMode render_mode = GlyphRenderMode::normal;
};

struct GlyphBitmapView {
    const std::uint8_t* pixels = nullptr;
    std::uint32_t size_bytes = 0U;
    std::uint32_t width = 0U;
    std::uint32_t rows = 0U;
    std::uint32_t pitch = 0U;
    std::int32_t bearing_left = 0;
    std::int32_t bearing_top = 0;
    std::int32_t advance_x_26_6 = 0;
    std::int32_t advance_y_26_6 = 0;
    std::uint32_t glyph_index = 0U;
    GlyphPixelMode pixel_mode = GlyphPixelMode::none;
};

struct FreeTypeHostCreateInfo {
    std::uint32_t reserve_face_count = 16U;
    std::uint32_t reserve_path_blob_bytes = 4096U;
    std::uint32_t reserve_glyph_cache_count = 4096U;
    std::uint32_t reserve_glyph_blob_bytes = 4U * 1024U * 1024U;
    std::uint32_t max_glyph_cache_count = 131072U;
    std::uint32_t max_glyph_blob_bytes = 64U * 1024U * 1024U;
};

struct FreeTypeHostStats {
    std::uint32_t face_count = 0U;
    std::uint32_t face_cache_hits = 0U;
    std::uint32_t face_cache_misses = 0U;
    std::uint32_t glyph_cache_entries = 0U;
    std::uint32_t glyph_cache_hits = 0U;
    std::uint32_t glyph_cache_misses = 0U;
    std::uint32_t glyph_blob_bytes = 0U;
};

class FreeTypeHost final {
public:
    FreeTypeHost() = default;
    ~FreeTypeHost();

    FreeTypeHost(const FreeTypeHost&) = delete;
    FreeTypeHost& operator=(const FreeTypeHost&) = delete;

    FreeTypeHost(FreeTypeHost&&) = delete;
    FreeTypeHost& operator=(FreeTypeHost&&) = delete;

    void Initialize(const FreeTypeHostCreateInfo& create_info_ = {});
    void Shutdown() noexcept;

    [[nodiscard]] FontFaceId RegisterFace(const FontFaceCreateInfo& create_info_);
    [[nodiscard]] FontFaceId AcquireFaceVariant(FontFaceId base_face_id_,
                                                std::uint32_t pixel_height_,
                                                std::uint32_t pixel_width_ = 0U);
    void SetFacePixelSize(FontFaceId face_id_,
                          std::uint32_t pixel_height_,
                          std::uint32_t pixel_width_ = 0U);

    [[nodiscard]] bool IsFaceIdValid(FontFaceId face_id_) const noexcept;
    [[nodiscard]] bool SupportsSdfRasterization() const noexcept;
    [[nodiscard]] std::uint32_t FaceCount() const noexcept;
    [[nodiscard]] std::uint32_t FaceRevision(FontFaceId face_id_) const;
    [[nodiscard]] FontFaceDescriptorView FaceDescriptor(FontFaceId face_id_) const;
    [[nodiscard]] const FontFaceMetrics& FaceMetrics(FontFaceId face_id_) const;
    [[nodiscard]] std::uint32_t GlyphIndex(FontFaceId face_id_, std::uint32_t codepoint_) const;
    [[nodiscard]] bool HasGlyph(FontFaceId face_id_, std::uint32_t codepoint_) const;
    [[nodiscard]] std::int32_t KerningX26_6(FontFaceId face_id_,
                                            std::uint32_t left_codepoint_,
                                            std::uint32_t right_codepoint_) const;

    [[nodiscard]] GlyphBitmapView RasterizeGlyph(const GlyphRasterRequest& request_);

    void ClearGlyphCache() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const FreeTypeHostStats& Stats() const noexcept;
    [[nodiscard]] std::string_view LastErrorMessage() const noexcept;

private:
    struct FaceEntry {
        FT_Face face = nullptr;
        std::uint64_t key_hash = 0U;
        std::uint32_t path_offset = 0U;
        std::uint32_t path_size = 0U;
        std::int32_t face_index = 0;
        std::uint32_t pixel_width = 0U;
        std::uint32_t pixel_height = 0U;
        std::uint32_t revision = 1U;
        FontFaceMetrics metrics{};
    };

    struct FaceLookupNode {
        std::uint64_t hash = 0U;
        std::uint32_t entry_index = 0U;
    };

    struct GlyphCacheKey {
        std::uint32_t face_id_value = 0U;
        std::uint32_t face_revision = 0U;
        std::uint32_t codepoint = 0U;
        std::int32_t load_flags = 0;
        std::uint8_t render_mode = 0U;
        std::uint8_t reserved0 = 0U;
        std::uint8_t reserved1 = 0U;
        std::uint8_t reserved2 = 0U;
    };

    struct GlyphCacheEntry {
        std::uint64_t hash = 0U;
        GlyphCacheKey key{};
        std::uint32_t glyph_index = 0U;
        std::uint32_t blob_offset = 0U;
        std::uint32_t blob_size = 0U;
        std::uint32_t width = 0U;
        std::uint32_t rows = 0U;
        std::uint32_t pitch = 0U;
        std::int32_t bearing_left = 0;
        std::int32_t bearing_top = 0;
        std::int32_t advance_x_26_6 = 0;
        std::int32_t advance_y_26_6 = 0;
        GlyphPixelMode pixel_mode = GlyphPixelMode::none;
    };

    struct GlyphLookupNode {
        std::uint64_t hash = 0U;
        std::uint32_t entry_index = 0U;
    };

    [[nodiscard]] static std::uint32_t FaceIdToIndex(std::uint32_t face_id_value_);
    [[nodiscard]] static FontFaceId MakeFaceId(std::uint32_t face_entry_index_);

    [[nodiscard]] FaceEntry& AccessFaceEntry(FontFaceId face_id_);
    [[nodiscard]] const FaceEntry& AccessFaceEntry(FontFaceId face_id_) const;
    [[nodiscard]] std::string_view FacePathView(const FaceEntry& face_entry_) const noexcept;
    [[nodiscard]] bool EqualFaceKey(const FaceEntry& face_entry_,
                                    std::string_view file_path_,
                                    std::int32_t face_index_,
                                    std::uint32_t pixel_width_,
                                    std::uint32_t pixel_height_) const noexcept;

    void SetFacePixelSizeInternal(FaceEntry& face_entry_,
                                  std::uint32_t pixel_height_,
                                  std::uint32_t pixel_width_);

    [[nodiscard]] GlyphBitmapView BuildGlyphView(const GlyphCacheEntry& glyph_entry_) const noexcept;
    void EnsureGlyphCacheCapacity(std::uint32_t incoming_blob_bytes_);
    void RefreshStatsCacheFootprint() noexcept;
    void SetLastError(std::string message_);

private:
    FT_Library library = nullptr;

    TextMcVector<FaceEntry> face_entries{};
    TextMcVector<FaceLookupNode> face_lookup{};
    TextMcVector<char> path_blob{};

    TextMcVector<GlyphCacheEntry> glyph_cache_entries{};
    TextMcVector<GlyphLookupNode> glyph_lookup{};
    TextMcVector<std::uint8_t> glyph_blob{};

    FreeTypeHostCreateInfo create_info_cache{};
    FreeTypeHostStats stats{};
    std::string last_error_message{};
    bool initialized = false;
};

} // namespace vr::text
