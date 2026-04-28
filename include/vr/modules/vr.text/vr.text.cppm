module;
// Global module fragment
#include "vr/detail/vr_module_fwd.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

struct FT_LibraryRec_;
struct FT_FaceRec_;
using FT_Library = FT_LibraryRec_*;
using FT_Face = FT_FaceRec_*;

export module vr.text;
import vr.types;
import vr.context;
import vr.resource;
import vr.render;
import vr.ecs;

export {
namespace vr::text {

// --- freetype_host.hpp --------------------------------------------------------

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

    vr::McVector<FaceEntry> face_entries{};
    vr::McVector<FaceLookupNode> face_lookup{};
    vr::McVector<char> path_blob{};

    vr::McVector<GlyphCacheEntry> glyph_cache_entries{};
    vr::McVector<GlyphLookupNode> glyph_lookup{};
    vr::McVector<std::uint8_t> glyph_blob{};

    FreeTypeHostCreateInfo create_info_cache{};
    FreeTypeHostStats stats{};
    std::string last_error_message{};
    bool initialized = false;
};

// --- glyph_atlas_host.hpp -----------------------------------------------------

struct GlyphAtlasCreateInfo {
    std::uint32_t page_width = 2048U;
    std::uint32_t page_height = 2048U;
    std::uint32_t max_page_count = 16U;
    std::uint32_t glyph_padding = 2U;
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
    [[nodiscard]] const vr::McVector<GlyphRectU16>& PageDirtyRects(std::uint32_t page_index_) const;

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
        vr::McVector<std::uint8_t> pixels{};
        vr::McVector<Shelf> shelves{};
        vr::McVector<GlyphRectU16> dirty_rects{};
    };

    struct AllocationResult {
        bool allocated = false;
        std::uint32_t page_index = 0U;
        GlyphRectU16 rect{};
        GlyphUvRect uv{};
    };

    [[nodiscard]] static std::uint64_t HashGlyphKey(const GlyphKey& key_) noexcept;
    [[nodiscard]] static bool EqualGlyphKey(const GlyphKey& lhs_, const GlyphKey& rhs_) noexcept;

    [[nodiscard]] static std::uint32_t LowerBoundFontMap(const vr::McVector<FontMapEntry>& font_map_,
                                                         std::uint32_t font_id_) noexcept;
    [[nodiscard]] static std::uint32_t LowerBoundLookup(const vr::McVector<GlyphLookupNode>& lookup_,
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

    vr::McVector<FontMapEntry> font_map{};
    vr::McVector<GlyphEntry> glyph_entries{};
    vr::McVector<GlyphLookupNode> glyph_lookup{};
    vr::McVector<AtlasPage> pages{};

    GlyphAtlasHostStats stats{};
    bool initialized = false;
};

// --- glyph_upload_host.hpp ----------------------------------------------------

struct GlyphUploadHostCreateInfo {
    VkFormat atlas_format = VK_FORMAT_R8_UNORM;
    VkImageUsageFlags image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkImageLayout shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bool prefer_shader_read_layout = true;
    bool force_general_layout = false;
    bool use_linear_sampler = true;
    bool clamp_to_edge = true;
    std::uint32_t reserve_page_count = 8U;
};

struct GlyphUploadHostStats {
    std::uint32_t page_count = 0U;
    std::uint32_t uploaded_page_count = 0U;
    std::uint32_t uploaded_rect_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    std::uint32_t barrier_count = 0U;
    std::uint32_t skipped_clean_page_count = 0U;
};

class GlyphUploadHost final {
public:
    GlyphUploadHost() = default;
    ~GlyphUploadHost() = default;

    GlyphUploadHost(const GlyphUploadHost&) = delete;
    GlyphUploadHost& operator=(const GlyphUploadHost&) = delete;

    GlyphUploadHost(GlyphUploadHost&&) = delete;
    GlyphUploadHost& operator=(GlyphUploadHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    resource::SamplerHost& sampler_host_,
                    const GlyphUploadHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void UploadDirtyPages(VulkanContext& context_,
                          render::UploadHost& upload_host_,
                          std::uint32_t frame_index_,
                          GlyphAtlasHost& atlas_host_);

    [[nodiscard]] VkImageView PageImageView(std::uint32_t page_index_) const;
    [[nodiscard]] VkImage PageImage(std::uint32_t page_index_) const;
    [[nodiscard]] VkImageLayout PageShaderLayout(std::uint32_t page_index_) const;
    [[nodiscard]] VkSampler Sampler() const;
    [[nodiscard]] resource::SamplerId SamplerId() const noexcept;
    [[nodiscard]] std::uint32_t PageCount() const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GlyphUploadHostStats& Stats() const noexcept;

private:
    struct PageResource {
        resource::ImageResource image{};
        VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        std::uint32_t generation = 0U;
    };

    [[nodiscard]] bool ShouldUseGeneralLayout(const VulkanContext& context_) const noexcept;
    void EnsurePageResources(VulkanContext& context_, const GlyphAtlasHost& atlas_host_);
    void DestroyPageResources(VulkanContext& context_) noexcept;
    void TransitionImageLayoutIfNeeded(render::UploadHost& upload_host_,
                                       std::uint32_t frame_index_,
                                       PageResource& page_resource_,
                                       VkImageLayout new_layout_,
                                       std::uint32_t& barrier_count_) const;

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;

    GlyphUploadHostCreateInfo create_info_cache{};
    vr::McVector<PageResource> pages{};
    vr::McVector<std::uint8_t> rect_upload_scratch{};

    resource::SamplerId sampler_id{};
    GlyphUploadHostStats stats{};
    bool initialized = false;
};

} // namespace vr::text
} // export

