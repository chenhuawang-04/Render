#pragma once

#include "vr/ecs/component/geometry_component.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/ecs/system/visual_runtime_route_common.hpp"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <iterator>
#include <limits>

namespace vr::ecs {

template<DimensionTag DimensionT>
class GeometrySystem final {
public:
    using GeometryType = Geometry<DimensionT>;
    using RuntimeType = typename GeometryType::RuntimeType;

    [[nodiscard]] static std::uint64_t AppearanceHandleMutationSerial() noexcept {
        return appearance_handle_mutation_serial.load(std::memory_order_relaxed);
    }

    // 64-bit sort key layout (MSB -> LSB):
    // [pass:2][visual_resource:16][geometry:16][minor:16][batch:14]
    static constexpr std::uint32_t sort_key_batch_bits = 14U;
    static constexpr std::uint32_t sort_key_minor_bits = 16U;
    static constexpr std::uint32_t sort_key_geometry_bits = 16U;
    static constexpr std::uint32_t sort_key_visual_resource_bits = 16U;
    static constexpr std::uint32_t sort_key_pass_bits = 2U;

    static constexpr std::uint32_t sort_key_batch_shift = 0U;
    static constexpr std::uint32_t sort_key_minor_shift = sort_key_batch_shift + sort_key_batch_bits;
    static constexpr std::uint32_t sort_key_geometry_shift = sort_key_minor_shift + sort_key_minor_bits;
    static constexpr std::uint32_t sort_key_visual_resource_shift = sort_key_geometry_shift + sort_key_geometry_bits;
    static constexpr std::uint32_t sort_key_pass_shift = sort_key_visual_resource_shift + sort_key_visual_resource_bits;

    static constexpr std::uint32_t sort_key_binding_shift = sort_key_minor_shift;

    static constexpr std::uint64_t sort_key_batch_mask = (std::uint64_t{1U} << sort_key_batch_bits) - 1U;
    static constexpr std::uint64_t sort_key_minor_mask = (std::uint64_t{1U} << sort_key_minor_bits) - 1U;
    static constexpr std::uint64_t sort_key_geometry_mask = (std::uint64_t{1U} << sort_key_geometry_bits) - 1U;
    static constexpr std::uint64_t sort_key_visual_resource_mask = (std::uint64_t{1U} << sort_key_visual_resource_bits) - 1U;
    static constexpr std::uint64_t sort_key_pass_mask = (std::uint64_t{1U} << sort_key_pass_bits) - 1U;

    static_assert(sort_key_pass_bits + sort_key_visual_resource_bits + sort_key_geometry_bits +
                      sort_key_minor_bits + sort_key_batch_bits == 64U,
                  "GeometrySystem sort-key bit layout must be exactly 64 bits");

    static void Initialize(GeometryType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.path.size_bytes = 0U;
            component_.path.capacity_bytes = GeometryPathInlineData::inline_capacity_bytes;
            component_.path.revision = 0U;
            component_.path.reserved = 0U;
            std::fill(std::begin(component_.path.bytes), std::end(component_.path.bytes), std::uint8_t{0U});
        } else {
            component_.mesh.submesh_index = 0U;
            component_.mesh.lod_index = 0U;
            component_.mesh.flags = 0U;
        }

        SetDefaultStyle(component_);
        SetDefaultRuntime(component_);
        RebuildSortKey(component_);
    }

    static void SetDefaultStyle(GeometryType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.stroke_width_px = 1.0F;
            component_.style.miter_limit = 4.0F;
            component_.style.topology = Geometry2DTopology::fill;
            component_.style.fill_rule = Geometry2DFillRule::non_zero;
            component_.style.line_join = Geometry2DLineJoin::miter;
            component_.style.line_cap = Geometry2DLineCap::butt;
            component_.style.reserved0 = 0U;
            component_.style.reserved1 = 0U;
            component_.style.reserved2 = 0U;
        } else {
            component_.style.topology = Geometry3DTopology::triangles;
            component_.style.reserved0 = 0U;
            component_.style.reserved1 = 0U;
            component_.style.line_width = 1.0F;
        }

