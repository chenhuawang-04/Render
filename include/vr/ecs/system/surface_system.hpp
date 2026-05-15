#pragma once

#include "vr/ecs/component/surface_component.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/ecs/system/visual_runtime_route_common.hpp"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <limits>

namespace vr::ecs {

struct SurfaceImageSourceDesc final {
    std::uint32_t surface_id = 0U;
    std::uint32_t atlas_page_id = 0U;
};

struct SurfaceSpriteSourceDesc final {
    std::uint32_t surface_id = 0U;
    std::uint32_t atlas_page_id = 0U;
};

struct SurfaceSampledSource3DDesc final {
    std::uint32_t surface_id = 0U;
    std::uint32_t sampler_id = 0U;
    std::uint16_t uv_set = 0U;
    std::uint16_t flags = 0U;
};

template<DimensionTag DimensionT>
class SurfaceSystem final {
public:
    using SurfaceType = Surface<DimensionT>;
    using RuntimeType = typename SurfaceType::RuntimeType;

    [[nodiscard]] static std::uint64_t AppearanceHandleMutationSerial() noexcept {
        return appearance_handle_mutation_serial.load(std::memory_order_relaxed);
    }

    // 64-bit sort key layout (MSB -> LSB):
    // [pass:2][visual_resource:16][surface:16][minor:16][batch:14]
    static constexpr std::uint32_t sort_key_batch_bits = 14U;
    static constexpr std::uint32_t sort_key_minor_bits = 16U;
    static constexpr std::uint32_t sort_key_surface_bits = 16U;
    static constexpr std::uint32_t sort_key_visual_resource_bits = 16U;
    static constexpr std::uint32_t sort_key_pass_bits = 2U;

    static constexpr std::uint32_t sort_key_batch_shift = 0U;
    static constexpr std::uint32_t sort_key_minor_shift = sort_key_batch_shift + sort_key_batch_bits;
    static constexpr std::uint32_t sort_key_surface_shift = sort_key_minor_shift + sort_key_minor_bits;
    static constexpr std::uint32_t sort_key_visual_resource_shift = sort_key_surface_shift + sort_key_surface_bits;
    static constexpr std::uint32_t sort_key_pass_shift = sort_key_visual_resource_shift + sort_key_visual_resource_bits;

    static constexpr std::uint32_t sort_key_binding_shift = sort_key_surface_shift;

    static constexpr std::uint64_t sort_key_batch_mask = (std::uint64_t{1U} << sort_key_batch_bits) - 1U;
    static constexpr std::uint64_t sort_key_minor_mask = (std::uint64_t{1U} << sort_key_minor_bits) - 1U;
    static constexpr std::uint64_t sort_key_surface_mask = (std::uint64_t{1U} << sort_key_surface_bits) - 1U;
    static constexpr std::uint64_t sort_key_visual_resource_mask = (std::uint64_t{1U} << sort_key_visual_resource_bits) - 1U;
    static constexpr std::uint64_t sort_key_pass_mask = (std::uint64_t{1U} << sort_key_pass_bits) - 1U;

    static_assert(sort_key_pass_bits + sort_key_visual_resource_bits + sort_key_surface_bits +
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
            component_.style.flip_x = 0U;
            component_.style.flip_y = 0U;
            component_.style.reserved0 = 0U;
            component_.style.reserved1 = 0U;
        } else {
            component_.style.uv_scale_u = 1.0F;
            component_.style.uv_scale_v = 1.0F;
            component_.style.uv_bias_u = 0.0F;
            component_.style.uv_bias_v = 0.0F;
            component_.style.filter_mode = Surface3DFilterMode::linear;
            component_.style.address_u = Surface3DAddressMode::wrap;
            component_.style.address_v = Surface3DAddressMode::wrap;
            component_.style.address_w = Surface3DAddressMode::wrap;
            component_.style.reserved0 = 0U;
            component_.style.reserved1 = 0U;
        }

