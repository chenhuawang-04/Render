#pragma once

#include "vr/ecs/component/surface_component.hpp"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <limits>

namespace vr::ecs {

template<DimensionTag DimensionT>
class SurfaceSystem final {
public:
    using SurfaceType = Surface<DimensionT>;
    using RuntimeType = typename SurfaceType::RuntimeType;

    [[nodiscard]] static std::uint64_t AppearanceHandleMutationSerial() noexcept {
        return appearance_handle_mutation_serial.load(std::memory_order_relaxed);
    }

    // 64-bit sort key layout (MSB -> LSB):
    // [pass:2][material:16][surface:16][minor:16][batch:14]
    static constexpr std::uint32_t sort_key_batch_bits = 14U;
    static constexpr std::uint32_t sort_key_minor_bits = 16U;
    static constexpr std::uint32_t sort_key_surface_bits = 16U;
    static constexpr std::uint32_t sort_key_material_bits = 16U;
    static constexpr std::uint32_t sort_key_pass_bits = 2U;

    static constexpr std::uint32_t sort_key_batch_shift = 0U;
    static constexpr std::uint32_t sort_key_minor_shift = sort_key_batch_shift + sort_key_batch_bits;
    static constexpr std::uint32_t sort_key_surface_shift = sort_key_minor_shift + sort_key_minor_bits;
    static constexpr std::uint32_t sort_key_material_shift = sort_key_surface_shift + sort_key_surface_bits;
    static constexpr std::uint32_t sort_key_pass_shift = sort_key_material_shift + sort_key_material_bits;

    static constexpr std::uint32_t sort_key_binding_shift = sort_key_surface_shift;

    static constexpr std::uint64_t sort_key_batch_mask = (std::uint64_t{1U} << sort_key_batch_bits) - 1U;
    static constexpr std::uint64_t sort_key_minor_mask = (std::uint64_t{1U} << sort_key_minor_bits) - 1U;
    static constexpr std::uint64_t sort_key_surface_mask = (std::uint64_t{1U} << sort_key_surface_bits) - 1U;
    static constexpr std::uint64_t sort_key_material_mask = (std::uint64_t{1U} << sort_key_material_bits) - 1U;
    static constexpr std::uint64_t sort_key_pass_mask = (std::uint64_t{1U} << sort_key_pass_bits) - 1U;

    static_assert(sort_key_pass_bits + sort_key_material_bits + sort_key_surface_bits +
                      sort_key_minor_bits + sort_key_batch_bits == 64U,
                  "SurfaceSystem sort-key bit layout must be exactly 64 bits");

    static void Initialize(SurfaceType& component_) noexcept {
        SetDefaultStyle(component_);
        SetDefaultRuntime(component_);
        RebuildSortKey(component_);
    }