        MarkDirty(component_, geometry_dirty_style_flag | geometry_dirty_runtime_flag);
    }

    static void SetDefaultRuntime(GeometryType& component_) noexcept {
        InitializeVisualRuntimeRouteCommon(
            component_.runtime.route,
            std::same_as<DimensionT, Dim2> ? GeometryRenderPassHint::overlay
                                           : GeometryRenderPassHint::opaque,
            geometry_dirty_data_flag |
                geometry_dirty_style_flag |
                geometry_dirty_runtime_flag |
                geometry_dirty_bounds_flag);
        component_.runtime.route.geometry_id = 0U;

        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.runtime.path_command_count = 0U;
            component_.runtime.tessellation_revision = 0U;
            component_.runtime.path_data_hash = 0U;
            component_.runtime.reserved0 = 0U;
            (void)WriteAppearanceRuntimeBridgeState(component_.runtime,
                                                    MakeAppearanceRuntimeBridge2D(nullptr));
            component_.runtime.bounds_min = Float2{.x = 0.0F, .y = 0.0F};
            component_.runtime.bounds_max = Float2{.x = 0.0F, .y = 0.0F};
        } else {
            component_.runtime.mesh_revision = 0U;
            component_.runtime.meshlet_count_hint = 0U;
            (void)WriteAppearanceRuntimeBridgeState(component_.runtime,
                                                    MakeAppearanceRuntimeBridge3D(nullptr));
            component_.runtime.bounds_min = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
            component_.runtime.bounds_max = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        }
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const GeometryType& component_) noexcept {
        return VisualRuntimeRouteDirtyFlags(component_.runtime.route);
    }

    [[nodiscard]] static bool HasDirtyFlags(const GeometryType& component_,
                                            std::uint32_t dirty_mask_) noexcept {
        return HasVisualRuntimeRouteDirtyFlags(component_.runtime.route, dirty_mask_);
    }

    static void MarkDirty(GeometryType& component_, std::uint32_t dirty_mask_) noexcept {
        MarkVisualRuntimeRouteDirty(component_.runtime.route, dirty_mask_);
    }

    static void ClearDirtyFlags(GeometryType& component_, std::uint32_t clear_mask_) noexcept {
        ClearVisualRuntimeRouteDirtyFlags(component_.runtime.route, clear_mask_);
    }

    static void SetVisible(GeometryType& component_, bool visible_) noexcept {
        if (!SetVisualRuntimeRouteVisible(component_.runtime.route, visible_)) {
            return;
        }
        MarkDirty(component_, geometry_dirty_runtime_flag);
    }

    static void SetRenderPassHint(GeometryType& component_,
                                  GeometryRenderPassHint pass_hint_) noexcept {
        if (!SetVisualRuntimeRoutePassHint(component_.runtime.route, pass_hint_)) {
            return;
        }
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetRuntimeRoute(GeometryType& component_,
                                std::uint32_t geometry_id_,
                                std::uint32_t authoring_visual_resource_id_,
                                std::uint32_t batch_tag_) noexcept {
        if (component_.runtime.route.geometry_id == geometry_id_ &&
            component_.runtime.route.authoring_visual_resource_id == authoring_visual_resource_id_ &&
            component_.runtime.route.batch_tag == batch_tag_) {
            return;
        }
        component_.runtime.route.geometry_id = geometry_id_;
        component_.runtime.route.authoring_visual_resource_id = authoring_visual_resource_id_;
        component_.runtime.route.batch_tag = batch_tag_;
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetGeometryId(GeometryType& component_, std::uint32_t geometry_id_) noexcept {
        if (component_.runtime.route.geometry_id == geometry_id_) {
            return;
        }
        component_.runtime.route.geometry_id = geometry_id_;
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetAuthoringVisualResourceId(GeometryType& component_,
                                             std::uint32_t authoring_visual_resource_id_) noexcept {
        if (!SetVisualRuntimeRouteAuthoringVisualResourceId(component_.runtime.route,
                                                            authoring_visual_resource_id_)) {
            return;
        }
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetBatchTag(GeometryType& component_, std::uint32_t batch_tag_) noexcept {
        if (!SetVisualRuntimeRouteBatchTag(component_.runtime.route, batch_tag_)) {
            return;
        }
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetUserData(GeometryType& component_, std::uint32_t user_data_) noexcept {
        if (!SetVisualRuntimeRouteUserData(component_.runtime.route, user_data_)) {
            return;
        }
        MarkDirty(component_, geometry_dirty_runtime_flag);
    }

    static void SetAppearanceHandle(GeometryType& component_,
                                    AppearanceHandle appearance_handle_) noexcept {
        if (!SetVisualRuntimeRouteAppearanceHandle(component_.runtime.route,
                                                   appearance_handle_)) {
            return;
        }
        BumpAppearanceHandleMutationSerial();
        MarkDirty(component_, geometry_dirty_runtime_flag);
    }

    static void ClearAppearanceHandle(GeometryType& component_) noexcept {
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
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    [[nodiscard]] static bool SetAppearanceRuntimeLink(GeometryType& component_,
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
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool SetAppearanceRuntimeLink(GeometryType& component_,
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
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool SetAppearanceRuntimeLink(GeometryType& component_,
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
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeState(GeometryType& component_,
                                                          const AppearanceStyle2D& appearance_style_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        return ApplyAppearanceRuntimeState(component_, &appearance_style_);
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeState(GeometryType& component_,
                                                          const AppearanceStyle2D* appearance_style_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const bool changed = WriteAppearanceRuntimeBridgeState(component_.runtime, appearance_style_);
        if (!changed) {
            return false;
        }
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeState(GeometryType& component_,
                                                          const AppearanceStyle3D& appearance_style_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        return ApplyAppearanceRuntimeState(component_, &appearance_style_);
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeState(GeometryType& component_,
                                                          const AppearanceStyle3D* appearance_style_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const bool changed = WriteAppearanceRuntimeBridgeState(component_.runtime, appearance_style_);
        if (!changed) {
            return false;
        }
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeBridgeState(
        GeometryType& component_,
        const AppearanceRuntimeBridge2D& appearance_bridge_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (!WriteAppearanceRuntimeBridgeState(component_.runtime, appearance_bridge_)) {
            return false;
        }
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    [[nodiscard]] static bool ApplyAppearanceRuntimeBridgeState(
        GeometryType& component_,
        const AppearanceRuntimeBridge3D& appearance_bridge_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (!WriteAppearanceRuntimeBridgeState(component_.runtime, appearance_bridge_)) {
            return false;
        }
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
        return true;
    }

    static void SetDepthBin(GeometryType& component_, std::uint16_t depth_bin_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (!SetVisualRuntimeRouteDepthBin(component_.runtime.route, depth_bin_)) {
            return;
        }
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetBounds(GeometryType& component_,
                          const Float2& bounds_min_,
                          const Float2& bounds_max_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        component_.runtime.bounds_min = bounds_min_;
        component_.runtime.bounds_max = bounds_max_;
        MarkDirty(component_, geometry_dirty_bounds_flag | geometry_dirty_runtime_flag);
    }

    static void SetBounds(GeometryType& component_,
                          const Float3& bounds_min_,
                          const Float3& bounds_max_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.runtime.bounds_min = bounds_min_;
        component_.runtime.bounds_max = bounds_max_;
        MarkDirty(component_, geometry_dirty_bounds_flag | geometry_dirty_runtime_flag);
    }

    [[nodiscard]] static std::uint64_t ComposeSortKey(const GeometryType& component_) noexcept {
        const bool has_linked_appearance = HasLinkedAppearanceHandle(component_.runtime.route);
        GeometryRenderPassHint effective_pass_hint = component_.runtime.route.pass_hint;
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
        const std::uint64_t geometry_bits =
            static_cast<std::uint64_t>(component_.runtime.route.geometry_id) & sort_key_geometry_mask;
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
        key |= (geometry_bits << sort_key_geometry_shift);
        key |= (minor_bits << sort_key_minor_shift);
        key |= (batch_bits << sort_key_batch_shift);
        return key;
    }

    static void RebuildSortKey(GeometryType& component_) noexcept {
        component_.runtime.route.sort_key = ComposeSortKey(component_);
    }

    [[nodiscard]] static std::uint64_t SortKey(const GeometryType& component_) noexcept {
        return component_.runtime.route.sort_key;
    }

    [[nodiscard]] static std::uint64_t BindingSortKey(const GeometryType& component_) noexcept {
        return BindingSortKey(component_.runtime.route.sort_key);
    }

    [[nodiscard]] static std::uint64_t BindingSortKey(std::uint64_t sort_key_) noexcept {
        return sort_key_ >> sort_key_binding_shift;
    }

    [[nodiscard]] static std::uint32_t ExtractPassBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>(PassHintFromSortBucket<GeometryRenderPassHint>(
            static_cast<std::uint32_t>((sort_key_ >> sort_key_pass_shift) & sort_key_pass_mask)));
    }

    [[nodiscard]] static std::uint32_t ExtractVisualResourceBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_visual_resource_shift) & sort_key_visual_resource_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractGeometryBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_geometry_shift) & sort_key_geometry_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractMinorBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_minor_shift) & sort_key_minor_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractBatchBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_batch_shift) & sort_key_batch_mask);
    }

    [[nodiscard]] static bool IsVisibleForBatch(const GeometryType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            return component_.runtime.route.visible != 0U && component_.path.size_bytes > 0U;
        } else {
            return component_.runtime.route.visible != 0U && component_.runtime.route.geometry_id != 0U;
        }
    }

private:
    static void BumpAppearanceHandleMutationSerial() noexcept {
        (void)appearance_handle_mutation_serial.fetch_add(1U, std::memory_order_relaxed);
    }

    inline static std::atomic<std::uint64_t> appearance_handle_mutation_serial{1U};
};

} // namespace vr::ecs