        MarkDirty(component_, surface_dirty_style_flag | surface_dirty_runtime_flag);
    }

    static void SetDefaultRuntime(SurfaceType& component_) noexcept {
        InitializeVisualRuntimeRouteCommon(
            component_.runtime.route,
            std::same_as<DimensionT, Dim2> ? SurfaceRenderPassHint::overlay
                                           : SurfaceRenderPassHint::opaque,
            surface_dirty_source_flag |
                surface_dirty_style_flag |
                surface_dirty_runtime_flag);
        component_.runtime.route.surface_id = 0U;

        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.runtime.source.surface_id = 0U;
            component_.runtime.source.atlas_page_id = 0U;
            component_.runtime.source.source_kind = Surface2DSourceKind::none;
            component_.runtime.source.reserved0 = 0U;
            component_.runtime.source.reserved1 = 0U;
            component_.runtime.source_revision = 0U;
            component_.runtime.reserved0 = 0U;
            (void)WriteAppearanceRuntimeBridgeState(component_.runtime,
                                                    MakeAppearanceRuntimeBridge2D(nullptr));
            component_.runtime.size = Float2{.x = 0.0F, .y = 0.0F};
            component_.runtime.pivot = Float2{.x = 0.5F, .y = 0.5F};
        } else {
            component_.runtime.source.surface_id = 0U;
            component_.runtime.source.sampler_id = 0U;
            component_.runtime.source.uv_set = 0U;
            component_.runtime.source.flags = 0U;
            component_.runtime.source_revision = 0U;
            (void)WriteAppearanceRuntimeBridgeState(component_.runtime,
                                                    MakeAppearanceRuntimeBridge3D(nullptr));
        }
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const SurfaceType& component_) noexcept {
        return VisualRuntimeRouteDirtyFlags(component_.runtime.route);
    }

    [[nodiscard]] static bool HasDirtyFlags(const SurfaceType& component_,
                                            std::uint32_t dirty_mask_) noexcept {
        return HasVisualRuntimeRouteDirtyFlags(component_.runtime.route, dirty_mask_);
    }

    static void MarkDirty(SurfaceType& component_, std::uint32_t dirty_mask_) noexcept {
        MarkVisualRuntimeRouteDirty(component_.runtime.route, dirty_mask_);
    }

    static void ClearDirtyFlags(SurfaceType& component_, std::uint32_t clear_mask_) noexcept {
        ClearVisualRuntimeRouteDirtyFlags(component_.runtime.route, clear_mask_);
    }

    static void SetVisible(SurfaceType& component_, bool visible_) noexcept {
        if (!SetVisualRuntimeRouteVisible(component_.runtime.route, visible_)) {
            return;
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
    }

    static void SetRenderPassHint(SurfaceType& component_,
                                  SurfaceRenderPassHint pass_hint_) noexcept {
        if (!SetVisualRuntimeRoutePassHint(component_.runtime.route, pass_hint_)) {
            return;
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetRuntimeRoute(SurfaceType& component_,
                                std::uint32_t surface_id_,
                                std::uint32_t authoring_visual_resource_id_,
                                std::uint32_t batch_tag_) noexcept {
        if (component_.runtime.route.surface_id == surface_id_ &&
            component_.runtime.route.authoring_visual_resource_id == authoring_visual_resource_id_ &&
            component_.runtime.route.batch_tag == batch_tag_) {
            return;
        }
        component_.runtime.route.surface_id = surface_id_;
        component_.runtime.route.authoring_visual_resource_id = authoring_visual_resource_id_;
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

    static void SetAuthoringVisualResourceId(SurfaceType& component_,
                                             std::uint32_t authoring_visual_resource_id_) noexcept {
        if (!SetVisualRuntimeRouteAuthoringVisualResourceId(component_.runtime.route,
                                                            authoring_visual_resource_id_)) {
            return;
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetBatchTag(SurfaceType& component_, std::uint32_t batch_tag_) noexcept {
        if (!SetVisualRuntimeRouteBatchTag(component_.runtime.route, batch_tag_)) {
            return;
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetUserData(SurfaceType& component_, std::uint32_t user_data_) noexcept {
        if (!SetVisualRuntimeRouteUserData(component_.runtime.route, user_data_)) {
            return;
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
    }

    static void SetAppearanceHandle(SurfaceType& component_,
                                    AppearanceHandle appearance_handle_) noexcept {
        if (!SetVisualRuntimeRouteAppearanceHandle(component_.runtime.route,
                                                   appearance_handle_)) {
            return;
        }
        BumpAppearanceHandleMutationSerial();
        MarkDirty(component_, surface_dirty_runtime_flag);
    }

    static void ClearAppearanceHandle(SurfaceType& component_) noexcept {
        if (IsAppearanceRuntimeRouteClear(component_.runtime.route)) {
            return;
        }
        ClearAppearanceRuntimeRoute(component_.runtime.route);
        if constexpr (std::same_as<DimensionT, Dim2>) {
            (void)WriteAppearanceRuntimeBridgeState(component_.runtime,
                                                    MakeAppearanceRuntimeBridge2D(nullptr));
        } else {
            (void)WriteAppearanceRuntimeBridgeState(component_.runtime,
                                                    MakeAppearanceRuntimeBridge3D(nullptr));
        }
        BumpAppearanceHandleMutationSerial();
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    [[nodiscard]] static bool SetAppearanceRuntimeLink(SurfaceType& component_,
                                                       AppearanceHandle appearance_handle_,
                                                       std::uint64_t appearance_sort_key_,
                                                       std::uint64_t appearance_pipeline_key_,
                                                       std::uint64_t appearance_resource_key_) noexcept {
        (void)appearance_sort_key_;
        const VisualRuntimeRouteLinkMutation link_mutation =
            UpdateVisualRuntimeRouteLink(component_.runtime.route,
                                         appearance_handle_,
                                         appearance_pipeline_key_,
                                         appearance_resource_key_);
        if (!link_mutation.route_changed) {
            return false;
        }

        if (link_mutation.handle_changed) {
            BumpAppearanceHandleMutationSerial();
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool SetAppearanceRuntimeLink(SurfaceType& component_,
                                                       AppearanceHandle appearance_handle_,
                                                       std::uint64_t appearance_sort_key_,
                                                       std::uint64_t appearance_pipeline_key_,
                                                       std::uint64_t appearance_resource_key_,
                                                       const AppearanceStyle2D* appearance_style_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        (void)appearance_sort_key_;
        const VisualRuntimeRouteLinkMutation link_mutation =
            UpdateVisualRuntimeRouteLink(component_.runtime.route,
                                         appearance_handle_,
                                         appearance_pipeline_key_,
                                         appearance_resource_key_);
        const bool appearance_state_changed =
            WriteAppearanceRuntimeBridgeState(component_.runtime, appearance_style_);
        if (!link_mutation.route_changed && !appearance_state_changed) {
            return false;
        }

        if (link_mutation.handle_changed) {
            BumpAppearanceHandleMutationSerial();
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool SetAppearanceRuntimeLink(SurfaceType& component_,
                                                       AppearanceHandle appearance_handle_,
                                                       std::uint64_t appearance_sort_key_,
                                                       std::uint64_t appearance_pipeline_key_,
                                                       std::uint64_t appearance_resource_key_,
                                                       const AppearanceStyle3D* appearance_style_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        (void)appearance_sort_key_;
        const VisualRuntimeRouteLinkMutation link_mutation =
            UpdateVisualRuntimeRouteLink(component_.runtime.route,
                                         appearance_handle_,
                                         appearance_pipeline_key_,
                                         appearance_resource_key_);
        const bool appearance_state_changed =
            WriteAppearanceRuntimeBridgeState(component_.runtime, appearance_style_);
        if (!link_mutation.route_changed && !appearance_state_changed) {
            return false;
        }

        if (link_mutation.handle_changed) {
            BumpAppearanceHandleMutationSerial();
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeState(SurfaceType& component_,
                                                          const AppearanceStyle2D& appearance_style_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        return ApplyAppearanceRuntimeState(component_, &appearance_style_);
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeState(SurfaceType& component_,
                                                          const AppearanceStyle2D* appearance_style_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const bool changed = WriteAppearanceRuntimeBridgeState(component_.runtime, appearance_style_);
        if (!changed) {
            return false;
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeState(SurfaceType& component_,
                                                          const AppearanceStyle3D& appearance_style_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        return ApplyAppearanceRuntimeState(component_, &appearance_style_);
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeState(SurfaceType& component_,
                                                          const AppearanceStyle3D* appearance_style_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const bool changed = WriteAppearanceRuntimeBridgeState(component_.runtime, appearance_style_);
        if (!changed) {
            return false;
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeBridgeState(
        SurfaceType& component_,
        const AppearanceRuntimeBridge2D& appearance_bridge_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (!WriteAppearanceRuntimeBridgeState(component_.runtime, appearance_bridge_)) {
            return false;
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeBridgeState(
        SurfaceType& component_,
        const AppearanceRuntimeBridge3D& appearance_bridge_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (!WriteAppearanceRuntimeBridgeState(component_.runtime, appearance_bridge_)) {
            return false;
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    static void SetDepthBin(SurfaceType& component_, std::uint16_t depth_bin_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (!SetVisualRuntimeRouteDepthBin(component_.runtime.route, depth_bin_)) {
            return;
        }
        MarkDirty(component_, surface_dirty_runtime_flag);
        RebuildSortKey(component_);
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

    static void SetSource(SurfaceType& component_,
                          const SurfaceImageSourceDesc& source_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const bool changed =
            component_.runtime.source.surface_id != source_.surface_id ||
            component_.runtime.source.atlas_page_id != source_.atlas_page_id ||
            component_.runtime.source.source_kind != Surface2DSourceKind::image ||
            component_.runtime.route.surface_id != source_.surface_id;
        if (!changed) {
            return;
        }

        component_.runtime.source.surface_id = source_.surface_id;
        component_.runtime.source.atlas_page_id = source_.atlas_page_id;
        component_.runtime.source.source_kind = Surface2DSourceKind::image;
        component_.runtime.route.surface_id = source_.surface_id;
        ++component_.runtime.source_revision;
        MarkDirty(component_, surface_dirty_source_flag | surface_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetSource(SurfaceType& component_,
                          const SurfaceSpriteSourceDesc& source_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const bool changed =
            component_.runtime.source.surface_id != source_.surface_id ||
            component_.runtime.source.atlas_page_id != source_.atlas_page_id ||
            component_.runtime.source.source_kind != Surface2DSourceKind::sprite ||
            component_.runtime.route.surface_id != source_.surface_id;
        if (!changed) {
            return;
        }

        component_.runtime.source.surface_id = source_.surface_id;
        component_.runtime.source.atlas_page_id = source_.atlas_page_id;
        component_.runtime.source.source_kind = Surface2DSourceKind::sprite;
        component_.runtime.route.surface_id = source_.surface_id;
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

    static void SetSource(SurfaceType& component_,
                          const SurfaceSampledSource3DDesc& source_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const bool changed =
            component_.runtime.source.surface_id != source_.surface_id ||
            component_.runtime.source.sampler_id != source_.sampler_id ||
            component_.runtime.source.uv_set != source_.uv_set ||
            component_.runtime.source.flags != source_.flags ||
            component_.runtime.route.surface_id != source_.surface_id;
        if (!changed) {
            return;
        }
        component_.runtime.source.surface_id = source_.surface_id;
        component_.runtime.source.sampler_id = source_.sampler_id;
        component_.runtime.source.uv_set = source_.uv_set;
        component_.runtime.source.flags = source_.flags;
        component_.runtime.route.surface_id = source_.surface_id;
        ++component_.runtime.source_revision;
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
        const bool has_linked_appearance = HasLinkedAppearanceHandle(component_.runtime.route);
        SurfaceRenderPassHint effective_pass_hint = component_.runtime.route.pass_hint;
        if (has_linked_appearance) {
            effective_pass_hint = ResolveLinkedPassHint(component_.runtime.route.pass_hint,
                                                        component_.runtime.route.appearance_pipeline_bucket);
        } else if constexpr (std::same_as<DimensionT, Dim2>) {
            effective_pass_hint = ResolveFallbackAppearancePassHint(
                component_.runtime.route.pass_hint,
                ReadAppearanceRuntimeBridge2D(component_.runtime));
        } else {
            effective_pass_hint = ResolveFallbackAppearancePassHint(
                component_.runtime.route.pass_hint,
                ReadAppearanceRuntimeBridge3D(component_.runtime));
        }
        const std::uint64_t pass_bits = static_cast<std::uint64_t>(SortPassBucket(effective_pass_hint)) &
                                        sort_key_pass_mask;
        const std::uint64_t visual_resource_bits =
            static_cast<std::uint64_t>(ResolveEffectiveVisualResourceId(component_.runtime.route)) &
            sort_key_visual_resource_mask;
        const std::uint64_t surface_bits =
            static_cast<std::uint64_t>(ResolveSortSurfaceId(component_, has_linked_appearance)) &
            sort_key_surface_mask;
        const std::uint64_t batch_bits =
            static_cast<std::uint64_t>(component_.runtime.route.batch_tag) & sort_key_batch_mask;

        std::uint64_t minor_bits = 0U;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const std::int32_t shifted_layer = static_cast<std::int32_t>(
                                                   ReadAppearanceRuntimeBridge2D(component_.runtime).layer) -
                                               static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min());
            minor_bits = static_cast<std::uint64_t>(static_cast<std::uint32_t>(shifted_layer)) & sort_key_minor_mask;
        } else {
            minor_bits = static_cast<std::uint64_t>(
                EncodeDepthMinorBucket(component_.runtime.route.depth_bin, effective_pass_hint)) &
                sort_key_minor_mask;
        }

        std::uint64_t key = 0U;
        key |= (pass_bits << sort_key_pass_shift);
        key |= (visual_resource_bits << sort_key_visual_resource_shift);
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
        return static_cast<std::uint32_t>(PassHintFromSortBucket<SurfaceRenderPassHint>(
            static_cast<std::uint32_t>((sort_key_ >> sort_key_pass_shift) & sort_key_pass_mask)));
    }

    [[nodiscard]] static std::uint32_t ExtractVisualResourceBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_visual_resource_shift) & sort_key_visual_resource_mask);
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
                    return component_.runtime.source.surface_id != 0U;
                case Surface2DSourceKind::sprite:
                    return component_.runtime.source.surface_id != 0U;
                case Surface2DSourceKind::none:
                default:
                    return component_.runtime.route.surface_id != 0U;
            }
        } else {
            return HasLinkedAppearanceHandle(component_.runtime.route) ||
                   component_.runtime.source.surface_id != 0U;
        }
    }

private:
    [[nodiscard]] static std::uint32_t ResolveSortSurfaceId(
        const SurfaceType& component_,
        bool has_linked_appearance_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            (void)has_linked_appearance_;
            return component_.runtime.route.surface_id;
        } else {
            return has_linked_appearance_ ? 0U : component_.runtime.route.surface_id;
        }
    }

    static void BumpAppearanceHandleMutationSerial() noexcept {
        (void)appearance_handle_mutation_serial.fetch_add(1U, std::memory_order_relaxed);
    }

    inline static std::atomic<std::uint64_t> appearance_handle_mutation_serial{1U};
};

} // namespace vr::ecs