    static void SetDefaultStyle(SurfaceType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.uv_u0 = 0.0F;
            component_.style.uv_v0 = 0.0F;
            component_.style.uv_u1 = 1.0F;
            component_.style.uv_v1 = 1.0F;
            component_.style.tint_color = Rgba8{255U, 255U, 255U, 255U};
            component_.style.opacity = 1.0F;
            component_.style.layer = 0;
            component_.style.blend_mode = Surface2DBlendMode::alpha;
            component_.style.flip_x = 0U;
            component_.style.flip_y = 0U;
            component_.style.premultiplied_alpha = 0U;
            component_.style.reserved0 = 0U;
        } else {
            component_.style.tint_color = Rgba8{255U, 255U, 255U, 255U};
            component_.style.uv_scale_u = 1.0F;
            component_.style.uv_scale_v = 1.0F;
            component_.style.uv_bias_u = 0.0F;
            component_.style.uv_bias_v = 0.0F;
            component_.style.opacity = 1.0F;
            component_.style.depth_test = 1U;
            component_.style.depth_write = 0U;
            component_.style.double_sided = 0U;
            component_.style.filter_mode = Surface3DFilterMode::linear;
            component_.style.address_u = Surface3DAddressMode::wrap;
            component_.style.address_v = Surface3DAddressMode::wrap;
            component_.style.address_w = Surface3DAddressMode::wrap;
            component_.style.reserved0 = 0U;
        }

        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetDefaultRuntime(SurfaceType& component_) noexcept {
        component_.runtime.route.sort_key = 0U;
        component_.runtime.route.surface_id = 0U;
        component_.runtime.route.material_id = 0U;
        component_.runtime.route.batch_tag = 0U;
        component_.runtime.route.user_data = 0U;
        component_.runtime.route.appearance_handle = invalid_appearance_handle;
        component_.runtime.route.appearance_pipeline_bucket = 0U;
        component_.runtime.route.appearance_resource_bucket = 0U;
        component_.runtime.route.depth_bin = 0U;
        component_.runtime.route.visible = 1U;
        component_.runtime.route.pass_hint = std::same_as<DimensionT, Dim2>
            ? SurfaceRenderPassHint::overlay
            : SurfaceRenderPassHint::opaque;
        component_.runtime.route.dirty_flags = surface_dirty_source_flag |
                                               surface_dirty_style_flag |
                                               surface_dirty_runtime_flag;

        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.runtime.source.image_id = 0U;
            component_.runtime.source.sprite_id = 0U;
            component_.runtime.source.atlas_page_id = 0U;
            component_.runtime.source.source_kind = Surface2DSourceKind::none;
            component_.runtime.source.reserved0 = 0U;
            component_.runtime.source.reserved1 = 0U;
            component_.runtime.source_revision = 0U;
            component_.runtime.reserved0 = 0U;
            component_.runtime.size = Float2{.x = 0.0F, .y = 0.0F};
            component_.runtime.pivot = Float2{.x = 0.5F, .y = 0.5F};
        } else {
            component_.runtime.texture.texture_id = 0U;
            component_.runtime.texture.sampler_id = 0U;
            component_.runtime.texture.uv_set = 0U;
            component_.runtime.texture.flags = 0U;
            component_.runtime.texture_revision = 0U;
            component_.runtime.reserved0 = 0U;
            component_.runtime.reserved1 = 0U;
        }
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const SurfaceType& component_) noexcept {
        return component_.runtime.route.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const SurfaceType& component_,
                                            std::uint32_t dirty_mask_) noexcept {
        return (component_.runtime.route.dirty_flags & dirty_mask_) != 0U;
    }

