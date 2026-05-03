#pragma once

#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace vr::ecs {

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
        component_.runtime.pass_hint = std::same_as<DimensionT, Dim2>
            ? TextRenderPassHint::overlay
            : TextRenderPassHint::transparent;
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
        const std::uint64_t pass_bits =
            static_cast<std::uint64_t>(SortPassBucket(component_.runtime.pass_hint)) & sort_key_pass_mask;
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
            minor_bits = static_cast<std::uint64_t>(
                EncodeDepthMinorBucket(component_.runtime.depth_bin, component_.runtime.pass_hint)) &
                sort_key_minor_mask;
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
        return static_cast<std::uint32_t>(PassHintFromSortBucket<TextRenderPassHint>(
            static_cast<std::uint32_t>((sort_key_ >> sort_key_pass_shift) & sort_key_pass_mask)));
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

} // namespace vr::ecs
