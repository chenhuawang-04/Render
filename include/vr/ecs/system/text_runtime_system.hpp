#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/text_batch_system.hpp"
#include "vr/text/glyph_atlas_host.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace vr::ecs {

template<typename T>
using TextRuntimeMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

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

    TextRuntimeMcVector<TextGlyphQuad> glyph_quads{};
    TextRuntimeMcVector<TextDrawBatch> draw_batches{};
    TextBatchScratch<DimensionT> batch_scratch{};

    TextRuntimeMcVector<std::uint32_t> utf32_codepoints{};
    TextRuntimeMcVector<float> line_widths{};
    TextRuntimeMcVector<float> line_x_offsets{};
    TextRuntimeMcVector<RunGlyphRecord> run_glyphs{};
    TextRuntimeMcVector<FaceVariantCacheEntry> face_variants{};
    TextRuntimeMcVector<GlyphResolveCacheEntry> glyph_resolve_cache{};
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
            if (scratch_.glyph_quads.capacity() < glyph_reserve) {
                scratch_.glyph_quads.reserve(glyph_reserve);
            }
            if (scratch_.run_glyphs.capacity() < glyph_reserve) {
                scratch_.run_glyphs.reserve(glyph_reserve);
            }
            if (scratch_.utf32_codepoints.capacity() < glyph_reserve) {
                scratch_.utf32_codepoints.reserve(glyph_reserve);
            }
            if (scratch_.glyph_resolve_cache.capacity() < glyph_reserve) {
                scratch_.glyph_resolve_cache.reserve(glyph_reserve);
            }
        }
    }

    [[nodiscard]] static TextRuntimeBuildStats Build(TextType* components_,
                                                     std::uint32_t component_count_,
                                                     text::GlyphAtlasHost& atlas_host_,
                                                     text::FreeTypeHost& freetype_host_,
                                                     ScratchType& scratch_,
                                                     const TextRuntimeBuildConfig& build_config_ = {},
                                                     const TextRuntimeBuildHint& build_hint_ = {}) {
        TextRuntimeBuildStats stats{};
        stats.total_component_count = component_count_;

        scratch_.glyph_quads.clear();
        scratch_.draw_batches.clear();
        scratch_.line_widths.clear();
        scratch_.line_x_offsets.clear();
        scratch_.run_glyphs.clear();
        scratch_.glyph_resolve_cache.clear();

        if (components_ == nullptr || component_count_ == 0U) {
            stats.face_variant_cache_entries = static_cast<std::uint32_t>(scratch_.face_variants.size());
            return stats;
        }
        if (!atlas_host_.IsInitialized()) {
            throw std::runtime_error("TextRuntimeSystem::Build requires initialized GlyphAtlasHost");
        }
        if (!freetype_host_.IsInitialized()) {
            throw std::runtime_error("TextRuntimeSystem::Build requires initialized FreeTypeHost");
        }

        const bool use_visible_component_indices = build_hint_.use_visible_component_indices != 0U;
        const bool use_external_visible_set_signature =
            build_hint_.use_external_visible_set_signature != 0U;
        const std::uint32_t* candidate_component_indices = use_visible_component_indices
            ? build_hint_.visible_component_indices
            : nullptr;
        const std::uint32_t candidate_component_count = use_visible_component_indices
            ? build_hint_.visible_component_count
            : component_count_;

        const auto batch_stats = use_visible_component_indices
            ? BatchSystemType::BuildAndSortFromCandidates(components_,
                                                          component_count_,
                                                          candidate_component_indices,
                                                          candidate_component_count,
                                                          scratch_.batch_scratch,
                                                          true)
            : BatchSystemType::BuildAndSort(components_,
                                            component_count_,
                                            scratch_.batch_scratch,
                                            true);
        stats.candidate_component_count = candidate_component_count;
        stats.used_visible_component_indices = use_visible_component_indices;
        stats.visible_set_signature = use_external_visible_set_signature
            ? build_hint_.external_visible_set_signature
            : ComputeVisibleSetSignature(candidate_component_indices,
                                         candidate_component_count,
                                         use_visible_component_indices);
        stats.visible_set_signature_from_hint = use_external_visible_set_signature;
        stats.visible_component_count = batch_stats.visible_count;
        if (batch_stats.visible_count == 0U) {
            stats.face_variant_cache_entries = static_cast<std::uint32_t>(scratch_.face_variants.size());
            return stats;
        }

        const TextBatchItem* sorted_items = BatchSystemType::SortedItems(scratch_.batch_scratch);
        if (sorted_items == nullptr) {
            stats.face_variant_cache_entries = static_cast<std::uint32_t>(scratch_.face_variants.size());
            return stats;
        }

        for (std::uint32_t visible_index = 0U; visible_index < batch_stats.visible_count; ++visible_index) {
            const std::uint32_t component_index = sorted_items[visible_index].component_index;
            if (component_index >= component_count_) {
                continue;
            }

            TextType& component = components_[component_index];
            if (!TextSystemType::IsVisibleForBatch(component)) {
                ++stats.skipped_component_count;
                continue;
            }

            const std::string_view text_view = TextSystemType::GetText(component);
            if (text_view.empty()) {
                component.runtime.glyph_begin = 0U;
                component.runtime.glyph_count = 0U;
                component.runtime.atlas_page_id = 0U;
                TextSystemType::ClearDirtyFlags(component,
                                                text_dirty_flag | style_dirty_flag | runtime_dirty_flag);
                ++stats.skipped_component_count;
                continue;
            }

            const std::uint32_t pixel_height = QuantizePixelHeight(component, build_config_);
            float glyph_world_scale = 1.0F;
            if constexpr (std::same_as<DimensionT, Dim3>) {
                const float safe_world_size = std::max(1e-6F, std::abs(component.style.world_size));
                glyph_world_scale = safe_world_size / static_cast<float>(std::max(1U, pixel_height));
            }
            const text::FontFaceId variant_face_id = AcquireVariantFaceId(component.runtime.font_id,
                                                                          pixel_height,
                                                                          atlas_host_,
                                                                          freetype_host_,
                                                                          scratch_);
            if (!variant_face_id.IsValid()) {
                component.runtime.glyph_begin = 0U;
                component.runtime.glyph_count = 0U;
                component.runtime.atlas_page_id = 0U;
                ++stats.skipped_component_count;
                continue;
            }

            DecodeUtf8(text_view, scratch_.utf32_codepoints);
            stats.decoded_codepoint_count += static_cast<std::uint32_t>(scratch_.utf32_codepoints.size());
            if (scratch_.utf32_codepoints.empty()) {
                component.runtime.glyph_begin = 0U;
                component.runtime.glyph_count = 0U;
                component.runtime.atlas_page_id = 0U;
                TextSystemType::ClearDirtyFlags(component,
                                                text_dirty_flag | style_dirty_flag | runtime_dirty_flag);
                ++stats.skipped_component_count;
                continue;
            }

            BuildRunGlyphs(component,
                           variant_face_id,
                           atlas_host_,
                           freetype_host_,
                           build_config_,
                           scratch_,
                           stats);
            if (scratch_.run_glyphs.empty()) {
                component.runtime.glyph_begin = 0U;
                component.runtime.glyph_count = 0U;
                component.runtime.atlas_page_id = 0U;
                TextSystemType::ClearDirtyFlags(component,
                                                text_dirty_flag | style_dirty_flag | runtime_dirty_flag);
                ++stats.skipped_component_count;
                continue;
            }

            const std::uint32_t glyph_begin = static_cast<std::uint32_t>(scratch_.glyph_quads.size());
            EmitGlyphQuadsAndBatches(component,
                                     component_index,
                                     glyph_world_scale,
                                     scratch_,
                                     stats);
            const std::uint32_t glyph_count = static_cast<std::uint32_t>(scratch_.glyph_quads.size()) - glyph_begin;

            component.runtime.glyph_begin = glyph_begin;
            component.runtime.glyph_count = glyph_count;
            component.runtime.atlas_page_id = glyph_count > 0U
                ? scratch_.glyph_quads[glyph_begin].atlas_page_id
                : 0U;
            TextSystemType::ClearDirtyFlags(component,
                                            text_dirty_flag | style_dirty_flag | runtime_dirty_flag);
            ++stats.built_component_count;
        }

        stats.emitted_glyph_quad_count = static_cast<std::uint32_t>(scratch_.glyph_quads.size());
        stats.emitted_batch_count = static_cast<std::uint32_t>(scratch_.draw_batches.size());
        stats.face_variant_cache_entries = static_cast<std::uint32_t>(scratch_.face_variants.size());
        return stats;
    }