    static void MarkDirty(SurfaceType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.runtime.route.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(SurfaceType& component_, std::uint32_t clear_mask_) noexcept {
        component_.runtime.route.dirty_flags &= ~clear_mask_;
    }

    static void SetVisible(SurfaceType& component_, bool visible_) noexcept {
        const std::uint8_t visible_value = visible_ ? 1U : 0U;
        if (component_.runtime.route.visible == visible_value) {
            return;
        }
        component_.runtime.route.visible = visible_value;
        MarkDirty(component_, surface_dirty_runtime_flag);
    }

    static void SetRenderPassHint(SurfaceType& component_,
                                  SurfaceRenderPassHint pass_hint_) noexcept {
        if (component_.runtime.route.pass_hint == pass_hint_) {
            return;
        }
        component_.runtime.route.pass_hint = pass_hint_;
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetRuntimeRoute(SurfaceType& component_,
                                std::uint32_t surface_id_,
                                std::uint32_t material_id_,
                                std::uint32_t batch_tag_) noexcept {
        if (component_.runtime.route.surface_id == surface_id_ &&
            component_.runtime.route.material_id == material_id_ &&
            component_.runtime.route.batch_tag == batch_tag_) {
            return;
        }
        component_.runtime.route.surface_id = surface_id_;
        component_.runtime.route.material_id = material_id_;
        component_.runtime.route.batch_tag = batch_tag_;
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetSurfaceId(SurfaceType& component_, std::uint32_t surface_id_) noexcept {
        if (component_.runtime.route.surface_id == surface_id_) {
            return;
        }
        component_.runtime.route.surface_id = surface_id_;
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetMaterialId(SurfaceType& component_, std::uint32_t material_id_) noexcept {
        if (component_.runtime.route.material_id == material_id_) {
            return;
        }
        component_.runtime.route.material_id = material_id_;
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetBatchTag(SurfaceType& component_, std::uint32_t batch_tag_) noexcept {
        if (component_.runtime.route.batch_tag == batch_tag_) {
            return;
        }
        component_.runtime.route.batch_tag = batch_tag_;
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetUserData(SurfaceType& component_, std::uint32_t user_data_) noexcept {
        if (component_.runtime.route.user_data == user_data_) {
            return;
        }
        component_.runtime.route.user_data = user_data_;
        MarkDirty(component_, surface_dirty_runtime_flag);
    }

    static void SetAppearanceHandle(SurfaceType& component_,
                                    AppearanceHandle appearance_handle_) noexcept {
        if (component_.runtime.route.appearance_handle.index == appearance_handle_.index &&
            component_.runtime.route.appearance_handle.generation == appearance_handle_.generation) {
            return;
        }
        component_.runtime.route.appearance_handle = appearance_handle_;
        BumpAppearanceHandleMutationSerial();
        MarkDirty(component_, surface_dirty_runtime_flag);
    }

    static void ClearAppearanceHandle(SurfaceType& component_) noexcept {
        if (component_.runtime.route.appearance_handle.index == invalid_appearance_handle.index &&
            component_.runtime.route.appearance_handle.generation == invalid_appearance_handle.generation &&
            component_.runtime.route.appearance_pipeline_bucket == 0U &&
            component_.runtime.route.appearance_resource_bucket == 0U) {
            return;
        }
        component_.runtime.route.appearance_handle = invalid_appearance_handle;
        component_.runtime.route.appearance_pipeline_bucket = 0U;
        component_.runtime.route.appearance_resource_bucket = 0U;
        BumpAppearanceHandleMutationSerial();
        MarkDirty(component_, surface_dirty_runtime_flag);
    }

    [[nodiscard]] static bool SetAppearanceRuntimeLink(SurfaceType& component_,
                                                       AppearanceHandle appearance_handle_,
                                                       std::uint64_t appearance_sort_key_,
                                                       std::uint64_t appearance_pipeline_key_,
                                                       std::uint64_t appearance_resource_key_) noexcept {
        const std::uint32_t pipeline_bucket = static_cast<std::uint32_t>(appearance_pipeline_key_);
        const std::uint32_t resource_bucket = static_cast<std::uint32_t>(appearance_resource_key_);
        const bool changed =
            component_.runtime.route.appearance_handle.index != appearance_handle_.index ||
            component_.runtime.route.appearance_handle.generation != appearance_handle_.generation ||
            component_.runtime.route.appearance_pipeline_bucket != pipeline_bucket ||
            component_.runtime.route.appearance_resource_bucket != resource_bucket ||
            component_.runtime.route.material_id != resource_bucket ||
            component_.runtime.route.sort_key != appearance_sort_key_;
        if (!changed) {
            return false;
        }

        const bool handle_changed =
            component_.runtime.route.appearance_handle.index != appearance_handle_.index ||
            component_.runtime.route.appearance_handle.generation != appearance_handle_.generation;
        component_.runtime.route.appearance_handle = appearance_handle_;
        component_.runtime.route.appearance_pipeline_bucket = pipeline_bucket;
        component_.runtime.route.appearance_resource_bucket = resource_bucket;
        component_.runtime.route.material_id = resource_bucket;
        component_.runtime.route.sort_key = appearance_sort_key_;
        if (handle_changed) {
            BumpAppearanceHandleMutationSerial();
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        return true;
    }

    static void SetDepthBin(SurfaceType& component_, std::uint16_t depth_bin_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.runtime.route.depth_bin == depth_bin_) {
            return;
        }
        component_.runtime.route.depth_bin = depth_bin_;
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetOpacity(SurfaceType& component_, float opacity_) noexcept {
        const float clamped = std::clamp(opacity_, 0.0F, 1.0F);
        if (component_.style.opacity == clamped) {
            return;
        }
        component_.style.opacity = clamped;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetTintColor(SurfaceType& component_, Rgba8 tint_color_) noexcept {
        if (IsSameColor(component_.style.tint_color, tint_color_)) {
            return;
        }
        component_.style.tint_color = tint_color_;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetUvRect(SurfaceType& component_,
                          float u0_,
                          float v0_,
                          float u1_,
                          float v1_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.style.uv_u0 == u0_ &&
            component_.style.uv_v0 == v0_ &&
            component_.style.uv_u1 == u1_ &&
            component_.style.uv_v1 == v1_) {
            return;
        }
        component_.style.uv_u0 = u0_;
        component_.style.uv_v0 = v0_;
        component_.style.uv_u1 = u1_;
        component_.style.uv_v1 = v1_;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetLayer(SurfaceType& component_, std::int16_t layer_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.style.layer == layer_) {
            return;
        }
        component_.style.layer = layer_;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetBlendMode(SurfaceType& component_,
                             Surface2DBlendMode blend_mode_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.style.blend_mode == blend_mode_) {
            return;
        }
        component_.style.blend_mode = blend_mode_;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetFlip(SurfaceType& component_,
                        bool flip_x_,
                        bool flip_y_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const std::uint8_t flip_x_value = flip_x_ ? 1U : 0U;
        const std::uint8_t flip_y_value = flip_y_ ? 1U : 0U;
        if (component_.style.flip_x == flip_x_value &&
            component_.style.flip_y == flip_y_value) {
            return;
        }
        component_.style.flip_x = flip_x_value;
        component_.style.flip_y = flip_y_value;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetPremultipliedAlpha(SurfaceType& component_, bool enabled_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const std::uint8_t enabled_value = enabled_ ? 1U : 0U;
        if (component_.style.premultiplied_alpha == enabled_value) {
            return;
        }
        component_.style.premultiplied_alpha = enabled_value;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetImageId(SurfaceType& component_,
                           std::uint32_t image_id_,
                           std::uint32_t atlas_page_id_ = 0U) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const bool changed =
            component_.runtime.source.image_id != image_id_ ||
            component_.runtime.source.atlas_page_id != atlas_page_id_ ||
            component_.runtime.source.source_kind != Surface2DSourceKind::image ||
            component_.runtime.route.surface_id != image_id_;
        if (!changed) {
            return;
        }

        component_.runtime.source.image_id = image_id_;
        component_.runtime.source.atlas_page_id = atlas_page_id_;
        component_.runtime.source.source_kind = Surface2DSourceKind::image;
        component_.runtime.route.surface_id = image_id_;
        ++component_.runtime.source_revision;
        MarkDirty(component_, surface_dirty_source_flag | surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetSpriteId(SurfaceType& component_,
                            std::uint32_t sprite_id_,
                            std::uint32_t atlas_page_id_ = 0U) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const bool changed =
            component_.runtime.source.sprite_id != sprite_id_ ||
            component_.runtime.source.atlas_page_id != atlas_page_id_ ||
            component_.runtime.source.source_kind != Surface2DSourceKind::sprite ||
            component_.runtime.route.surface_id != sprite_id_;
        if (!changed) {
            return;
        }

        component_.runtime.source.sprite_id = sprite_id_;
        component_.runtime.source.atlas_page_id = atlas_page_id_;
        component_.runtime.source.source_kind = Surface2DSourceKind::sprite;
        component_.runtime.route.surface_id = sprite_id_;
        ++component_.runtime.source_revision;
        MarkDirty(component_, surface_dirty_source_flag | surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetAtlasPageId(SurfaceType& component_, std::uint32_t atlas_page_id_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.runtime.source.atlas_page_id == atlas_page_id_) {
            return;
        }
        component_.runtime.source.atlas_page_id = atlas_page_id_;
        ++component_.runtime.source_revision;
        MarkDirty(component_, surface_dirty_source_flag | surface_dirty_runtime_flag);
    }

    static void SetSize(SurfaceType& component_, const Float2& size_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.runtime.size.x == size_.x &&
            component_.runtime.size.y == size_.y) {
            return;
        }
        component_.runtime.size = size_;
        MarkDirty(component_, surface_dirty_runtime_flag);
    }

    static void SetPivot(SurfaceType& component_, const Float2& pivot_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.runtime.pivot.x == pivot_.x &&
            component_.runtime.pivot.y == pivot_.y) {
            return;
        }
        component_.runtime.pivot = pivot_;
        MarkDirty(component_, surface_dirty_runtime_flag);
    }

    static void SetTextureId(SurfaceType& component_, std::uint32_t texture_id_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const bool changed =
            component_.runtime.texture.texture_id != texture_id_ ||
            component_.runtime.route.surface_id != texture_id_;
        if (!changed) {
            return;
        }
        component_.runtime.texture.texture_id = texture_id_;
        component_.runtime.route.surface_id = texture_id_;
        ++component_.runtime.texture_revision;
        MarkDirty(component_, surface_dirty_source_flag | surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetSamplerId(SurfaceType& component_, std::uint32_t sampler_id_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.runtime.texture.sampler_id == sampler_id_) {
            return;
        }
        component_.runtime.texture.sampler_id = sampler_id_;
        ++component_.runtime.texture_revision;
        MarkDirty(component_, surface_dirty_source_flag | surface_dirty_runtime_flag);
    }

    static void SetTextureRoute(SurfaceType& component_,
                                std::uint32_t texture_id_,
                                std::uint32_t sampler_id_,
                                std::uint16_t uv_set_,
                                std::uint16_t texture_flags_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const bool changed =
            component_.runtime.texture.texture_id != texture_id_ ||
            component_.runtime.texture.sampler_id != sampler_id_ ||
            component_.runtime.texture.uv_set != uv_set_ ||
            component_.runtime.texture.flags != texture_flags_ ||
            component_.runtime.route.surface_id != texture_id_;
        if (!changed) {
            return;
        }
        component_.runtime.texture.texture_id = texture_id_;
        component_.runtime.texture.sampler_id = sampler_id_;
        component_.runtime.texture.uv_set = uv_set_;
        component_.runtime.texture.flags = texture_flags_;
        component_.runtime.route.surface_id = texture_id_;
        ++component_.runtime.texture_revision;
        MarkDirty(component_, surface_dirty_source_flag | surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetUvTransform(SurfaceType& component_,
                               float uv_scale_u_,
                               float uv_scale_v_,
                               float uv_bias_u_,
                               float uv_bias_v_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.style.uv_scale_u == uv_scale_u_ &&
            component_.style.uv_scale_v == uv_scale_v_ &&
            component_.style.uv_bias_u == uv_bias_u_ &&
            component_.style.uv_bias_v == uv_bias_v_) {
            return;
        }
        component_.style.uv_scale_u = uv_scale_u_;
        component_.style.uv_scale_v = uv_scale_v_;
        component_.style.uv_bias_u = uv_bias_u_;
        component_.style.uv_bias_v = uv_bias_v_;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetDepthTest(SurfaceType& component_, bool enabled_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t enabled_value = enabled_ ? 1U : 0U;
        if (component_.style.depth_test == enabled_value) {
            return;
        }
        component_.style.depth_test = enabled_value;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetDepthWrite(SurfaceType& component_, bool enabled_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t enabled_value = enabled_ ? 1U : 0U;
        if (component_.style.depth_write == enabled_value) {
            return;
        }
        component_.style.depth_write = enabled_value;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetDoubleSided(SurfaceType& component_, bool enabled_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t enabled_value = enabled_ ? 1U : 0U;
        if (component_.style.double_sided == enabled_value) {
            return;
        }
        component_.style.double_sided = enabled_value;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetFilterMode(SurfaceType& component_,
                              Surface3DFilterMode filter_mode_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.style.filter_mode == filter_mode_) {
            return;
        }
        component_.style.filter_mode = filter_mode_;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetAddressMode(SurfaceType& component_,
                               Surface3DAddressMode address_u_,
                               Surface3DAddressMode address_v_,
                               Surface3DAddressMode address_w_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.style.address_u == address_u_ &&
            component_.style.address_v == address_v_ &&
            component_.style.address_w == address_w_) {
            return;
        }
        component_.style.address_u = address_u_;
        component_.style.address_v = address_v_;
        component_.style.address_w = address_w_;
        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    [[nodiscard]] static std::uint64_t ComposeSortKey(const SurfaceType& component_) noexcept {
        const std::uint64_t pass_bits = static_cast<std::uint64_t>(component_.runtime.route.pass_hint) &
                                        sort_key_pass_mask;
        const std::uint64_t material_bits =
            static_cast<std::uint64_t>(component_.runtime.route.material_id) & sort_key_material_mask;
        const std::uint64_t surface_bits =
            static_cast<std::uint64_t>(component_.runtime.route.surface_id) & sort_key_surface_mask;
        const std::uint64_t batch_bits =
            static_cast<std::uint64_t>(component_.runtime.route.batch_tag) & sort_key_batch_mask;

        std::uint64_t minor_bits = 0U;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const std::int32_t shifted_layer = static_cast<std::int32_t>(component_.style.layer) -
                                               static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min());
            minor_bits = static_cast<std::uint64_t>(static_cast<std::uint32_t>(shifted_layer)) & sort_key_minor_mask;
        } else {
            minor_bits = static_cast<std::uint64_t>(component_.runtime.route.depth_bin) & sort_key_minor_mask;
        }

        std::uint64_t key = 0U;
        key |= (pass_bits << sort_key_pass_shift);
        key |= (material_bits << sort_key_material_shift);
        key |= (surface_bits << sort_key_surface_shift);
        key |= (minor_bits << sort_key_minor_shift);
        key |= (batch_bits << sort_key_batch_shift);
        return key;
    }

    static void RebuildSortKey(SurfaceType& component_) noexcept {
        component_.runtime.route.sort_key = ComposeSortKey(component_);
    }

    [[nodiscard]] static std::uint64_t SortKey(const SurfaceType& component_) noexcept {
        return component_.runtime.route.sort_key;
    }

    [[nodiscard]] static std::uint64_t BindingSortKey(const SurfaceType& component_) noexcept {
        return BindingSortKey(component_.runtime.route.sort_key);
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

    [[nodiscard]] static std::uint32_t ExtractSurfaceBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_surface_shift) & sort_key_surface_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractMinorBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_minor_shift) & sort_key_minor_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractBatchBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_batch_shift) & sort_key_batch_mask);
    }

    [[nodiscard]] static bool IsVisibleForBatch(const SurfaceType& component_) noexcept {
        if (component_.runtime.route.visible == 0U) {
            return false;
        }

        if constexpr (std::same_as<DimensionT, Dim2>) {
            switch (component_.runtime.source.source_kind) {
                case Surface2DSourceKind::image:
                    return component_.runtime.source.image_id != 0U;
                case Surface2DSourceKind::sprite:
                    return component_.runtime.source.sprite_id != 0U;
                case Surface2DSourceKind::none:
                default:
                    return component_.runtime.route.surface_id != 0U;
            }
        } else {
            return component_.runtime.texture.texture_id != 0U;
        }
    }

private:
    static void BumpAppearanceHandleMutationSerial() noexcept {
        (void)appearance_handle_mutation_serial.fetch_add(1U, std::memory_order_relaxed);
    }

    [[nodiscard]] static bool IsSameColor(const Rgba8& lhs_,
                                          const Rgba8& rhs_) noexcept {
        return lhs_.r == rhs_.r &&
               lhs_.g == rhs_.g &&
               lhs_.b == rhs_.b &&
               lhs_.a == rhs_.a;
    }

    inline static std::atomic<std::uint64_t> appearance_handle_mutation_serial{1U};
};

} // namespace vr::ecs