// Now add ECS text types that use text module types
export {
namespace vr::ecs {

// --- text_component.hpp -------------------------------------------------------

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

static_assert(PurePodComponent<TextBufferInlineUtf8>);
static_assert(PurePodComponent<TextRuntimeBatchData>);
static_assert(PurePodComponent<TextStyle2D>);
static_assert(PurePodComponent<TextStyle3D>);
static_assert(PurePodComponent<Text<Dim2>>);
static_assert(PurePodComponent<Text<Dim3>>);
static_assert(sizeof(TextRuntimeBatchData) <= 64U);
static_assert(alignof(TextRuntimeBatchData) <= 8U);

// --- text_system.hpp ----------------------------------------------------------

template<DimensionTag DimensionT>
class TextSystem final {
public:
    using TextType = Text<DimensionT>;
    using StyleType = typename TextType::StyleType;

    static constexpr std::uint32_t inline_capacity_bytes = TextBufferInlineUtf8::inline_capacity_bytes;

    // 64-bit sort key layout (MSB -> LSB):
    // [pass:2][material:16][font:12][atlas:10][minor:16][batch_tag:8]
    static constexpr std::uint32_t sort_key_batch_bits = 8U;
    static constexpr std::uint32_t sort_key_minor_bits = 16U;
    static constexpr std::uint32_t sort_key_atlas_bits = 10U;
    static constexpr std::uint32_t sort_key_font_bits = 12U;
    static constexpr std::uint32_t sort_key_material_bits = 16U;
    static constexpr std::uint32_t sort_key_pass_bits = 2U;

    static constexpr std::uint32_t sort_key_batch_shift = 0U;
    static constexpr std::uint32_t sort_key_minor_shift = sort_key_batch_shift + sort_key_batch_bits;
    static constexpr std::uint32_t sort_key_atlas_shift = sort_key_minor_shift + sort_key_minor_bits;
    static constexpr std::uint32_t sort_key_font_shift = sort_key_atlas_shift + sort_key_atlas_bits;
    static constexpr std::uint32_t sort_key_material_shift = sort_key_font_shift + sort_key_font_bits;
    static constexpr std::uint32_t sort_key_pass_shift = sort_key_material_shift + sort_key_material_bits;

    static constexpr std::uint32_t sort_key_binding_shift = sort_key_atlas_shift;

    static constexpr std::uint64_t sort_key_batch_mask = (std::uint64_t{1U} << sort_key_batch_bits) - 1U;
    static constexpr std::uint64_t sort_key_minor_mask = (std::uint64_t{1U} << sort_key_minor_bits) - 1U;
    static constexpr std::uint64_t sort_key_atlas_mask = (std::uint64_t{1U} << sort_key_atlas_bits) - 1U;
    static constexpr std::uint64_t sort_key_font_mask = (std::uint64_t{1U} << sort_key_font_bits) - 1U;
    static constexpr std::uint64_t sort_key_material_mask = (std::uint64_t{1U} << sort_key_material_bits) - 1U;
    static constexpr std::uint64_t sort_key_pass_mask = (std::uint64_t{1U} << sort_key_pass_bits) - 1U;

    static_assert(sort_key_pass_bits + sort_key_material_bits + sort_key_font_bits +
                      sort_key_atlas_bits + sort_key_minor_bits + sort_key_batch_bits == 64U,
                  "TextSystem sort-key bit layout must be exactly 64 bits");

    static void Initialize(TextType& component_) noexcept {
        component_.text.size_bytes = 0U;
        component_.text.capacity_bytes = inline_capacity_bytes;
        component_.text.revision = 0U;
        component_.text.reserved = 0U;
        std::memset(component_.text.utf8, 0, sizeof(component_.text.utf8));
        SetDefaultStyle(component_);
        SetDefaultRuntime(component_);
        RebuildSortKey(component_);
    }

    static void SetDefaultStyle(TextType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.pixel_size = 18.0F;
            component_.style.line_spacing = 1.0F;
            component_.style.letter_spacing = 0.0F;
            component_.style.horizontal_align = TextHorizontalAlign::left;
            component_.style.vertical_align = TextVerticalAlign::top;
            component_.style.color = Rgba8{255U, 255U, 255U, 255U};
            component_.style.layer = 0;
            component_.style.enable_sdf = 1U;
            component_.style.enable_outline = 0U;
            component_.style.outline_width_px = 0U;
            component_.style.reserved0 = 0U;
            component_.style.outline_color = Rgba8{0U, 0U, 0U, 255U};
        } else {
            component_.style.world_size = 0.25F;
            component_.style.max_screen_size_px = 144.0F;
            component_.style.line_spacing = 1.0F;
            component_.style.letter_spacing = 0.0F;
            component_.style.horizontal_align = TextHorizontalAlign::center;
            component_.style.vertical_align = TextVerticalAlign::center;
            component_.style.color = Rgba8{255U, 255U, 255U, 255U};
            component_.style.billboard = 1U;
            component_.style.depth_test = 1U;
            component_.style.depth_write = 0U;
            component_.style.enable_sdf = 1U;
            component_.style.enable_outline = 0U;
            component_.style.outline_width_px = 0U;
            component_.style.reserved0 = 0U;
            component_.style.reserved1 = 0U;
            component_.style.outline_color = Rgba8{0U, 0U, 0U, 255U};
        }

        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
        RebuildSortKey(component_);
    }

    static void SetDefaultRuntime(TextType& component_) noexcept {
        component_.runtime.sort_key = 0U;
        component_.runtime.font_id = 0U;
        component_.runtime.material_id = 0U;
        component_.runtime.atlas_page_id = 0U;
        component_.runtime.glyph_begin = 0U;
        component_.runtime.glyph_count = 0U;
        component_.runtime.batch_tag = 0U;
        component_.runtime.user_data = 0U;
        component_.runtime.depth_bin = 0U;
        component_.runtime.visible = 1U;
        component_.runtime.pass_hint = TextRenderPassHint::overlay;
        component_.runtime.dirty_flags = text_dirty_flag | style_dirty_flag | runtime_dirty_flag;
    }

    static void SetVisible(TextType& component_, bool visible_) noexcept {
        component_.runtime.visible = visible_ ? 1U : 0U;
        MarkDirty(component_, runtime_dirty_flag);
    }

    static void SetRenderPassHint(TextType& component_,
                                  TextRenderPassHint pass_hint_) noexcept {
        component_.runtime.pass_hint = pass_hint_;
        MarkDirty(component_, runtime_dirty_flag);
        RebuildSortKey(component_);
    }

    static void SetRuntimeRoute(TextType& component_,
                                std::uint32_t font_id_,
                                std::uint32_t material_id_,
                                std::uint32_t atlas_page_id_,
                                std::uint32_t batch_tag_) noexcept {
        component_.runtime.font_id = font_id_;
        component_.runtime.material_id = material_id_;
        component_.runtime.atlas_page_id = atlas_page_id_;
        component_.runtime.batch_tag = batch_tag_;
        MarkDirty(component_, runtime_dirty_flag);
        RebuildSortKey(component_);
    }

    static void SetFontId(TextType& component_, std::uint32_t font_id_) noexcept {
        component_.runtime.font_id = font_id_;
        MarkDirty(component_, runtime_dirty_flag);
        RebuildSortKey(component_);
    }

    static void SetMaterialId(TextType& component_,
                              std::uint32_t material_id_) noexcept {
        component_.runtime.material_id = material_id_;
        MarkDirty(component_, runtime_dirty_flag);
        RebuildSortKey(component_);
    }

    static void SetAtlasPageId(TextType& component_,
                               std::uint32_t atlas_page_id_) noexcept {
        component_.runtime.atlas_page_id = atlas_page_id_;
        MarkDirty(component_, runtime_dirty_flag);
        RebuildSortKey(component_);
    }

    static void SetGlyphRange(TextType& component_,
                              std::uint32_t glyph_begin_,
                              std::uint32_t glyph_count_) noexcept {
        component_.runtime.glyph_begin = glyph_begin_;
        component_.runtime.glyph_count = glyph_count_;
        MarkDirty(component_, runtime_dirty_flag);
    }

    static void SetBatchTag(TextType& component_, std::uint32_t batch_tag_) noexcept {
        component_.runtime.batch_tag = batch_tag_;
        MarkDirty(component_, runtime_dirty_flag);
        RebuildSortKey(component_);
    }

    static void SetUserData(TextType& component_, std::uint32_t user_data_) noexcept {
        component_.runtime.user_data = user_data_;
        MarkDirty(component_, runtime_dirty_flag);
    }

    static void SetDepthBin(TextType& component_, std::uint16_t depth_bin_) noexcept {
        component_.runtime.depth_bin = depth_bin_;
        MarkDirty(component_, runtime_dirty_flag);
        RebuildSortKey(component_);
    }

    static void SetSortKeyOverride(TextType& component_,
                                   std::uint64_t sort_key_) noexcept {
        component_.runtime.sort_key = sort_key_;
        MarkDirty(component_, runtime_dirty_flag);
    }

    [[nodiscard]] static std::uint64_t ComposeSortKey(const TextType& component_) noexcept {
        const std::uint64_t pass_bits = static_cast<std::uint64_t>(component_.runtime.pass_hint) & sort_key_pass_mask;
        const std::uint64_t material_bits = static_cast<std::uint64_t>(component_.runtime.material_id) & sort_key_material_mask;
        const std::uint64_t font_bits = static_cast<std::uint64_t>(component_.runtime.font_id) & sort_key_font_mask;
        const std::uint64_t atlas_bits = static_cast<std::uint64_t>(component_.runtime.atlas_page_id) & sort_key_atlas_mask;
        const std::uint64_t batch_bits = static_cast<std::uint64_t>(component_.runtime.batch_tag) & sort_key_batch_mask;

        std::uint64_t minor_bits = 0U;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const std::int32_t shifted_layer = static_cast<std::int32_t>(component_.style.layer) -
                                               static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min());
            minor_bits = static_cast<std::uint64_t>(static_cast<std::uint32_t>(shifted_layer)) & sort_key_minor_mask;
        } else {
            minor_bits = static_cast<std::uint64_t>(component_.runtime.depth_bin) & sort_key_minor_mask;
        }

        std::uint64_t key = 0U;
        key |= (pass_bits << sort_key_pass_shift);
        key |= (material_bits << sort_key_material_shift);
        key |= (font_bits << sort_key_font_shift);
        key |= (atlas_bits << sort_key_atlas_shift);
        key |= (minor_bits << sort_key_minor_shift);
        key |= (batch_bits << sort_key_batch_shift);
        return key;
    }

    static void RebuildSortKey(TextType& component_) noexcept {
        component_.runtime.sort_key = ComposeSortKey(component_);
    }

    [[nodiscard]] static std::uint64_t BindingSortKey(const TextType& component_) noexcept {
        return BindingSortKey(component_.runtime.sort_key);
    }

    [[nodiscard]] static std::uint64_t BindingSortKey(std::uint64_t sort_key_) noexcept {
        return sort_key_ >> sort_key_binding_shift;
    }

    [[nodiscard]] static std::uint32_t ExtractPassBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_pass_shift) & sort_key_pass_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractMaterialBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_material_shift) & sort_key_material_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractFontBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_font_shift) & sort_key_font_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractAtlasBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_atlas_shift) & sort_key_atlas_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractMinorBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_minor_shift) & sort_key_minor_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractBatchBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_batch_shift) & sort_key_batch_mask);
    }

    [[nodiscard]] static bool IsVisibleForBatch(const TextType& component_) noexcept {
        return component_.runtime.visible != 0U && component_.text.size_bytes > 0U;
    }

    [[nodiscard]] static std::uint64_t SortKey(const TextType& component_) noexcept {
        return component_.runtime.sort_key;
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const TextType& component_) noexcept {
        return component_.runtime.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const TextType& component_, std::uint32_t mask_) noexcept {
        return (component_.runtime.dirty_flags & mask_) != 0U;
    }

    static void MarkDirty(TextType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.runtime.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(TextType& component_, std::uint32_t clear_mask_) noexcept {
        component_.runtime.dirty_flags &= ~clear_mask_;
    }

    static bool SetText(TextType& component_, std::string_view text_) noexcept {
        if (!EnsureInlineCapacityOrRecover(component_)) {
            return false;
        }

        const std::size_t new_size_size_t = text_.size();
        const std::uint32_t capacity_bytes = component_.text.capacity_bytes;
        if (new_size_size_t > static_cast<std::size_t>(capacity_bytes)) {
            return false;
        }

        const std::uint32_t new_size = static_cast<std::uint32_t>(new_size_size_t);
        const std::uint32_t current_size = component_.text.size_bytes;
        if (new_size == current_size) {
            if (new_size == 0U) {
                return true;
            }
            if (BytesEqual(component_.text.utf8,
                           text_.data(),
                           new_size)) {
                return true;
            }
        }

        if (new_size > 0U) {
            std::memcpy(component_.text.utf8,
                        text_.data(),
                        static_cast<std::size_t>(new_size));
        }
        if (new_size < capacity_bytes) {
            component_.text.utf8[new_size] = '\0';
        }
        component_.text.size_bytes = new_size;
        ++component_.text.revision;
        MarkDirty(component_, text_dirty_flag | runtime_dirty_flag);
        return true;
    }

    static bool AppendText(TextType& component_, std::string_view text_) noexcept {
        if (text_.empty()) {
            return true;
        }
        if (!EnsureInlineCapacityOrRecover(component_)) {
            return false;
        }

        const std::size_t append_size_size_t = text_.size();
        const std::uint32_t capacity_bytes = component_.text.capacity_bytes;
        const std::uint32_t current_size = component_.text.size_bytes;
        if (current_size > capacity_bytes) {
            return false;
        }
        const std::size_t remaining_size = static_cast<std::size_t>(capacity_bytes - current_size);
        if (append_size_size_t > remaining_size) {
            return false;
        }

        const std::uint32_t append_size = static_cast<std::uint32_t>(append_size_size_t);
        const std::uint32_t dst_offset = current_size;
        std::memcpy(component_.text.utf8 + dst_offset,
                    text_.data(),
                    static_cast<std::size_t>(append_size));
        const std::uint32_t merged_size = current_size + append_size;
        component_.text.size_bytes = merged_size;
        if (merged_size < capacity_bytes) {
            component_.text.utf8[merged_size] = '\0';
        }
        ++component_.text.revision;
        MarkDirty(component_, text_dirty_flag | runtime_dirty_flag);
        return true;
    }

    static void ClearText(TextType& component_) noexcept {
        if (!EnsureInlineCapacityOrRecover(component_)) {
            return;
        }
        if (component_.text.size_bytes == 0U) {
            return;
        }

        component_.text.size_bytes = 0U;
        component_.text.utf8[0] = '\0';
        ++component_.text.revision;
        MarkDirty(component_, text_dirty_flag | runtime_dirty_flag);
    }

    [[nodiscard]] static std::string_view GetText(const TextType& component_) noexcept {
        if (component_.text.size_bytes == 0U) {
            return {};
        }
        return std::string_view(component_.text.utf8,
                                static_cast<std::size_t>(component_.text.size_bytes));
    }

    [[nodiscard]] static std::uint32_t TextSizeBytes(const TextType& component_) noexcept {
        return component_.text.size_bytes;
    }

    [[nodiscard]] static std::uint32_t Revision(const TextType& component_) noexcept {
        return component_.text.revision;
    }

    static void SetColor(TextType& component_, Rgba8 color_) noexcept {
        component_.style.color = color_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetHorizontalAlign(TextType& component_,
                                   TextHorizontalAlign horizontal_align_) noexcept {
        component_.style.horizontal_align = horizontal_align_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetVerticalAlign(TextType& component_,
                                 TextVerticalAlign vertical_align_) noexcept {
        component_.style.vertical_align = vertical_align_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetLineSpacing(TextType& component_, float line_spacing_) noexcept {
        component_.style.line_spacing = line_spacing_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetLetterSpacing(TextType& component_, float letter_spacing_) noexcept {
        component_.style.letter_spacing = letter_spacing_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetOutlineEnabled(TextType& component_, bool enabled_) noexcept {
        component_.style.enable_outline = enabled_ ? 1U : 0U;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetOutlineWidthPx(TextType& component_, std::uint8_t outline_width_px_) noexcept {
        component_.style.outline_width_px = outline_width_px_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetOutlineColor(TextType& component_, Rgba8 outline_color_) noexcept {
        component_.style.outline_color = outline_color_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetSdfEnabled(TextType& component_, bool enabled_) noexcept {
        component_.style.enable_sdf = enabled_ ? 1U : 0U;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetPixelSize(TextType& component_, float pixel_size_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        component_.style.pixel_size = pixel_size_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetLayer(TextType& component_, std::int16_t layer_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        component_.style.layer = layer_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
        RebuildSortKey(component_);
    }

    static void SetWorldSize(TextType& component_, float world_size_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.world_size = world_size_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetMaxScreenSizePx(TextType& component_, float max_screen_size_px_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.max_screen_size_px = max_screen_size_px_;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetBillboard(TextType& component_, bool enabled_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.billboard = enabled_ ? 1U : 0U;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetDepthTest(TextType& component_, bool enabled_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.depth_test = enabled_ ? 1U : 0U;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

    static void SetDepthWrite(TextType& component_, bool enabled_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.depth_write = enabled_ ? 1U : 0U;
        MarkDirty(component_, style_dirty_flag | runtime_dirty_flag);
    }

private:
    [[nodiscard]] static bool EnsureInlineCapacityOrRecover(TextType& component_) noexcept {
        if (component_.text.capacity_bytes == inline_capacity_bytes) {
            return true;
        }
        return EnsureCapacityInitialized(component_);
    }

    [[nodiscard]] static bool BytesEqual(const char* lhs_,
                                         const char* rhs_,
                                         std::uint32_t size_bytes_) noexcept {
        if (size_bytes_ <= sizeof(std::uint64_t)) {
            std::uint64_t lhs_bits = 0U;
            std::uint64_t rhs_bits = 0U;
            std::memcpy(&lhs_bits, lhs_, static_cast<std::size_t>(size_bytes_));
            std::memcpy(&rhs_bits, rhs_, static_cast<std::size_t>(size_bytes_));
            return lhs_bits == rhs_bits;
        }
        return std::memcmp(lhs_, rhs_, static_cast<std::size_t>(size_bytes_)) == 0;
    }

    [[nodiscard]] static bool EnsureCapacityInitialized(TextType& component_) noexcept {
        if (component_.text.capacity_bytes == inline_capacity_bytes) {
            return true;
        }
        if (component_.text.capacity_bytes != 0U) {
            return false;
        }

        component_.text.capacity_bytes = inline_capacity_bytes;
        component_.text.size_bytes = std::min(component_.text.size_bytes,
                                              inline_capacity_bytes);
        component_.text.reserved = 0U;
        return true;
    }
};

// --- text_batch_system.hpp ----------------------------------------------------

struct TextBatchItem final {
    std::uint64_t sort_key;
    std::uint32_t component_index;
    std::uint32_t reserved0;
};

struct TextBatchBuildStats final {
    std::uint32_t total_count;
    std::uint32_t scanned_count;
    std::uint32_t visible_count;
    std::uint32_t hidden_count;
    std::uint32_t empty_count;
    std::uint32_t out_of_range_candidate_count;
    std::uint8_t used_candidate_indices;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

static_assert(PurePodComponent<TextBatchItem>);
static_assert(PurePodComponent<TextBatchBuildStats>);

template<DimensionTag DimensionT>
struct TextBatchScratch final {
    vr::McVector<TextBatchItem> visible_items{};
    vr::McVector<TextBatchItem> radix_scratch{};
    vr::McVector<std::uint32_t> ordered_indices{};
};

template<DimensionTag DimensionT>
class TextBatchSystem final {
public:
    using TextType = Text<DimensionT>;
    using TextSystemType = TextSystem<DimensionT>;
    using ScratchType = TextBatchScratch<DimensionT>;

    static constexpr std::uint32_t radix_bits_per_pass = 8U;
    static constexpr std::uint32_t radix_bucket_count = 1U << radix_bits_per_pass;
    static constexpr std::uint32_t radix_bucket_mask = radix_bucket_count - 1U;
    static constexpr std::uint32_t radix_pass_count = 64U / radix_bits_per_pass;

    static_assert((64U % radix_bits_per_pass) == 0U,
                  "TextBatchSystem radix_bits_per_pass must divide 64");

    static void Reserve(ScratchType& scratch_, std::uint32_t max_component_count_) {
        const auto reserve_count = static_cast<std::size_t>(max_component_count_);
        if (scratch_.visible_items.capacity() < reserve_count) {
            scratch_.visible_items.reserve(reserve_count);
        }
        if (scratch_.radix_scratch.capacity() < reserve_count) {
            scratch_.radix_scratch.reserve(reserve_count);
        }
        if (scratch_.ordered_indices.capacity() < reserve_count) {
            scratch_.ordered_indices.reserve(reserve_count);
        }
    }

    [[nodiscard]] static TextBatchBuildStats BuildVisibleItems(const TextType* components_,
                                                               std::uint32_t component_count_,
                                                               ScratchType& scratch_,
                                                               bool build_ordered_indices_ = false) {
        return BuildVisibleItemsInternal(components_, component_count_, nullptr, 0U, false, scratch_, build_ordered_indices_);
    }

    [[nodiscard]] static TextBatchBuildStats BuildVisibleItemsFromCandidates(
        const TextType* components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_count_,
        ScratchType& scratch_,
        bool build_ordered_indices_ = false) {
        return BuildVisibleItemsInternal(components_, component_count_,
                                         candidate_component_indices_, candidate_count_, true,
                                         scratch_, build_ordered_indices_);
    }

    [[nodiscard]] static TextBatchBuildStats BuildAndSort(const TextType* components_,
                                                          std::uint32_t component_count_,
                                                          ScratchType& scratch_,
                                                          bool build_ordered_indices_ = true) {
        TextBatchBuildStats stats = BuildVisibleItemsInternal(components_, component_count_,
                                                              nullptr, 0U, false, scratch_, false);
        if (stats.visible_count > 1U) {
            SortVisibleItemsBySortKey(scratch_);
        }
        if (build_ordered_indices_) {
            BuildOrderedIndices(scratch_);
        }
        return stats;
    }

    [[nodiscard]] static TextBatchBuildStats BuildAndSortFromCandidates(
        const TextType* components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_count_,
        ScratchType& scratch_,
        bool build_ordered_indices_ = true) {
        TextBatchBuildStats stats = BuildVisibleItemsInternal(components_, component_count_,
                                                              candidate_component_indices_, candidate_count_, true,
                                                              scratch_, false);
        if (stats.visible_count > 1U) {
            SortVisibleItemsBySortKey(scratch_);
        }
        if (build_ordered_indices_) {
            BuildOrderedIndices(scratch_);
        }
        return stats;
    }

    [[nodiscard]] static std::uint32_t VisibleCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.visible_items.size());
    }

    [[nodiscard]] static const TextBatchItem* SortedItems(const ScratchType& scratch_) noexcept {
        return scratch_.visible_items.data();
    }

    [[nodiscard]] static const std::uint32_t* OrderedIndices(const ScratchType& scratch_) noexcept {
        return scratch_.ordered_indices.data();
    }

    [[nodiscard]] static std::uint32_t OrderedIndexCount(const ScratchType& scratch_) noexcept {
        return static_cast<std::uint32_t>(scratch_.ordered_indices.size());
    }

    template<typename FnT>
    static void ForEachSortedItem(const TextType* components_,
                                  const ScratchType& scratch_,
                                  FnT&& function_) {
        if (components_ == nullptr) return;
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        for (std::uint32_t i = 0U; i < count; ++i) {
            const TextBatchItem& item = scratch_.visible_items[i];
            function_(item, components_[item.component_index]);
        }
    }

    template<typename FnT>
    static void ForEachSortKeyGroup(const ScratchType& scratch_, FnT&& function_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count == 0U) return;
        std::uint32_t begin = 0U;
        std::uint64_t current_key = scratch_.visible_items[0U].sort_key;
        for (std::uint32_t i = 1U; i < count; ++i) {
            const std::uint64_t key = scratch_.visible_items[i].sort_key;
            if (key == current_key) continue;
            function_(begin, i - begin, current_key);
            begin = i;
            current_key = key;
        }
        function_(begin, count - begin, current_key);
    }

    template<typename FnT>
    static void ForEachBindingGroup(const ScratchType& scratch_, FnT&& function_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count == 0U) return;
        std::uint32_t begin = 0U;
        std::uint64_t current_key = TextSystemType::BindingSortKey(scratch_.visible_items[0U].sort_key);
        for (std::uint32_t i = 1U; i < count; ++i) {
            const std::uint64_t key = TextSystemType::BindingSortKey(scratch_.visible_items[i].sort_key);
            if (key == current_key) continue;
            function_(begin, i - begin, current_key);
            begin = i;
            current_key = key;
        }
        function_(begin, count - begin, current_key);
    }

private:
    [[nodiscard]] static TextBatchBuildStats BuildVisibleItemsInternal(
        const TextType* components_, std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_, std::uint32_t candidate_count_,
        bool use_candidate_indices_, ScratchType& scratch_, bool build_ordered_indices_) {
        TextBatchBuildStats stats{};
        stats.total_count = component_count_;
        stats.scanned_count = use_candidate_indices_ ? candidate_count_ : component_count_;
        stats.visible_count = 0U;
        stats.hidden_count = 0U;
        stats.empty_count = 0U;
        stats.out_of_range_candidate_count = 0U;
        stats.used_candidate_indices = use_candidate_indices_ ? 1U : 0U;

        scratch_.visible_items.clear();
        scratch_.ordered_indices.clear();
        if (components_ == nullptr || component_count_ == 0U) return stats;

        Reserve(scratch_, component_count_);

        if (use_candidate_indices_) {
            if (candidate_component_indices_ == nullptr) {
                stats.out_of_range_candidate_count = candidate_count_;
            } else {
                for (std::uint32_t i = 0U; i < candidate_count_; ++i) {
                    const std::uint32_t component_index = candidate_component_indices_[i];
                    if (component_index >= component_count_) { ++stats.out_of_range_candidate_count; continue; }
                    const TextType& component = components_[component_index];
                    if (component.text.size_bytes == 0U) { ++stats.empty_count; continue; }
                    if (component.runtime.visible == 0U) { ++stats.hidden_count; continue; }
                    scratch_.visible_items.emplace_back(TextBatchItem{
                        .sort_key = component.runtime.sort_key,
                        .component_index = component_index,
                        .reserved0 = 0U
                    });
                }
            }
        } else {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                const TextType& component = components_[i];
                if (component.text.size_bytes == 0U) { ++stats.empty_count; continue; }
                if (component.runtime.visible == 0U) { ++stats.hidden_count; continue; }
                scratch_.visible_items.emplace_back(TextBatchItem{
                    .sort_key = component.runtime.sort_key,
                    .component_index = i,
                    .reserved0 = 0U
                });
            }
        }

        stats.visible_count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (build_ordered_indices_) {
            BuildOrderedIndices(scratch_);
        }
        return stats;
    }

    static void BuildOrderedIndices(ScratchType& scratch_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        scratch_.ordered_indices.resize(count);
        for (std::uint32_t i = 0U; i < count; ++i) {
            scratch_.ordered_indices[i] = scratch_.visible_items[i].component_index;
        }
    }

    static void SortVisibleItemsBySortKey(ScratchType& scratch_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count <= 1U) return;
        if (TryInsertionSortIfNearlySorted(scratch_)) return;
        RadixSortBySortKey(scratch_);
    }

    [[nodiscard]] static bool TryInsertionSortIfNearlySorted(ScratchType& scratch_) {
        constexpr std::uint32_t k_max_adjacent_descents = 8U;
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count <= 1U) return true;

        TextBatchItem* items = scratch_.visible_items.data();
        std::uint32_t descents = 0U;
        for (std::uint32_t i = 1U; i < count; ++i) {
            if (items[i - 1U].sort_key <= items[i].sort_key) continue;
            ++descents;
            if (descents > k_max_adjacent_descents) return false;
        }
        if (descents == 0U) return true;

        for (std::uint32_t i = 1U; i < count; ++i) {
            const TextBatchItem item = items[i];
            if (items[i - 1U].sort_key <= item.sort_key) continue;
            std::uint32_t j = i;
            while (j > 0U && items[j - 1U].sort_key > item.sort_key) {
                items[j] = items[j - 1U];
                --j;
            }
            items[j] = item;
        }
        return true;
    }

    static void RadixSortBySortKey(ScratchType& scratch_) {
        const std::uint32_t count = static_cast<std::uint32_t>(scratch_.visible_items.size());
        if (count <= 1U) return;

        scratch_.radix_scratch.resize(count);
        TextBatchItem* src = scratch_.visible_items.data();
        TextBatchItem* dst = scratch_.radix_scratch.data();
        bool src_is_primary = true;

        std::array<std::uint32_t, radix_bucket_count> histogram{};
        std::array<std::uint32_t, radix_bucket_count> offsets{};

        for (std::uint32_t pass_index = 0U; pass_index < radix_pass_count; ++pass_index) {
            histogram.fill(0U);
            const std::uint32_t shift = pass_index * radix_bits_per_pass;
            for (std::uint32_t i = 0U; i < count; ++i) {
                const std::uint32_t bucket = static_cast<std::uint32_t>((src[i].sort_key >> shift) & radix_bucket_mask);
                ++histogram[bucket];
            }
            std::uint32_t prefix = 0U;
            for (std::uint32_t bucket = 0U; bucket < radix_bucket_count; ++bucket) {
                offsets[bucket] = prefix;
                prefix += histogram[bucket];
            }
            for (std::uint32_t i = 0U; i < count; ++i) {
                const std::uint32_t bucket = static_cast<std::uint32_t>((src[i].sort_key >> shift) & radix_bucket_mask);
                dst[offsets[bucket]++] = src[i];
            }
            std::swap(src, dst);
            src_is_primary = !src_is_primary;
        }

        if (!src_is_primary) {
            std::memcpy(scratch_.visible_items.data(), src,
                        static_cast<std::size_t>(count) * sizeof(TextBatchItem));
        }
    }
};

// --- text_runtime_system.hpp --------------------------------------------------

struct TextGlyphQuad final {
    float x0;
    float y0;
    float x1;
    float y1;
    float u0;
    float v0;
    float u1;
    float v1;
    Rgba8 color;
    std::uint8_t sdf_enabled;
    std::uint8_t outline_enabled;
    std::uint8_t outline_width_px;
    std::uint8_t reserved0;
    Rgba8 outline_color;
    std::uint32_t atlas_page_id;
    std::uint32_t component_index;
    std::uint32_t glyph_index;
    std::uint32_t user_data;
};

struct TextDrawBatch final {
    std::uint64_t sort_key;
    std::uint32_t glyph_begin;
    std::uint32_t glyph_count;
    std::uint32_t atlas_page_id;
    std::uint32_t font_id;
    std::uint32_t material_id;
    std::uint32_t first_component_index;
    std::uint32_t reserved0;
};

struct TextRuntimeBuildConfig {
    std::uint32_t tab_space_count = 4U;
    std::int32_t glyph_load_flags = 0;
    float dim3_pixels_per_world_unit = 96.0F;
    float pixel_size_quantization = 1.0F;
    float min_pixel_size = 8.0F;
    float max_pixel_size = 256.0F;
    text::GlyphRenderMode bitmap_render_mode = text::GlyphRenderMode::light;
    bool enable_kerning = true;
};

struct TextRuntimeBuildHint final {
    const std::uint32_t* visible_component_indices = nullptr;
    std::uint64_t external_visible_set_signature = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint8_t use_visible_component_indices = 0U;
    std::uint8_t use_external_visible_set_signature = 0U;
    std::uint16_t reserved0 = 0U;
};

struct TextRuntimeBuildStats {
    std::uint32_t total_component_count = 0U;
    std::uint32_t candidate_component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t built_component_count = 0U;
    std::uint32_t skipped_component_count = 0U;
    std::uint32_t decoded_codepoint_count = 0U;
    std::uint32_t glyph_resolve_count = 0U;
    std::uint32_t emitted_glyph_quad_count = 0U;
    std::uint32_t emitted_batch_count = 0U;
    std::uint32_t face_variant_cache_entries = 0U;
    std::uint32_t kerning_apply_count = 0U;
    std::uint64_t visible_set_signature = 0U;
    bool used_visible_component_indices = false;
    bool visible_set_signature_from_hint = false;
};

template<DimensionTag DimensionT>
struct TextRuntimeScratch final {
    struct FaceVariantCacheEntry {
        std::uint32_t font_id = 0U;
        std::uint32_t base_face_id_value = 0U;
        std::uint32_t pixel_height = 0U;
        text::FontFaceId face_id{};
    };

    struct RunGlyphRecord {
        text::GlyphAtlasResolvedEntry resolved{};
        std::uint32_t line_index = 0U;
        float pen_x = 0.0F;
        float baseline_y = 0.0F;
        std::uint32_t codepoint = 0U;
    };

    struct GlyphResolveCacheEntry {
        std::uint64_t hash = 0U;
        std::uint32_t face_id_value = 0U;
        std::uint32_t codepoint = 0U;
        std::int32_t load_flags = 0;
        std::uint8_t render_mode = 0U;
        std::uint8_t reserved0 = 0U;
        std::uint8_t reserved1 = 0U;
        std::uint8_t reserved2 = 0U;
        text::GlyphAtlasResolvedEntry resolved{};
    };

    vr::McVector<TextGlyphQuad> glyph_quads{};
    vr::McVector<TextDrawBatch> draw_batches{};
    TextBatchScratch<DimensionT> batch_scratch{};

    vr::McVector<std::uint32_t> utf32_codepoints{};
    vr::McVector<float> line_widths{};
    vr::McVector<float> line_x_offsets{};
    vr::McVector<RunGlyphRecord> run_glyphs{};
    vr::McVector<FaceVariantCacheEntry> face_variants{};
    vr::McVector<GlyphResolveCacheEntry> glyph_resolve_cache{};
};

static_assert(PurePodComponent<TextGlyphQuad>);
static_assert(PurePodComponent<TextDrawBatch>);

template<DimensionTag DimensionT>
class TextRuntimeSystem final {
public:
    using TextType = Text<DimensionT>;
    using TextSystemType = TextSystem<DimensionT>;
    using BatchSystemType = TextBatchSystem<DimensionT>;
    using ScratchType = TextRuntimeScratch<DimensionT>;
    using FaceVariantEntry = typename ScratchType::FaceVariantCacheEntry;

    static void Reserve(ScratchType& scratch_,
                        std::uint32_t component_count_,
                        std::uint32_t glyph_capacity_hint_ = 0U) {
        BatchSystemType::Reserve(scratch_.batch_scratch, component_count_);
        if (glyph_capacity_hint_ > 0U) {
            const auto glyph_reserve = static_cast<std::size_t>(glyph_capacity_hint_);
            if (scratch_.glyph_quads.capacity() < glyph_reserve) scratch_.glyph_quads.reserve(glyph_reserve);
            if (scratch_.run_glyphs.capacity() < glyph_reserve) scratch_.run_glyphs.reserve(glyph_reserve);
            if (scratch_.utf32_codepoints.capacity() < glyph_reserve) scratch_.utf32_codepoints.reserve(glyph_reserve);
            if (scratch_.glyph_resolve_cache.capacity() < glyph_reserve) scratch_.glyph_resolve_cache.reserve(glyph_reserve);
        }
    }

    [[nodiscard]] static TextRuntimeBuildStats Build(TextType* components_,
                                                     std::uint32_t component_count_,
                                                     text::GlyphAtlasHost& atlas_host_,
                                                     text::FreeTypeHost& freetype_host_,
                                                     ScratchType& scratch_,
                                                     const TextRuntimeBuildConfig& build_config_ = {},
                                                     const TextRuntimeBuildHint& build_hint_ = {});

private:
    static constexpr std::uint64_t k_hash_prime = 1099511628211ULL;

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= k_hash_prime;
    }

    [[nodiscard]] static std::uint64_t ComputeVisibleSetSignature(
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_component_count_,
        bool use_candidate_indices_) noexcept;

    [[nodiscard]] static std::uint64_t HashGlyphResolveRequest(std::uint32_t face_id_value_,
                                                                std::uint32_t codepoint_,
                                                                std::int32_t load_flags_,
                                                                text::GlyphRenderMode render_mode_) noexcept;

    [[nodiscard]] static std::uint32_t LowerBoundGlyphResolveCache(
        const vr::McVector<typename ScratchType::GlyphResolveCacheEntry>& cache_,
        std::uint64_t hash_) noexcept;

    [[nodiscard]] static text::GlyphRenderMode NormalizeBitmapRenderMode(
        text::GlyphRenderMode mode_) noexcept;

    [[nodiscard]] static text::GlyphAtlasResolvedEntry ResolveGlyphCached(
        text::GlyphAtlasHost& atlas_host_,
        text::FontFaceId face_id_,
        std::uint32_t codepoint_,
        std::int32_t load_flags_,
        text::GlyphRenderMode render_mode_,
        ScratchType& scratch_);

    [[nodiscard]] static std::uint32_t QuantizePixelHeight(const TextType& component_,
                                                           const TextRuntimeBuildConfig& build_config_) noexcept;

    [[nodiscard]] static std::uint64_t MakeFaceVariantKey(std::uint32_t base_face_id_value_,
                                                           std::uint32_t pixel_height_) noexcept;

    [[nodiscard]] static std::uint32_t LowerBoundFaceVariant(
        const vr::McVector<FaceVariantEntry>& face_variants_,
        std::uint64_t key_) noexcept;

    [[nodiscard]] static text::FontFaceId AcquireVariantFaceId(
        std::uint32_t font_id_,
        std::uint32_t pixel_height_,
        text::GlyphAtlasHost& atlas_host_,
        text::FreeTypeHost& freetype_host_,
        ScratchType& scratch_);

    static void DecodeUtf8(std::string_view text_,
                           vr::McVector<std::uint32_t>& out_codepoints_);

    [[nodiscard]] static float ResolveLineSpacing(const TextType& component_) noexcept;
    [[nodiscard]] static float ResolveLetterSpacing(const TextType& component_) noexcept;

    static void BuildRunGlyphs(TextType& component_,
                               text::FontFaceId face_id_,
                               text::GlyphAtlasHost& atlas_host_,
                               text::FreeTypeHost& freetype_host_,
                               const TextRuntimeBuildConfig& build_config_,
                               ScratchType& scratch_,
                               TextRuntimeBuildStats& stats_);

    [[nodiscard]] static std::uint64_t SortKeyWithAtlas(const TextType& component_,
                                                        std::uint32_t atlas_page_id_) noexcept;

    static void AppendOrMergeBatch(ScratchType& scratch_,
                                   std::uint64_t sort_key_,
                                   std::uint32_t glyph_begin_,
                                   std::uint32_t glyph_count_,
                                   std::uint32_t atlas_page_id_,
                                   std::uint32_t font_id_,
                                   std::uint32_t material_id_,
                                   std::uint32_t component_index_);

    static void EmitGlyphQuadsAndBatches(TextType& component_,
                                         std::uint32_t component_index_,
                                         float glyph_world_scale_,
                                         ScratchType& scratch_,
                                         TextRuntimeBuildStats& stats_);
};

// --- text_render_3d_system.hpp -------------------------------------------------

struct Text3DGpuInstance final {
    float rect_x0;
    float rect_y0;
    float rect_x1;
    float rect_y1;

    float uv_u0;
    float uv_v0;
    float uv_u1;
    float uv_v1;

    Float3 origin_world;
    float reserved0;

    Float3 basis_right_world;
    float reserved1;

    Float3 basis_up_world;
    float reserved2;

    std::uint32_t color_rgba8;
    std::uint32_t outline_color_rgba8;
    std::uint32_t params;
    std::uint32_t atlas_page_id;

    std::uint32_t component_index;
    std::uint32_t user_data;
    std::uint32_t glyph_index;
    std::uint32_t reserved3;
};

struct Text3DFrameData final {
    Matrix4x4 view_projection;
    Float3 camera_position;
    float reserved0;

    Float3 camera_right;
    float reserved1;

    Float3 camera_up;
    float reserved2;
};

struct TextRender3DBuildStats final {
    TextRuntimeBuildStats runtime{};
    std::uint32_t emitted_instance_count = 0U;
    std::uint32_t emitted_batch_count = 0U;
    std::uint32_t billboard_instance_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
};

struct Text3DDrawBatch final {
    std::uint64_t sort_key;
    std::uint32_t glyph_begin;
    std::uint32_t glyph_count;
    std::uint32_t atlas_page_id;
    std::uint32_t font_id;
    std::uint32_t material_id;
    std::uint32_t first_component_index;
    std::uint32_t depth_flags;
};

struct TextRender3DScratch final {
    TextRuntimeScratch<Dim3> runtime_scratch{};
    vr::McVector<Text3DGpuInstance> instances{};
    vr::McVector<Text3DDrawBatch> draw_batches{};
};

static_assert(PurePodComponent<Text3DGpuInstance>);
static_assert(PurePodComponent<Text3DFrameData>);
static_assert(PurePodComponent<Text3DDrawBatch>);
static_assert(alignof(Text3DGpuInstance) == 4U);
static_assert(sizeof(Text3DGpuInstance) == 112U);

class TextRender3DSystem final {
public:
    using TextType = Text<Dim3>;
    using TransformType = Transform<Dim3>;
    using CameraType = Camera<Dim3>;

    static void Reserve(TextRender3DScratch& scratch_,
                        std::uint32_t component_count_,
                        std::uint32_t glyph_capacity_hint_ = 0U) {
        TextRuntimeSystem<Dim3>::Reserve(scratch_.runtime_scratch, component_count_, glyph_capacity_hint_);
        if (glyph_capacity_hint_ > 0U) {
            const auto target = static_cast<std::size_t>(glyph_capacity_hint_);
            if (scratch_.instances.capacity() < target) scratch_.instances.reserve(target);
            if (scratch_.draw_batches.capacity() < target) scratch_.draw_batches.reserve(target);
        }
    }

    [[nodiscard]] static Text3DFrameData BuildFrameData(const CameraType& camera_,
                                                        const TransformType& camera_transform_) noexcept;

    [[nodiscard]] static TextRender3DBuildStats Build(TextType* components_,
                                                       const TransformType* transforms_,
                                                       std::uint32_t component_count_,
                                                       const CameraType& camera_,
                                                       const TransformType& camera_transform_,
                                                       text::GlyphAtlasHost& atlas_host_,
                                                       text::FreeTypeHost& freetype_host_,
                                                       TextRender3DScratch& scratch_,
                                                       const TextRuntimeBuildConfig& build_config_ = {},
                                                       const TextRuntimeBuildHint& runtime_build_hint_ = {});

    [[nodiscard]] static TextRender3DBuildStats BuildFromRuntime(TextType* components_,
                                                                  const TransformType* transforms_,
                                                                  std::uint32_t component_count_,
                                                                  const CameraType& camera_,
                                                                  const TransformType& camera_transform_,
                                                                  TextRender3DScratch& scratch_,
                                                                  const TextRuntimeBuildStats& runtime_stats_);

private:
    [[nodiscard]] static std::uint32_t PackRgba8(const Rgba8& color_) noexcept;
    [[nodiscard]] static std::uint32_t PackParams(const TextGlyphQuad& quad_,
                                                  bool depth_test_enabled_,
                                                  bool depth_write_enabled_) noexcept;
    [[nodiscard]] static Float3 ExtractBasisX(const Matrix4x4& matrix_) noexcept;
    [[nodiscard]] static Float3 ExtractBasisY(const Matrix4x4& matrix_) noexcept;
    [[nodiscard]] static Float3 ExtractTranslation(const Matrix4x4& matrix_) noexcept;
    [[nodiscard]] static float Length(const Float3& value_) noexcept;
    [[nodiscard]] static Float3 Scale(const Float3& value_, float factor_) noexcept;
    [[nodiscard]] static Float3 NormalizeOrFallback(const Float3& value_,
                                                    const Float3& fallback_) noexcept;
    [[nodiscard]] static std::uint32_t DepthFlagsFromInstanceParams(std::uint32_t params_) noexcept;
    static void AppendOrMergeDrawBatch3D(TextRender3DScratch& scratch_,
                                         const TextDrawBatch& source_batch_,
                                         std::uint32_t glyph_begin_,
                                         std::uint32_t glyph_count_,
                                         std::uint32_t depth_flags_);
    static void Rebuild3DDrawBatches(TextRender3DScratch& scratch_,
                                     TextRender3DBuildStats& stats_);
};

} // namespace vr::ecs
} // export

// Text renderers (depend on both vr::text and vr::ecs text types)
export {
namespace vr::text {

// --- text_renderer_2d.hpp -----------------------------------------------------

struct TextRenderer2DCreateInfo {
    ecs::TextRuntimeBuildConfig runtime_build{};
    std::uint32_t reserve_component_count = 2048U;
    std::uint32_t reserve_glyph_count = 32768U;
    VkDeviceSize initial_vertex_buffer_bytes = 4U * 1024U * 1024U;
    float depth = 0.0F;
    float sdf_smooth = 1.0F;
    float bitmap_gamma = 1.0F;
    float bitmap_edge_sharpness = 1.0F;
    bool enable_pixel_snap = true;
    float pixel_snap_step = 1.0F;
    bool clear_swapchain = true;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
};

struct TextRenderer2DStats {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t built_component_count = 0U;
    std::uint32_t glyph_quad_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t skipped_draw_batch_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
};

class TextRenderer2D final {
public:
    TextRenderer2D() = default;
    ~TextRenderer2D() = default;

    TextRenderer2D(const TextRenderer2D&) = delete;
    TextRenderer2D& operator=(const TextRenderer2D&) = delete;

    TextRenderer2D(TextRenderer2D&&) = delete;
    TextRenderer2D& operator=(TextRenderer2D&&) = delete;

    void Initialize(const TextRenderer2DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetComponents(ecs::Text<ecs::Dim2>* components_,
                       std::uint32_t component_count_) noexcept;

    void PrepareFrame(const render::RuntimePrepareContext& prepare_context_);
    void Record(const render::FrameRecordContext& record_context_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const TextRenderer2DStats& Stats() const noexcept;

private:
    struct GpuTextInstance final {
        float rect_x0;
        float rect_y0;
        float rect_x1;
        float rect_y1;

        float uv_u0;
        float uv_v0;
        float uv_u1;
        float uv_v1;

        std::uint32_t color_rgba8;
        std::uint32_t outline_color_rgba8;
        std::uint32_t params;
    };

    struct PushConstants final {
        float inv_viewport_x;
        float inv_viewport_y;
        float depth;
        float sdf_smooth;
        float bitmap_gamma;
        float bitmap_edge_sharpness;
    };

    static_assert(ecs::PurePodComponent<GpuTextInstance>);
    static_assert(sizeof(PushConstants) == 24U);

    struct PerFrameState final {
        resource::BufferResource vertex_buffer{};
        VkDeviceSize vertex_buffer_capacity_bytes = 0U;
        std::uint32_t instance_count = 0U;
        std::uint64_t uploaded_revision = 0U;
        vr::McVector<VkDescriptorSet> page_sets{};
        vr::McVector<std::uint32_t> page_set_epochs{};
        std::uint32_t page_set_epoch = 1U;
        vr::McVector<std::uint32_t> page_touch_epochs{};
        std::uint32_t page_touch_epoch = 1U;
    };

    [[nodiscard]] static std::uint32_t PackRgba8(const ecs::Rgba8& color_) noexcept;
    [[nodiscard]] static std::uint32_t PackParams(const ecs::TextGlyphQuad& quad_) noexcept;
    [[nodiscard]] static VkDeviceSize NextPow2(VkDeviceSize value_) noexcept;
    [[nodiscard]] static float QuantizeToStep(float value_, float step_) noexcept;
    [[nodiscard]] static bool AnyComponentDirty(const ecs::Text<ecs::Dim2>* components_,
                                                std::uint32_t component_count_) noexcept;

    void ResetPerFrameDrawState(std::uint32_t frame_index_,
                                std::uint32_t atlas_page_count_);
    void BuildGpuInstancesFromScratch();
    void EnsureGpuResourcesForFrame(VulkanContext& context_,
                                    const render::RuntimePrepareContext& prepare_context_,
                                    std::uint32_t frame_index_,
                                    VkDeviceSize required_bytes_);
    void PreparePageDescriptorSetsForFrame(std::uint32_t frame_index_);

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::DescriptorHost& descriptor_host_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_);

    [[nodiscard]] VkDescriptorSet EnsurePageDescriptorSet(VulkanContext& context_,
                                                          render::DescriptorHost& descriptor_host_,
                                                          std::uint32_t frame_index_,
                                                          std::uint32_t page_index_);

    void RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_,
                                                bool has_previous_content_) const;
    void RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const;

private:
    TextRenderer2DCreateInfo create_info_cache{};
    TextRenderer2DStats stats{};

    ecs::Text<ecs::Dim2>* components = nullptr;
    std::uint32_t component_count = 0U;

    ecs::TextRuntimeScratch<ecs::Dim2> runtime_scratch{};
    vr::McVector<GpuTextInstance> gpu_instances{};
    vr::McVector<PerFrameState> frame_states{};
    vr::McVector<std::uint8_t> image_initialized{};

    render::DescriptorSetLayoutId descriptor_layout_id{};
    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    render::GraphicsPipelineId graphics_pipeline_id{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;

    render::DescriptorMcVector<render::DescriptorImageWrite> descriptor_image_write_scratch{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};

    VulkanContext* context = nullptr;
    render::UploadHost* upload_host = nullptr;
    render::DescriptorHost* descriptor_host = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    FreeTypeHost* freetype_host = nullptr;
    GlyphAtlasHost* glyph_atlas_host = nullptr;
    GlyphUploadHost* glyph_upload_host = nullptr;

    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    ecs::TextRuntimeBuildStats cached_build_stats{};
    const ecs::Text<ecs::Dim2>* cached_components_ptr = nullptr;
    std::uint32_t cached_component_count = 0U;
    std::uint64_t runtime_geometry_revision = 1U;
    bool runtime_geometry_valid = false;
    bool initialized = false;
};

// --- text_renderer_3d.hpp -----------------------------------------------------

struct TextRenderer3DCreateInfo {
    ecs::TextRuntimeBuildConfig runtime_build{};
    std::uint32_t reserve_component_count = 2048U;
    std::uint32_t reserve_glyph_count = 32768U;
    VkDeviceSize initial_vertex_buffer_bytes = 4U * 1024U * 1024U;
    bool enable_depth = true;
    VkFormat preferred_depth_format = VK_FORMAT_D32_SFLOAT;
    bool clear_depth = true;
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
    float sdf_smooth = 1.0F;
    float bitmap_gamma = 1.0F;
    float bitmap_edge_sharpness = 1.0F;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
};

struct TextRenderer3DStats {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t built_component_count = 0U;
    std::uint32_t glyph_quad_count = 0U;
    std::uint32_t instance_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t billboard_instance_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t skipped_draw_batch_count = 0U;
    std::uint32_t depth_pipeline_bind_count = 0U;
    std::uint32_t reverse_z_draw_call_count = 0U;
    std::uint32_t culling_input_count = 0U;
    std::uint32_t culling_visible_count = 0U;
    std::uint32_t culling_culled_count = 0U;
    std::uint32_t culling_mask_reject_count = 0U;
    std::uint32_t culling_frustum_reject_count = 0U;
    std::uint32_t culling_invalid_bounds_count = 0U;
    std::uint32_t culling_plane_test_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    bool used_bounds_culling = false;
};

class TextRenderer3D final {
public:
    TextRenderer3D() = default;
    ~TextRenderer3D() = default;

    TextRenderer3D(const TextRenderer3D&) = delete;
    TextRenderer3D& operator=(const TextRenderer3D&) = delete;

    TextRenderer3D(TextRenderer3D&&) = delete;
    TextRenderer3D& operator=(TextRenderer3D&&) = delete;

    void Initialize(const TextRenderer3DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetSceneData(ecs::Text<ecs::Dim3>* text_components_,
                      ecs::Transform<ecs::Dim3>* text_transforms_,
                      std::uint32_t component_count_,
                      ecs::Camera<ecs::Dim3>* camera_component_,
                      ecs::Transform<ecs::Dim3>* camera_transform_,
                      ecs::Bounds<ecs::Dim3>* bounds_components_ = nullptr) noexcept;

    void PrepareFrame(const render::RuntimePrepareContext& prepare_context_);
    void Record(const render::FrameRecordContext& record_context_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const TextRenderer3DStats& Stats() const noexcept;

private:
    struct PushConstants final {
        ecs::Matrix4x4 view_projection;
        float sdf_smooth;
        float bitmap_gamma;
        float bitmap_edge_sharpness;
        float reserved0;
    };

    static_assert(sizeof(PushConstants) == 80U);

    enum class DepthPipelineMode : std::uint8_t {
        no_depth = 0U,
        depth_test = 1U,
        depth_test_write = 2U,
        depth_test_reverse_z = 3U,
        depth_test_write_reverse_z = 4U,
        count = 5U
    };

    struct PerFrameState final {
        resource::BufferResource vertex_buffer{};
        VkDeviceSize vertex_buffer_capacity_bytes = 0U;
        std::uint32_t instance_count = 0U;
        std::uint64_t uploaded_revision = 0U;
        vr::McVector<VkDescriptorSet> page_sets{};
        vr::McVector<std::uint32_t> page_set_epochs{};
        std::uint32_t page_set_epoch = 1U;
        vr::McVector<std::uint32_t> page_touch_epochs{};
        std::uint32_t page_touch_epoch = 1U;
    };

    struct RetiredDepthImage final {
        resource::ImageResource resource{};
        std::uint64_t retire_value = 0U;
    };

    [[nodiscard]] static VkDeviceSize NextPow2(VkDeviceSize value_) noexcept;
    [[nodiscard]] static bool AnyTextComponentDirty(const ecs::Text<ecs::Dim3>* components_,
                                                    std::uint32_t component_count_) noexcept;
    [[nodiscard]] static bool IsDepthFormatSupported(VulkanContext& context_,
                                                     VkFormat format_) noexcept;
    [[nodiscard]] static bool DepthFormatHasStencil(VkFormat format_) noexcept;
    [[nodiscard]] static VkImageAspectFlags DepthImageAspectMask(VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_,
                                                     VkFormat preferred_format_);
    [[nodiscard]] static std::size_t PipelineModeIndex(DepthPipelineMode mode_) noexcept;
    [[nodiscard]] static std::uint64_t ComputeTransformRevisionSignature(
        const ecs::Transform<ecs::Dim3>* transforms_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_component_count_,
        bool use_candidate_indices_) noexcept;
    [[nodiscard]] static DepthPipelineMode ResolveDepthPipelineMode(const ecs::Text3DDrawBatch& batch_,
                                                                    bool use_depth_,
                                                                    bool reverse_z_) noexcept;

    void ResetPerFrameDrawState(std::uint32_t frame_index_,
                                std::uint32_t atlas_page_count_);
    void EnsureGpuResourcesForFrame(VulkanContext& context_,
                                    const render::RuntimePrepareContext& prepare_context_,
                                    std::uint32_t frame_index_,
                                    VkDeviceSize required_bytes_);
    void PreparePageDescriptorSetsForFrame(std::uint32_t frame_index_);

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::DescriptorHost& descriptor_host_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_,
                               VkFormat depth_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsureGraphicsPipelineForMode(VulkanContext& context_,
                                                                            render::PipelineHost& pipeline_host_,
                                                                            VkFormat color_format_,
                                                                            VkFormat depth_format_,
                                                                            DepthPipelineMode mode_);
    void DestroyDepthResources(VulkanContext& context_);
    void DestroyRetiredDepthResources(VulkanContext& context_);
    void RetireDepthResources(std::uint64_t retire_value_);
    void CollectRetiredDepthResources(VulkanContext& context_,
                                      std::uint64_t completed_value_);
    void EnsureDepthResources(VulkanContext& context_,
                              std::uint32_t image_count_,
                              VkExtent2D extent_);

    [[nodiscard]] VkDescriptorSet EnsurePageDescriptorSet(VulkanContext& context_,
                                                          render::DescriptorHost& descriptor_host_,
                                                          std::uint32_t frame_index_,
                                                          std::uint32_t page_index_);

    void RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_,
                                                bool has_previous_content_) const;
    void RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const;
    void RecordDepthTransitionToAttachment(VkCommandBuffer command_buffer_,
                                           const resource::ImageResource& depth_resource_,
                                           bool initialized_) const;

private:
    TextRenderer3DCreateInfo create_info_cache{};
    TextRenderer3DStats stats{};

    ecs::Text<ecs::Dim3>* text_components = nullptr;
    ecs::Transform<ecs::Dim3>* text_transforms = nullptr;
    std::uint32_t component_count = 0U;
    ecs::Camera<ecs::Dim3>* camera_component = nullptr;
    ecs::Transform<ecs::Dim3>* camera_transform = nullptr;
    ecs::Bounds<ecs::Dim3>* bounds_components = nullptr;

    ecs::TextRender3DScratch render_scratch{};
    ecs::CullingScratch<ecs::Dim3> culling_scratch{};
    ecs::CullingBuildStats culling_stats{};
    vr::McVector<PerFrameState> frame_states{};
    vr::McVector<std::uint8_t> image_initialized{};

    render::DescriptorSetLayoutId descriptor_layout_id{};
    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    std::array<render::GraphicsPipelineId,
               static_cast<std::size_t>(DepthPipelineMode::count)> graphics_pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;

    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    vr::McVector<resource::ImageResource> depth_images{};
    vr::McVector<std::uint8_t> depth_image_initialized{};
    vr::McVector<RetiredDepthImage> retired_depth_images{};

    render::DescriptorMcVector<render::DescriptorImageWrite> descriptor_image_write_scratch{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};

    VulkanContext* context = nullptr;
    render::UploadHost* upload_host = nullptr;
    render::DescriptorHost* descriptor_host = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    FreeTypeHost* freetype_host = nullptr;
    GlyphAtlasHost* glyph_atlas_host = nullptr;
    GlyphUploadHost* glyph_upload_host = nullptr;

    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;

    ecs::TextRuntimeBuildStats cached_runtime_stats{};
    ecs::TextRender3DBuildStats cached_render_stats{};
    ecs::Text3DFrameData frame_data_cache{};

    const ecs::Text<ecs::Dim3>* cached_components_ptr = nullptr;
    const ecs::Transform<ecs::Dim3>* cached_transforms_ptr = nullptr;
    const ecs::Camera<ecs::Dim3>* cached_camera_component_ptr = nullptr;
    const ecs::Transform<ecs::Dim3>* cached_camera_transform_ptr = nullptr;
    std::uint32_t cached_component_count = 0U;
    std::uint64_t cached_transform_signature = 0U;
    std::uint32_t cached_camera_world_revision = 0U;

    std::uint64_t runtime_geometry_revision = 1U;
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool runtime_geometry_valid = false;
    bool instance_geometry_valid = false;
    bool contains_billboard_instances = false;
    bool active_camera_reverse_z = false;
    bool initialized = false;
};

} // namespace vr::text
} // export