private:
    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_ + 0x9e3779b97f4a7c15ULL + (hash_ << 6U) + (hash_ >> 2U);
    }

    [[nodiscard]] static std::uint64_t ComputeVisibleSetSignature(
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_component_count_,
        bool use_candidate_indices_) noexcept {
        if (!use_candidate_indices_) {
            return 0U;
        }

        std::uint64_t hash = 0xa69d6f2c3e4b5871ULL;
        HashCombine(hash, static_cast<std::uint64_t>(candidate_component_count_));
        if (candidate_component_indices_ == nullptr) {
            HashCombine(hash, 0xffffffffffffffffULL);
            return hash;
        }

        for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
            HashCombine(hash, static_cast<std::uint64_t>(candidate_component_indices_[i]));
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t HashGlyphResolveRequest(std::uint32_t face_id_value_,
                                                                std::uint32_t codepoint_,
                                                                std::int32_t load_flags_,
                                                                text::GlyphRenderMode render_mode_) noexcept {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        hash ^= static_cast<std::uint64_t>(face_id_value_);
        hash *= 0x100000001b3ULL;
        hash ^= static_cast<std::uint64_t>(codepoint_);
        hash *= 0x100000001b3ULL;
        hash ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(load_flags_));
        hash *= 0x100000001b3ULL;
        hash ^= static_cast<std::uint64_t>(static_cast<std::uint8_t>(render_mode_));
        hash *= 0x100000001b3ULL;
        return hash;
    }

    [[nodiscard]] static std::uint32_t LowerBoundGlyphResolveCache(
        const TextRuntimeMcVector<typename ScratchType::GlyphResolveCacheEntry>& cache_,
        std::uint64_t hash_) noexcept {
        std::uint32_t first = 0U;
        std::uint32_t count = static_cast<std::uint32_t>(cache_.size());
        while (count > 0U) {
            const std::uint32_t step = count / 2U;
            const std::uint32_t it = first + step;
            if (cache_[it].hash < hash_) {
                first = it + 1U;
                count -= step + 1U;
            } else {
                count = step;
            }
        }
        return first;
    }

    [[nodiscard]] static text::GlyphRenderMode NormalizeBitmapRenderMode(
        text::GlyphRenderMode mode_) noexcept {
        switch (mode_) {
            case text::GlyphRenderMode::normal:
            case text::GlyphRenderMode::light:
            case text::GlyphRenderMode::mono:
                return mode_;
            default:
                return text::GlyphRenderMode::light;
        }
    }

    [[nodiscard]] static text::GlyphAtlasResolvedEntry ResolveGlyphCached(
        text::GlyphAtlasHost& atlas_host_,
        text::FontFaceId face_id_,
        std::uint32_t codepoint_,
        std::int32_t load_flags_,
        text::GlyphRenderMode render_mode_,
        ScratchType& scratch_) {
        const std::uint64_t hash = HashGlyphResolveRequest(face_id_.value,
                                                           codepoint_,
                                                           load_flags_,
                                                           render_mode_);
        const std::uint32_t insert_pos = LowerBoundGlyphResolveCache(scratch_.glyph_resolve_cache, hash);
        for (std::uint32_t i = insert_pos; i < scratch_.glyph_resolve_cache.size(); ++i) {
            const auto& entry = scratch_.glyph_resolve_cache[i];
            if (entry.hash != hash) {
                break;
            }
            if (entry.face_id_value == face_id_.value &&
                entry.codepoint == codepoint_ &&
                entry.load_flags == load_flags_ &&
                entry.render_mode == static_cast<std::uint8_t>(render_mode_)) {
                return entry.resolved;
            }
        }

        const text::GlyphAtlasResolvedEntry resolved = atlas_host_.ResolveGlyphWithFace(face_id_,
                                                                                         codepoint_,
                                                                                         load_flags_,
                                                                                         render_mode_);
        typename ScratchType::GlyphResolveCacheEntry cache_entry{};
        cache_entry.hash = hash;
        cache_entry.face_id_value = face_id_.value;
        cache_entry.codepoint = codepoint_;
        cache_entry.load_flags = load_flags_;
        cache_entry.render_mode = static_cast<std::uint8_t>(render_mode_);
        cache_entry.resolved = resolved;
        scratch_.glyph_resolve_cache.push_back(cache_entry);
        for (std::uint32_t i = static_cast<std::uint32_t>(scratch_.glyph_resolve_cache.size() - 1U);
             i > insert_pos;
             --i) {
            scratch_.glyph_resolve_cache[i] = scratch_.glyph_resolve_cache[i - 1U];
        }
        scratch_.glyph_resolve_cache[insert_pos] = cache_entry;
        return resolved;
    }

    [[nodiscard]] static std::uint32_t QuantizePixelHeight(const TextType& component_,
                                                           const TextRuntimeBuildConfig& build_config_) noexcept {
        float pixel_size = build_config_.min_pixel_size;

        if constexpr (std::same_as<DimensionT, Dim2>) {
            pixel_size = component_.style.pixel_size;
        } else {
            pixel_size = component_.style.world_size * build_config_.dim3_pixels_per_world_unit;
            if (component_.style.max_screen_size_px > 0.0F) {
                pixel_size = std::min(pixel_size, component_.style.max_screen_size_px);
            }
        }

        pixel_size = std::clamp(pixel_size,
                                build_config_.min_pixel_size,
                                build_config_.max_pixel_size);

        const float quant = std::max(0.25F, build_config_.pixel_size_quantization);
        const float quantized = std::round(pixel_size / quant) * quant;
        const float clamped = std::max(1.0F, quantized);
        return static_cast<std::uint32_t>(std::clamp(clamped, 1.0F, 2048.0F));
    }

    [[nodiscard]] static std::uint64_t MakeFaceVariantKey(std::uint32_t base_face_id_value_,
                                                           std::uint32_t pixel_height_) noexcept {
        return (static_cast<std::uint64_t>(base_face_id_value_) << 32U) |
               static_cast<std::uint64_t>(pixel_height_);
    }

    [[nodiscard]] static std::uint32_t LowerBoundFaceVariant(
        const TextRuntimeMcVector<FaceVariantEntry>& face_variants_,
        std::uint64_t key_) noexcept {
        std::uint32_t first = 0U;
        std::uint32_t count = static_cast<std::uint32_t>(face_variants_.size());
        while (count > 0U) {
            const std::uint32_t step = count / 2U;
            const std::uint32_t it = first + step;
            const auto& entry = face_variants_[it];
            const std::uint64_t probe = MakeFaceVariantKey(entry.base_face_id_value, entry.pixel_height);
            if (probe < key_) {
                first = it + 1U;
                count -= step + 1U;
            } else {
                count = step;
            }
        }
        return first;
    }

    [[nodiscard]] static text::FontFaceId AcquireVariantFaceId(
        std::uint32_t font_id_,
        std::uint32_t pixel_height_,
        text::GlyphAtlasHost& atlas_host_,
        text::FreeTypeHost& freetype_host_,
        ScratchType& scratch_) {
        if (font_id_ == 0U || pixel_height_ == 0U) {
            return {};
        }

        const text::FontFaceId base_face_id = atlas_host_.ResolveMappedFace(font_id_);
        if (!base_face_id.IsValid()) {
            return {};
        }

        const std::uint64_t key = MakeFaceVariantKey(base_face_id.value, pixel_height_);
        const std::uint32_t insert_pos = LowerBoundFaceVariant(scratch_.face_variants, key);
        if (insert_pos < scratch_.face_variants.size()) {
            const auto& existing = scratch_.face_variants[insert_pos];
            if (existing.base_face_id_value == base_face_id.value &&
                existing.pixel_height == pixel_height_ &&
                existing.face_id.IsValid()) {
                return existing.face_id;
            }
        }

        const text::FontFaceId variant_face_id = freetype_host_.AcquireFaceVariant(base_face_id,
                                                                                    pixel_height_,
                                                                                    0U);
        if (!variant_face_id.IsValid()) {
            return {};
        }

        FaceVariantEntry cache_entry{};
        cache_entry.font_id = font_id_;
        cache_entry.base_face_id_value = base_face_id.value;
        cache_entry.pixel_height = pixel_height_;
        cache_entry.face_id = variant_face_id;

        scratch_.face_variants.push_back(cache_entry);
        for (std::uint32_t i = static_cast<std::uint32_t>(scratch_.face_variants.size() - 1U);
             i > insert_pos;
             --i) {
            scratch_.face_variants[i] = scratch_.face_variants[i - 1U];
        }
        scratch_.face_variants[insert_pos] = cache_entry;
        return variant_face_id;
    }

    static void DecodeUtf8(std::string_view text_,
                           TextRuntimeMcVector<std::uint32_t>& out_codepoints_) {
        out_codepoints_.clear();

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(text_.data());
        const std::size_t size = text_.size();
        std::size_t index = 0U;

        while (index < size) {
            const std::uint8_t b0 = bytes[index];
            if (b0 < 0x80U) {
                out_codepoints_.push_back(static_cast<std::uint32_t>(b0));
                ++index;
                continue;
            }

            auto push_replacement = [&]() {
                out_codepoints_.push_back(0xFFFDUL);
            };

            if ((b0 & 0xE0U) == 0xC0U) {
                if (index + 1U >= size) {
                    push_replacement();
                    ++index;
                    continue;
                }
                const std::uint8_t b1 = bytes[index + 1U];
                if ((b1 & 0xC0U) != 0x80U) {
                    push_replacement();
                    ++index;
                    continue;
                }
                const std::uint32_t codepoint =
                    ((static_cast<std::uint32_t>(b0 & 0x1FU) << 6U) |
                     static_cast<std::uint32_t>(b1 & 0x3FU));
                if (codepoint < 0x80U) {
                    push_replacement();
                } else {
                    out_codepoints_.push_back(codepoint);
                }
                index += 2U;
                continue;
            }

            if ((b0 & 0xF0U) == 0xE0U) {
                if (index + 2U >= size) {
                    push_replacement();
                    ++index;
                    continue;
                }
                const std::uint8_t b1 = bytes[index + 1U];
                const std::uint8_t b2 = bytes[index + 2U];
                if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U) {
                    push_replacement();
                    ++index;
                    continue;
                }
                const std::uint32_t codepoint =
                    ((static_cast<std::uint32_t>(b0 & 0x0FU) << 12U) |
                     (static_cast<std::uint32_t>(b1 & 0x3FU) << 6U) |
                     static_cast<std::uint32_t>(b2 & 0x3FU));
                if (codepoint < 0x800U || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
                    push_replacement();
                } else {
                    out_codepoints_.push_back(codepoint);
                }
                index += 3U;
                continue;
            }

            if ((b0 & 0xF8U) == 0xF0U) {
                if (index + 3U >= size) {
                    push_replacement();
                    ++index;
                    continue;
                }
                const std::uint8_t b1 = bytes[index + 1U];
                const std::uint8_t b2 = bytes[index + 2U];
                const std::uint8_t b3 = bytes[index + 3U];
                if ((b1 & 0xC0U) != 0x80U ||
                    (b2 & 0xC0U) != 0x80U ||
                    (b3 & 0xC0U) != 0x80U) {
                    push_replacement();
                    ++index;
                    continue;
                }
                const std::uint32_t codepoint =
                    ((static_cast<std::uint32_t>(b0 & 0x07U) << 18U) |
                     (static_cast<std::uint32_t>(b1 & 0x3FU) << 12U) |
                     (static_cast<std::uint32_t>(b2 & 0x3FU) << 6U) |
                     static_cast<std::uint32_t>(b3 & 0x3FU));
                if (codepoint < 0x10000U || codepoint > 0x10FFFFU) {
                    push_replacement();
                } else {
                    out_codepoints_.push_back(codepoint);
                }
                index += 4U;
                continue;
            }

            push_replacement();
            ++index;
        }
    }

    [[nodiscard]] static float ResolveLineSpacing(const TextType& component_) noexcept {
        return std::max(0.25F, component_.style.line_spacing);
    }

    [[nodiscard]] static float ResolveLetterSpacing(const TextType& component_) noexcept {
        return component_.style.letter_spacing;
    }

    static void BuildRunGlyphs(TextType& component_,
                               text::FontFaceId face_id_,
                               text::GlyphAtlasHost& atlas_host_,
                               text::FreeTypeHost& freetype_host_,
                               const TextRuntimeBuildConfig& build_config_,
                               ScratchType& scratch_,
                               TextRuntimeBuildStats& stats_) {
        scratch_.run_glyphs.clear();
        scratch_.line_widths.clear();
        scratch_.line_x_offsets.clear();

        const text::FontFaceMetrics face_metrics = freetype_host_.FaceMetrics(face_id_);
        const float face_height_px = std::max(1.0F, static_cast<float>(face_metrics.height_26_6) / 64.0F);
        const float line_step = face_height_px * ResolveLineSpacing(component_);
        const float letter_spacing = ResolveLetterSpacing(component_);
        const std::uint32_t tab_count = std::max(1U, build_config_.tab_space_count);
        const bool request_sdf = component_.style.enable_sdf != 0U;
        const bool use_sdf = request_sdf && freetype_host_.SupportsSdfRasterization();
        const text::GlyphRenderMode bitmap_render_mode =
            NormalizeBitmapRenderMode(build_config_.bitmap_render_mode);
        const text::GlyphRenderMode render_mode = use_sdf
            ? text::GlyphRenderMode::sdf
            : bitmap_render_mode;
        const std::int32_t load_flags = build_config_.glyph_load_flags;

        scratch_.line_widths.push_back(0.0F);

        std::uint32_t line_index = 0U;
        float pen_x = 0.0F;
        std::uint32_t prev_codepoint = 0U;

        auto emit_codepoint = [&](std::uint32_t codepoint_) {
            float kerning_x = 0.0F;
            if (build_config_.enable_kerning &&
                prev_codepoint != 0U &&
                codepoint_ != 0U) {
                kerning_x = static_cast<float>(
                    freetype_host_.KerningX26_6(face_id_, prev_codepoint, codepoint_)) / 64.0F;
                if (kerning_x != 0.0F) {
                    ++stats_.kerning_apply_count;
                }
            }

            pen_x += kerning_x;

            const text::GlyphAtlasResolvedEntry resolved = ResolveGlyphCached(atlas_host_,
                                                                              face_id_,
                                                                              codepoint_,
                                                                              load_flags,
                                                                              render_mode,
                                                                              scratch_);
            ++stats_.glyph_resolve_count;

            typename ScratchType::RunGlyphRecord record{};
            record.resolved = resolved;
            record.line_index = line_index;
            record.pen_x = pen_x;
            record.codepoint = codepoint_;
            scratch_.run_glyphs.push_back(record);

            pen_x += static_cast<float>(resolved.metrics.advance_x_26_6) / 64.0F;
            pen_x += letter_spacing;
            prev_codepoint = codepoint_;
        };

        for (std::uint32_t codepoint : scratch_.utf32_codepoints) {
            if (codepoint == '\r') {
                continue;
            }
            if (codepoint == '\n') {
                scratch_.line_widths[line_index] = pen_x;
                ++line_index;
                scratch_.line_widths.push_back(0.0F);
                pen_x = 0.0F;
                prev_codepoint = 0U;
                continue;
            }
            if (codepoint == '\t') {
                for (std::uint32_t i = 0U; i < tab_count; ++i) {
                    emit_codepoint(static_cast<std::uint32_t>(' '));
                }
                continue;
            }

            emit_codepoint(codepoint);
        }
        scratch_.line_widths[line_index] = pen_x;

        scratch_.line_x_offsets.resize(scratch_.line_widths.size());
        for (std::uint32_t i = 0U; i < scratch_.line_widths.size(); ++i) {
            const float line_width = scratch_.line_widths[i];
            switch (component_.style.horizontal_align) {
                case TextHorizontalAlign::left:
                    scratch_.line_x_offsets[i] = 0.0F;
                    break;
                case TextHorizontalAlign::center:
                    scratch_.line_x_offsets[i] = -line_width * 0.5F;
                    break;
                case TextHorizontalAlign::right:
                    scratch_.line_x_offsets[i] = -line_width;
                    break;
                default:
                    scratch_.line_x_offsets[i] = 0.0F;
                    break;
            }
        }

        const std::uint32_t line_count = static_cast<std::uint32_t>(scratch_.line_widths.size());
        const float total_height = line_count > 0U
            ? static_cast<float>(line_count - 1U) * line_step + face_height_px
            : face_height_px;

        float vertical_offset = 0.0F;
        switch (component_.style.vertical_align) {
            case TextVerticalAlign::top:
                vertical_offset = 0.0F;
                break;
            case TextVerticalAlign::center:
                vertical_offset = -total_height * 0.5F;
                break;
            case TextVerticalAlign::bottom:
                vertical_offset = -total_height;
                break;
            default:
                vertical_offset = 0.0F;
                break;
        }

        const float baseline0 = static_cast<float>(face_metrics.ascender_26_6) / 64.0F;
        for (auto& run_glyph : scratch_.run_glyphs) {
            const float line_base_y = baseline0 +
                                      static_cast<float>(run_glyph.line_index) * line_step +
                                      vertical_offset;
            run_glyph.pen_x += scratch_.line_x_offsets[run_glyph.line_index];
            run_glyph.baseline_y = line_base_y;
        }
    }

    [[nodiscard]] static std::uint64_t SortKeyWithAtlas(const TextType& component_,
                                                        std::uint32_t atlas_page_id_) noexcept {
        constexpr std::uint64_t atlas_mask = TextSystemType::sort_key_atlas_mask;
        constexpr std::uint32_t atlas_shift = TextSystemType::sort_key_atlas_shift;

        std::uint64_t sort_key = component_.runtime.sort_key;
        sort_key &= ~(atlas_mask << atlas_shift);
        sort_key |= ((static_cast<std::uint64_t>(atlas_page_id_) & atlas_mask) << atlas_shift);
        return sort_key;
    }

    static void AppendOrMergeBatch(ScratchType& scratch_,
                                   std::uint64_t sort_key_,
                                   std::uint32_t glyph_begin_,
                                   std::uint32_t glyph_count_,
                                   std::uint32_t atlas_page_id_,
                                   std::uint32_t font_id_,
                                   std::uint32_t material_id_,
                                   std::uint32_t component_index_) {
        if (glyph_count_ == 0U) {
            return;
        }

        if (!scratch_.draw_batches.empty()) {
            TextDrawBatch& last = scratch_.draw_batches.back();
            if (last.sort_key == sort_key_ &&
                last.atlas_page_id == atlas_page_id_ &&
                last.font_id == font_id_ &&
                last.material_id == material_id_ &&
                last.glyph_begin + last.glyph_count == glyph_begin_) {
                last.glyph_count += glyph_count_;
                return;
            }
        }

        TextDrawBatch batch{};
        batch.sort_key = sort_key_;
        batch.glyph_begin = glyph_begin_;
        batch.glyph_count = glyph_count_;
        batch.atlas_page_id = atlas_page_id_;
        batch.font_id = font_id_;
        batch.material_id = material_id_;
        batch.first_component_index = component_index_;
        batch.reserved0 = 0U;
        scratch_.draw_batches.push_back(batch);
    }

    static void EmitGlyphQuadsAndBatches(TextType& component_,
                                         std::uint32_t component_index_,
                                         float glyph_world_scale_,
                                         ScratchType& scratch_,
                                         TextRuntimeBuildStats& stats_) {
        const std::uint32_t glyph_begin = static_cast<std::uint32_t>(scratch_.glyph_quads.size());
        std::uint32_t segment_begin = glyph_begin;
        std::uint32_t segment_count = 0U;
        std::uint32_t segment_page = 0U;
        std::uint64_t segment_sort_key = 0U;
        bool segment_active = false;

        for (const auto& run_glyph : scratch_.run_glyphs) {
            const auto& resolved = run_glyph.resolved;
            if (!resolved.region.HasAtlasPixels()) {
                continue;
            }

            const float units_scale = std::max(1e-6F, glyph_world_scale_);

            TextGlyphQuad quad{};
            const float bearing_left = static_cast<float>(resolved.metrics.bearing_left) * units_scale;
            const float bearing_top = static_cast<float>(resolved.metrics.bearing_top) * units_scale;
            const float glyph_width = static_cast<float>(resolved.region.pixel_rect.width) * units_scale;
            const float glyph_height = static_cast<float>(resolved.region.pixel_rect.height) * units_scale;
            const float baseline_y = run_glyph.baseline_y * units_scale;

            quad.x0 = run_glyph.pen_x * units_scale + bearing_left;
            quad.y0 = baseline_y - bearing_top;
            quad.x1 = quad.x0 + glyph_width;
            quad.y1 = quad.y0 + glyph_height;

            quad.u0 = resolved.region.uv_rect.u0;
            quad.v0 = resolved.region.uv_rect.v0;
            quad.u1 = resolved.region.uv_rect.u1;
            quad.v1 = resolved.region.uv_rect.v1;

            const bool glyph_is_sdf = component_.style.enable_sdf != 0U &&
                                      resolved.pixel_mode == text::GlyphPixelMode::sdf;

            quad.color = component_.style.color;
            quad.sdf_enabled = glyph_is_sdf ? 1U : 0U;
            quad.outline_enabled = (glyph_is_sdf && component_.style.enable_outline != 0U) ? 1U : 0U;
            quad.outline_width_px = quad.outline_enabled != 0U
                ? component_.style.outline_width_px
                : 0U;
            quad.reserved0 = 0U;
            quad.outline_color = component_.style.outline_color;
            quad.atlas_page_id = resolved.region.page_index;
            quad.component_index = component_index_;
            quad.glyph_index = resolved.glyph_index;
            quad.user_data = component_.runtime.user_data;

            const std::uint64_t sort_key = SortKeyWithAtlas(component_, quad.atlas_page_id);

            scratch_.glyph_quads.push_back(quad);
            ++stats_.emitted_glyph_quad_count;

            if (!segment_active) {
                segment_active = true;
                segment_begin = static_cast<std::uint32_t>(scratch_.glyph_quads.size() - 1U);
                segment_count = 1U;
                segment_page = quad.atlas_page_id;
                segment_sort_key = sort_key;
                continue;
            }

            if (segment_page == quad.atlas_page_id &&
                segment_sort_key == sort_key &&
                segment_begin + segment_count ==
                    static_cast<std::uint32_t>(scratch_.glyph_quads.size() - 1U)) {
                ++segment_count;
                continue;
            }

            AppendOrMergeBatch(scratch_,
                               segment_sort_key,
                               segment_begin,
                               segment_count,
                               segment_page,
                               component_.runtime.font_id,
                               component_.runtime.material_id,
                               component_index_);

            segment_begin = static_cast<std::uint32_t>(scratch_.glyph_quads.size() - 1U);
            segment_count = 1U;
            segment_page = quad.atlas_page_id;
            segment_sort_key = sort_key;
        }

        if (segment_active && segment_count > 0U) {
            AppendOrMergeBatch(scratch_,
                               segment_sort_key,
                               segment_begin,
                               segment_count,
                               segment_page,
                               component_.runtime.font_id,
                               component_.runtime.material_id,
                               component_index_);
        }

    }
};

} // namespace vr::ecs
