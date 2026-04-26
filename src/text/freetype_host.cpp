#include "vr/text/freetype_host.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ERRORS_H

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vr::text {

namespace {

#if defined(FT_RENDER_MODE_SDF) && defined(FT_PIXEL_MODE_SDF)
inline constexpr bool k_ft_sdf_supported = true;
#else
inline constexpr bool k_ft_sdf_supported = false;
#endif

[[nodiscard]] std::uint64_t HashBytes(const void* data_, std::size_t size_bytes_) noexcept {
    const auto* ptr = static_cast<const std::uint8_t*>(data_);
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0U; i < size_bytes_; ++i) {
        hash ^= static_cast<std::uint64_t>(ptr[i]);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
    hash_ ^= value_ + 0x9e3779b97f4a7c15ULL + (hash_ << 6U) + (hash_ >> 2U);
}

[[nodiscard]] std::string BuildFtErrorMessage(const char* stage_, FT_Error error_) {
    std::ostringstream oss;
    oss << stage_ << " failed: FT_Error(" << static_cast<int>(error_) << ")";

    const char* error_string = FT_Error_String(error_);
    if (error_string != nullptr && error_string[0] != '\0') {
        oss << " " << error_string;
    }
    return oss.str();
}

template<typename LookupVectorT>
[[nodiscard]] std::uint32_t LowerBoundLookupByHash(const LookupVectorT& lookup_,
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

template<typename LookupVectorT>
void InsertLookupNode(LookupVectorT& lookup_,
                      std::uint64_t hash_,
                      std::uint32_t entry_index_) {
    typename LookupVectorT::value_type node{};
    node.hash = hash_;
    node.entry_index = entry_index_;

    const std::uint32_t insert_pos = LowerBoundLookupByHash(lookup_, hash_);
    lookup_.push_back(node);
    for (std::uint32_t i = static_cast<std::uint32_t>(lookup_.size() - 1U); i > insert_pos; --i) {
        lookup_[i] = lookup_[i - 1U];
    }
    lookup_[insert_pos] = node;
}

template<typename LookupVectorT, typename EntryVectorT, typename CandidateT, typename EqualsFnT>
[[nodiscard]] std::uint32_t FindLookupEntryIndex(const LookupVectorT& lookup_,
                                                 const EntryVectorT& entries_,
                                                 std::uint64_t hash_,
                                                 const CandidateT& candidate_,
                                                 EqualsFnT equals_) noexcept {
    const std::uint32_t begin = LowerBoundLookupByHash(lookup_, hash_);
    for (std::uint32_t i = begin; i < lookup_.size(); ++i) {
        const auto& node = lookup_[i];
        if (node.hash != hash_) {
            break;
        }
        if (node.entry_index >= entries_.size()) {
            continue;
        }
        if (equals_(entries_[node.entry_index], candidate_)) {
            return node.entry_index;
        }
    }
    return std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] FT_Render_Mode ToFtRenderMode(GlyphRenderMode render_mode_) noexcept {
    switch (render_mode_) {
        case GlyphRenderMode::normal: return FT_RENDER_MODE_NORMAL;
        case GlyphRenderMode::light: return FT_RENDER_MODE_LIGHT;
        case GlyphRenderMode::mono: return FT_RENDER_MODE_MONO;
        case GlyphRenderMode::lcd: return FT_RENDER_MODE_LCD;
        case GlyphRenderMode::lcd_v: return FT_RENDER_MODE_LCD_V;
        case GlyphRenderMode::sdf:
#ifdef FT_RENDER_MODE_SDF
            return FT_RENDER_MODE_SDF;
#else
            return FT_RENDER_MODE_NORMAL;
#endif
    }
    return FT_RENDER_MODE_NORMAL;
}

[[nodiscard]] GlyphRenderMode NormalizeRenderMode(GlyphRenderMode render_mode_) noexcept {
#ifdef FT_RENDER_MODE_SDF
    return render_mode_;
#else
    if (render_mode_ == GlyphRenderMode::sdf) {
        return GlyphRenderMode::normal;
    }
    return render_mode_;
#endif
}

[[nodiscard]] GlyphPixelMode ToGlyphPixelMode(unsigned int pixel_mode_) noexcept {
    switch (pixel_mode_) {
        case FT_PIXEL_MODE_NONE: return GlyphPixelMode::none;
        case FT_PIXEL_MODE_MONO: return GlyphPixelMode::mono;
        case FT_PIXEL_MODE_GRAY: return GlyphPixelMode::gray;
        case FT_PIXEL_MODE_GRAY2: return GlyphPixelMode::gray2;
        case FT_PIXEL_MODE_GRAY4: return GlyphPixelMode::gray4;
        case FT_PIXEL_MODE_LCD: return GlyphPixelMode::lcd;
        case FT_PIXEL_MODE_LCD_V: return GlyphPixelMode::lcd_v;
        case FT_PIXEL_MODE_BGRA: return GlyphPixelMode::bgra;
#ifdef FT_PIXEL_MODE_SDF
        case FT_PIXEL_MODE_SDF: return GlyphPixelMode::sdf;
#endif
        default: return GlyphPixelMode::unknown;
    }
}

[[nodiscard]] FontFaceMetrics BuildFaceMetrics(FT_Face face_) noexcept {
    FontFaceMetrics metrics{};
    if (face_ == nullptr) {
        return metrics;
    }

    if (face_->size != nullptr) {
        metrics.ascender_26_6 = static_cast<std::int32_t>(face_->size->metrics.ascender);
        metrics.descender_26_6 = static_cast<std::int32_t>(face_->size->metrics.descender);
        metrics.height_26_6 = static_cast<std::int32_t>(face_->size->metrics.height);
        metrics.max_advance_26_6 = static_cast<std::int32_t>(face_->size->metrics.max_advance);
        metrics.x_ppem = static_cast<std::uint16_t>(face_->size->metrics.x_ppem);
        metrics.y_ppem = static_cast<std::uint16_t>(face_->size->metrics.y_ppem);
    }

    metrics.units_per_em = static_cast<std::uint16_t>(face_->units_per_EM);
    metrics.underline_position = static_cast<std::int16_t>(face_->underline_position);
    metrics.underline_thickness = static_cast<std::int16_t>(face_->underline_thickness);
    return metrics;
}

} // namespace

FreeTypeHost::~FreeTypeHost() {
    Shutdown();
}

void FreeTypeHost::Initialize(const FreeTypeHostCreateInfo& create_info_) {
    Shutdown();

    create_info_cache = create_info_;
    if (create_info_cache.max_glyph_cache_count == 0U) {
        create_info_cache.max_glyph_cache_count = 1U;
    }
    if (create_info_cache.max_glyph_blob_bytes == 0U) {
        create_info_cache.max_glyph_blob_bytes = 1U;
    }
    create_info_cache.reserve_glyph_cache_count = std::min(create_info_cache.reserve_glyph_cache_count,
                                                           create_info_cache.max_glyph_cache_count);
    create_info_cache.reserve_glyph_blob_bytes = std::min(create_info_cache.reserve_glyph_blob_bytes,
                                                          create_info_cache.max_glyph_blob_bytes);

    FT_Error error = FT_Init_FreeType(&library);
    if (error != 0) {
        const std::string message = BuildFtErrorMessage("FT_Init_FreeType", error);
        SetLastError(message);
        throw std::runtime_error(message);
    }

    if (create_info_cache.reserve_face_count > 0U) {
        face_entries.reserve(create_info_cache.reserve_face_count);
        face_lookup.reserve(create_info_cache.reserve_face_count);
    }
    if (create_info_cache.reserve_path_blob_bytes > 0U) {
        path_blob.reserve(create_info_cache.reserve_path_blob_bytes);
    }
    if (create_info_cache.reserve_glyph_cache_count > 0U) {
        glyph_cache_entries.reserve(create_info_cache.reserve_glyph_cache_count);
        glyph_lookup.reserve(create_info_cache.reserve_glyph_cache_count);
    }
    if (create_info_cache.reserve_glyph_blob_bytes > 0U) {
        glyph_blob.reserve(create_info_cache.reserve_glyph_blob_bytes);
    }

    stats = {};
    last_error_message.clear();
    initialized = true;
}

void FreeTypeHost::Shutdown() noexcept {
    for (auto& face_entry : face_entries) {
        if (face_entry.face != nullptr) {
            FT_Done_Face(face_entry.face);
            face_entry.face = nullptr;
        }
    }

    if (library != nullptr) {
        FT_Done_FreeType(library);
        library = nullptr;
    }

    face_entries.clear();
    face_lookup.clear();
    path_blob.clear();
    glyph_cache_entries.clear();
    glyph_lookup.clear();
    glyph_blob.clear();
    create_info_cache = {};
    stats = {};
    last_error_message.clear();
    initialized = false;
}

FontFaceId FreeTypeHost::RegisterFace(const FontFaceCreateInfo& create_info_) {
    if (!initialized || library == nullptr) {
        throw std::runtime_error("FreeTypeHost::RegisterFace called before Initialize");
    }
    if (create_info_.file_path.empty()) {
        throw std::invalid_argument("FreeTypeHost::RegisterFace requires non-empty file path");
    }
    if (create_info_.pixel_height == 0U) {
        throw std::invalid_argument("FreeTypeHost::RegisterFace requires pixel_height > 0");
    }

    const std::uint64_t key_hash = [&]() noexcept {
        std::uint64_t hash = HashBytes(create_info_.file_path.data(), create_info_.file_path.size());
        HashCombine(hash, static_cast<std::uint32_t>(create_info_.face_index));
        HashCombine(hash, create_info_.pixel_width);
        HashCombine(hash, create_info_.pixel_height);
        return hash;
    }();

    const std::uint32_t existing_index = FindLookupEntryIndex(
        face_lookup,
        face_entries,
        key_hash,
        create_info_,
        [this](const FaceEntry& face_entry_, const FontFaceCreateInfo& candidate_) noexcept {
            return EqualFaceKey(face_entry_,
                                candidate_.file_path,
                                candidate_.face_index,
                                candidate_.pixel_width,
                                candidate_.pixel_height);
        });

    if (existing_index != std::numeric_limits<std::uint32_t>::max()) {
        ++stats.face_cache_hits;
        FaceEntry& face_entry = face_entries[existing_index];
        if (face_entry.pixel_height != create_info_.pixel_height ||
            face_entry.pixel_width != create_info_.pixel_width) {
            SetFacePixelSizeInternal(face_entry, create_info_.pixel_height, create_info_.pixel_width);
        }
        return MakeFaceId(existing_index);
    }

    TextMcVector<char> path_temp{};
    path_temp.resize(create_info_.file_path.size() + 1U);
    std::memcpy(path_temp.data(),
                create_info_.file_path.data(),
                create_info_.file_path.size());
    path_temp[create_info_.file_path.size()] = '\0';

    FT_Face face = nullptr;
    FT_Error error = FT_New_Face(library,
                                 path_temp.data(),
                                 static_cast<FT_Long>(create_info_.face_index),
                                 &face);
    if (error != 0) {
        const std::string message = BuildFtErrorMessage("FT_New_Face", error);
        SetLastError(message);
        throw std::runtime_error(message);
    }

    const std::size_t path_blob_size = path_blob.size();
    FaceEntry face_entry{};
    face_entry.face = face;
    face_entry.key_hash = key_hash;
    face_entry.face_index = create_info_.face_index;
    face_entry.path_offset = static_cast<std::uint32_t>(path_blob_size);
    face_entry.path_size = static_cast<std::uint32_t>(create_info_.file_path.size());
    face_entry.revision = 1U;

    try {
        const std::size_t projected_path_size = path_blob_size + create_info_.file_path.size();
        if (projected_path_size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error("FreeTypeHost path blob exceeds uint32_t range");
        }

        path_blob.append(create_info_.file_path.data(),
                         create_info_.file_path.size());

        SetFacePixelSizeInternal(face_entry,
                                 create_info_.pixel_height,
                                 create_info_.pixel_width);

        if (face_entries.size() >= std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("FreeTypeHost face registry overflow");
        }

        face_entries.push_back(face_entry);
        const std::uint32_t face_entry_index = static_cast<std::uint32_t>(face_entries.size() - 1U);
        InsertLookupNode(face_lookup, key_hash, face_entry_index);

        ++stats.face_cache_misses;
        stats.face_count = static_cast<std::uint32_t>(face_entries.size());
        return MakeFaceId(face_entry_index);
    } catch (...) {
        path_blob.resize(path_blob_size);
        FT_Done_Face(face);
        throw;
    }
}

FontFaceId FreeTypeHost::AcquireFaceVariant(FontFaceId base_face_id_,
                                            std::uint32_t pixel_height_,
                                            std::uint32_t pixel_width_) {
    if (pixel_height_ == 0U) {
        throw std::invalid_argument("FreeTypeHost::AcquireFaceVariant requires pixel_height > 0");
    }

    const FontFaceDescriptorView descriptor = FaceDescriptor(base_face_id_);
    if (descriptor.file_path.empty()) {
        throw std::runtime_error("FreeTypeHost::AcquireFaceVariant cannot resolve base face path");
    }

    FontFaceCreateInfo create_info{};
    create_info.file_path = descriptor.file_path;
    create_info.face_index = descriptor.face_index;
    create_info.pixel_width = pixel_width_;
    create_info.pixel_height = pixel_height_;
    return RegisterFace(create_info);
}

void FreeTypeHost::SetFacePixelSize(FontFaceId face_id_,
                                    std::uint32_t pixel_height_,
                                    std::uint32_t pixel_width_) {
    FaceEntry& face_entry = AccessFaceEntry(face_id_);
    SetFacePixelSizeInternal(face_entry, pixel_height_, pixel_width_);
}

bool FreeTypeHost::IsFaceIdValid(FontFaceId face_id_) const noexcept {
    if (!initialized || library == nullptr || !face_id_.IsValid()) {
        return false;
    }

    const std::uint32_t face_entry_index = face_id_.value - 1U;
    return face_entry_index < face_entries.size() &&
           face_entries[face_entry_index].face != nullptr;
}

bool FreeTypeHost::SupportsSdfRasterization() const noexcept {
    return k_ft_sdf_supported;
}

std::uint32_t FreeTypeHost::FaceCount() const noexcept {
    return static_cast<std::uint32_t>(face_entries.size());
}

std::uint32_t FreeTypeHost::FaceRevision(FontFaceId face_id_) const {
    return AccessFaceEntry(face_id_).revision;
}

FontFaceDescriptorView FreeTypeHost::FaceDescriptor(FontFaceId face_id_) const {
    const FaceEntry& face_entry = AccessFaceEntry(face_id_);
    FontFaceDescriptorView descriptor{};
    descriptor.file_path = FacePathView(face_entry);
    descriptor.face_index = face_entry.face_index;
    descriptor.pixel_width = face_entry.pixel_width;
    descriptor.pixel_height = face_entry.pixel_height;
    return descriptor;
}

const FontFaceMetrics& FreeTypeHost::FaceMetrics(FontFaceId face_id_) const {
    return AccessFaceEntry(face_id_).metrics;
}

std::uint32_t FreeTypeHost::GlyphIndex(FontFaceId face_id_, std::uint32_t codepoint_) const {
    const FaceEntry& face_entry = AccessFaceEntry(face_id_);
    return static_cast<std::uint32_t>(FT_Get_Char_Index(face_entry.face, codepoint_));
}

bool FreeTypeHost::HasGlyph(FontFaceId face_id_, std::uint32_t codepoint_) const {
    return GlyphIndex(face_id_, codepoint_) != 0U;
}

std::int32_t FreeTypeHost::KerningX26_6(FontFaceId face_id_,
                                        std::uint32_t left_codepoint_,
                                        std::uint32_t right_codepoint_) const {
    const FaceEntry& face_entry = AccessFaceEntry(face_id_);
    if (face_entry.face == nullptr || FT_HAS_KERNING(face_entry.face) == 0) {
        return 0;
    }

    const FT_UInt left_index = FT_Get_Char_Index(face_entry.face, left_codepoint_);
    const FT_UInt right_index = FT_Get_Char_Index(face_entry.face, right_codepoint_);
    if (left_index == 0U || right_index == 0U) {
        return 0;
    }

    FT_Vector delta{};
    const FT_Error error = FT_Get_Kerning(face_entry.face,
                                          left_index,
                                          right_index,
                                          FT_KERNING_DEFAULT,
                                          &delta);
    if (error != 0) {
        return 0;
    }
    return static_cast<std::int32_t>(delta.x);
}

GlyphBitmapView FreeTypeHost::RasterizeGlyph(const GlyphRasterRequest& request_) {
    FaceEntry& face_entry = AccessFaceEntry(request_.face_id);
    const GlyphRenderMode effective_render_mode = NormalizeRenderMode(request_.render_mode);

    GlyphCacheKey key{};
    key.face_id_value = request_.face_id.value;
    key.face_revision = face_entry.revision;
    key.codepoint = request_.codepoint;
    key.load_flags = request_.load_flags;
    key.render_mode = static_cast<std::uint8_t>(effective_render_mode);
    const auto hash_glyph_cache_key = [](const GlyphCacheKey& glyph_key_) noexcept {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        HashCombine(hash, glyph_key_.face_id_value);
        HashCombine(hash, glyph_key_.face_revision);
        HashCombine(hash, glyph_key_.codepoint);
        HashCombine(hash, static_cast<std::uint32_t>(glyph_key_.load_flags));
        HashCombine(hash, glyph_key_.render_mode);
        return hash;
    };

    const auto equal_glyph_cache_key = [](const GlyphCacheKey& lhs_,
                                          const GlyphCacheKey& rhs_) noexcept {
        return lhs_.face_id_value == rhs_.face_id_value &&
               lhs_.face_revision == rhs_.face_revision &&
               lhs_.codepoint == rhs_.codepoint &&
               lhs_.load_flags == rhs_.load_flags &&
               lhs_.render_mode == rhs_.render_mode;
    };

    const std::uint64_t key_hash = hash_glyph_cache_key(key);

    const std::uint32_t existing_index = FindLookupEntryIndex(
        glyph_lookup,
        glyph_cache_entries,
        key_hash,
        key,
        [equal_glyph_cache_key](const GlyphCacheEntry& glyph_entry_, const GlyphCacheKey& candidate_) noexcept {
            return equal_glyph_cache_key(glyph_entry_.key, candidate_);
        });

    if (existing_index != std::numeric_limits<std::uint32_t>::max()) {
        ++stats.glyph_cache_hits;
        return BuildGlyphView(glyph_cache_entries[existing_index]);
    }

    ++stats.glyph_cache_misses;

    const std::uint32_t glyph_index = static_cast<std::uint32_t>(
        FT_Get_Char_Index(face_entry.face, request_.codepoint));

    if (glyph_index == 0U && request_.codepoint != 0U) {
        EnsureGlyphCacheCapacity(0U);
        glyph_cache_entries.push_back({
            .hash = key_hash,
            .key = key,
            .glyph_index = 0U,
            .blob_offset = 0U,
            .blob_size = 0U,
            .width = 0U,
            .rows = 0U,
            .pitch = 0U,
            .bearing_left = 0,
            .bearing_top = 0,
            .advance_x_26_6 = 0,
            .advance_y_26_6 = 0,
            .pixel_mode = GlyphPixelMode::none
        });
        const std::uint32_t glyph_entry_index = static_cast<std::uint32_t>(glyph_cache_entries.size() - 1U);
        InsertLookupNode(glyph_lookup, key_hash, glyph_entry_index);
        RefreshStatsCacheFootprint();
        return BuildGlyphView(glyph_cache_entries[glyph_entry_index]);
    }

    FT_Error error = FT_Load_Glyph(face_entry.face,
                                   static_cast<FT_UInt>(glyph_index),
                                   request_.load_flags);
    if (error != 0) {
        const std::string message = BuildFtErrorMessage("FT_Load_Glyph", error);
        SetLastError(message);
        throw std::runtime_error(message);
    }

    FT_GlyphSlot glyph_slot = face_entry.face->glyph;
    if (glyph_slot == nullptr) {
        throw std::runtime_error("FreeTypeHost glyph slot is null after FT_Load_Glyph");
    }

    if (glyph_slot->format != FT_GLYPH_FORMAT_BITMAP) {
        error = FT_Render_Glyph(glyph_slot, ToFtRenderMode(effective_render_mode));
        if (error != 0) {
            const std::string message = BuildFtErrorMessage("FT_Render_Glyph", error);
            SetLastError(message);
            throw std::runtime_error(message);
        }
    }

    const FT_Bitmap& bitmap = glyph_slot->bitmap;
    const std::uint32_t rows = static_cast<std::uint32_t>(bitmap.rows);
    const std::int32_t pitch_signed = bitmap.pitch;
    const std::uint32_t pitch = (pitch_signed >= 0)
        ? static_cast<std::uint32_t>(pitch_signed)
        : static_cast<std::uint32_t>(-pitch_signed);

    const std::uint64_t blob_size_u64 = static_cast<std::uint64_t>(rows) * pitch;
    if (blob_size_u64 > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error("FreeTypeHost glyph bitmap exceeds uint32_t cache size");
    }
    const std::uint32_t blob_size = static_cast<std::uint32_t>(blob_size_u64);

    EnsureGlyphCacheCapacity(blob_size);

    const std::size_t blob_offset = glyph_blob.size();
    const std::size_t resized_blob_size = blob_offset + blob_size;
    if (resized_blob_size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error("FreeTypeHost glyph blob exceeds uint32_t addressing range");
    }
    glyph_blob.resize(resized_blob_size);

    if (blob_size > 0U) {
        std::uint8_t* dst = glyph_blob.data() + blob_offset;
        if (bitmap.buffer != nullptr && pitch > 0U) {
            for (std::uint32_t y = 0U; y < rows; ++y) {
                const std::uint8_t* src_row = nullptr;
                if (pitch_signed >= 0) {
                    src_row = bitmap.buffer + static_cast<std::size_t>(y) * pitch;
                } else {
                    src_row = bitmap.buffer + static_cast<std::size_t>(rows - 1U - y) * pitch;
                }
                std::memcpy(dst + static_cast<std::size_t>(y) * pitch,
                            src_row,
                            pitch);
            }
        } else {
            std::memset(dst, 0, blob_size);
        }
    }

    glyph_cache_entries.push_back({
        .hash = key_hash,
        .key = key,
        .glyph_index = glyph_index,
        .blob_offset = static_cast<std::uint32_t>(blob_offset),
        .blob_size = blob_size,
        .width = static_cast<std::uint32_t>(bitmap.width),
        .rows = rows,
        .pitch = pitch,
        .bearing_left = static_cast<std::int32_t>(glyph_slot->bitmap_left),
        .bearing_top = static_cast<std::int32_t>(glyph_slot->bitmap_top),
        .advance_x_26_6 = static_cast<std::int32_t>(glyph_slot->advance.x),
        .advance_y_26_6 = static_cast<std::int32_t>(glyph_slot->advance.y),
        .pixel_mode = ToGlyphPixelMode(bitmap.pixel_mode)
    });
    const std::uint32_t glyph_entry_index = static_cast<std::uint32_t>(glyph_cache_entries.size() - 1U);
    InsertLookupNode(glyph_lookup, key_hash, glyph_entry_index);
    RefreshStatsCacheFootprint();
    return BuildGlyphView(glyph_cache_entries[glyph_entry_index]);
}

void FreeTypeHost::ClearGlyphCache() noexcept {
    glyph_cache_entries.clear();
    glyph_lookup.clear();
    glyph_blob.clear();
    RefreshStatsCacheFootprint();
}

bool FreeTypeHost::IsInitialized() const noexcept {
    return initialized;
}

const FreeTypeHostStats& FreeTypeHost::Stats() const noexcept {
    return stats;
}

std::string_view FreeTypeHost::LastErrorMessage() const noexcept {
    return last_error_message;
}

std::uint32_t FreeTypeHost::FaceIdToIndex(std::uint32_t face_id_value_) {
    if (face_id_value_ == 0U) {
        throw std::runtime_error("FreeTypeHost face id must be non-zero");
    }
    return face_id_value_ - 1U;
}

FontFaceId FreeTypeHost::MakeFaceId(std::uint32_t face_entry_index_) {
    FontFaceId face_id{};
    face_id.value = face_entry_index_ + 1U;
    return face_id;
}

FreeTypeHost::FaceEntry& FreeTypeHost::AccessFaceEntry(FontFaceId face_id_) {
    if (!initialized || library == nullptr) {
        throw std::runtime_error("FreeTypeHost face access before Initialize");
    }
    const std::uint32_t face_entry_index = FaceIdToIndex(face_id_.value);
    if (face_entry_index >= face_entries.size()) {
        throw std::out_of_range("FreeTypeHost face id out of range");
    }
    return face_entries[face_entry_index];
}

const FreeTypeHost::FaceEntry& FreeTypeHost::AccessFaceEntry(FontFaceId face_id_) const {
    if (!initialized || library == nullptr) {
        throw std::runtime_error("FreeTypeHost face access before Initialize");
    }
    const std::uint32_t face_entry_index = FaceIdToIndex(face_id_.value);
    if (face_entry_index >= face_entries.size()) {
        throw std::out_of_range("FreeTypeHost face id out of range");
    }
    return face_entries[face_entry_index];
}

std::string_view FreeTypeHost::FacePathView(const FaceEntry& face_entry_) const noexcept {
    if (face_entry_.path_size == 0U || path_blob.empty()) {
        return {};
    }
    return std::string_view(path_blob.data() + face_entry_.path_offset,
                            face_entry_.path_size);
}

bool FreeTypeHost::EqualFaceKey(const FaceEntry& face_entry_,
                                std::string_view file_path_,
                                std::int32_t face_index_,
                                std::uint32_t pixel_width_,
                                std::uint32_t pixel_height_) const noexcept {
    if (face_entry_.face_index != face_index_ ||
        face_entry_.pixel_width != pixel_width_ ||
        face_entry_.pixel_height != pixel_height_) {
        return false;
    }
    const std::string_view existing_path = FacePathView(face_entry_);
    return existing_path == file_path_;
}

void FreeTypeHost::SetFacePixelSizeInternal(FaceEntry& face_entry_,
                                            std::uint32_t pixel_height_,
                                            std::uint32_t pixel_width_) {
    if (pixel_height_ == 0U) {
        throw std::invalid_argument("FreeTypeHost::SetFacePixelSize requires pixel_height > 0");
    }

    if (face_entry_.pixel_height == pixel_height_ &&
        face_entry_.pixel_width == pixel_width_) {
        return;
    }

    FT_Error error = FT_Set_Pixel_Sizes(face_entry_.face,
                                        static_cast<FT_UInt>(pixel_width_),
                                        static_cast<FT_UInt>(pixel_height_));
    if (error != 0) {
        const std::string message = BuildFtErrorMessage("FT_Set_Pixel_Sizes", error);
        SetLastError(message);
        throw std::runtime_error(message);
    }

    face_entry_.pixel_width = pixel_width_;
    face_entry_.pixel_height = pixel_height_;
    ++face_entry_.revision;
    if (face_entry_.revision == 0U) {
        face_entry_.revision = 1U;
        ClearGlyphCache();
    }
    face_entry_.metrics = BuildFaceMetrics(face_entry_.face);
}

GlyphBitmapView FreeTypeHost::BuildGlyphView(const GlyphCacheEntry& glyph_entry_) const noexcept {
    GlyphBitmapView view{};
    if (glyph_entry_.blob_size > 0U &&
        glyph_entry_.blob_offset < glyph_blob.size() &&
        glyph_entry_.blob_offset + glyph_entry_.blob_size <= glyph_blob.size()) {
        view.pixels = glyph_blob.data() + glyph_entry_.blob_offset;
    }
    view.size_bytes = glyph_entry_.blob_size;
    view.width = glyph_entry_.width;
    view.rows = glyph_entry_.rows;
    view.pitch = glyph_entry_.pitch;
    view.bearing_left = glyph_entry_.bearing_left;
    view.bearing_top = glyph_entry_.bearing_top;
    view.advance_x_26_6 = glyph_entry_.advance_x_26_6;
    view.advance_y_26_6 = glyph_entry_.advance_y_26_6;
    view.glyph_index = glyph_entry_.glyph_index;
    view.pixel_mode = glyph_entry_.pixel_mode;
    return view;
}

void FreeTypeHost::EnsureGlyphCacheCapacity(std::uint32_t incoming_blob_bytes_) {
    const std::size_t max_cache_entries = static_cast<std::size_t>(create_info_cache.max_glyph_cache_count);
    const std::size_t max_blob_bytes = static_cast<std::size_t>(create_info_cache.max_glyph_blob_bytes);

    if (incoming_blob_bytes_ > max_blob_bytes) {
        throw std::runtime_error("FreeTypeHost incoming glyph bitmap exceeds max_glyph_blob_bytes");
    }

    const bool exceeds_entry_limit = glyph_cache_entries.size() >= max_cache_entries;
    const bool exceeds_blob_limit = glyph_blob.size() > max_blob_bytes - incoming_blob_bytes_;
    if (exceeds_entry_limit || exceeds_blob_limit) {
        ClearGlyphCache();
    }
}

void FreeTypeHost::RefreshStatsCacheFootprint() noexcept {
    stats.glyph_cache_entries = static_cast<std::uint32_t>(
        std::min<std::size_t>(glyph_cache_entries.size(),
                              static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
    stats.glyph_blob_bytes = static_cast<std::uint32_t>(
        std::min<std::size_t>(glyph_blob.size(),
                              static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
}

void FreeTypeHost::SetLastError(std::string message_) {
    last_error_message = std::move(message_);
}

} // namespace vr::text
