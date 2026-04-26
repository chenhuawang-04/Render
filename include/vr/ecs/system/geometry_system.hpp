#pragma once

#include "vr/ecs/component/geometry_component.hpp"

#include <algorithm>
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

    // 64-bit sort key layout (MSB -> LSB):
    // [pass:2][material:16][geometry:16][minor:16][batch:14]
    static constexpr std::uint32_t sort_key_batch_bits = 14U;
    static constexpr std::uint32_t sort_key_minor_bits = 16U;
    static constexpr std::uint32_t sort_key_geometry_bits = 16U;
    static constexpr std::uint32_t sort_key_material_bits = 16U;
    static constexpr std::uint32_t sort_key_pass_bits = 2U;

    static constexpr std::uint32_t sort_key_batch_shift = 0U;
    static constexpr std::uint32_t sort_key_minor_shift = sort_key_batch_shift + sort_key_batch_bits;
    static constexpr std::uint32_t sort_key_geometry_shift = sort_key_minor_shift + sort_key_minor_bits;
    static constexpr std::uint32_t sort_key_material_shift = sort_key_geometry_shift + sort_key_geometry_bits;
    static constexpr std::uint32_t sort_key_pass_shift = sort_key_material_shift + sort_key_material_bits;

    static constexpr std::uint32_t sort_key_binding_shift = sort_key_minor_shift;

    static constexpr std::uint64_t sort_key_batch_mask = (std::uint64_t{1U} << sort_key_batch_bits) - 1U;
    static constexpr std::uint64_t sort_key_minor_mask = (std::uint64_t{1U} << sort_key_minor_bits) - 1U;
    static constexpr std::uint64_t sort_key_geometry_mask = (std::uint64_t{1U} << sort_key_geometry_bits) - 1U;
    static constexpr std::uint64_t sort_key_material_mask = (std::uint64_t{1U} << sort_key_material_bits) - 1U;
    static constexpr std::uint64_t sort_key_pass_mask = (std::uint64_t{1U} << sort_key_pass_bits) - 1U;

    static_assert(sort_key_pass_bits + sort_key_material_bits + sort_key_geometry_bits +
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
            component_.style.fill_color = Rgba8{255U, 255U, 255U, 255U};
            component_.style.stroke_color = Rgba8{0U, 0U, 0U, 255U};
            component_.style.layer = 0;
            component_.style.topology = Geometry2DTopology::fill;
            component_.style.fill_rule = Geometry2DFillRule::non_zero;
            component_.style.line_join = Geometry2DLineJoin::miter;
            component_.style.line_cap = Geometry2DLineCap::butt;
            component_.style.antialiasing = 1U;
            component_.style.reserved0 = 0U;
            component_.style.reserved1 = 0U;
        } else {
            component_.style.albedo_color = Rgba8{255U, 255U, 255U, 255U};
            component_.style.depth_test = 1U;
            component_.style.depth_write = 1U;
            component_.style.double_sided = 0U;
            component_.style.topology = Geometry3DTopology::triangles;
            component_.style.shading_model = Geometry3DShadingModel::lit;
            component_.style.cast_shadow = 1U;
            component_.style.receive_shadow = 1U;
            component_.style.reserved0 = 0U;
            component_.style.metallic = 0.0F;
            component_.style.roughness = 1.0F;
            component_.style.normal_scale = 1.0F;
            component_.style.line_width = 1.0F;
        }

        MarkDirty(component_, geometry_dirty_style_flag | geometry_dirty_runtime_flag);
    }

    static void SetDefaultRuntime(GeometryType& component_) noexcept {
        component_.runtime.route.sort_key = 0U;
        component_.runtime.route.geometry_id = 0U;
        component_.runtime.route.material_id = 0U;
        component_.runtime.route.batch_tag = 0U;
        component_.runtime.route.user_data = 0U;
        component_.runtime.route.depth_bin = 0U;
        component_.runtime.route.visible = 1U;
        component_.runtime.route.pass_hint = std::same_as<DimensionT, Dim2>
            ? GeometryRenderPassHint::overlay
            : GeometryRenderPassHint::opaque;
        component_.runtime.route.dirty_flags = geometry_dirty_data_flag |
                                               geometry_dirty_style_flag |
                                               geometry_dirty_runtime_flag |
                                               geometry_dirty_bounds_flag;

        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.runtime.path_command_count = 0U;
            component_.runtime.tessellation_revision = 0U;
            component_.runtime.path_data_hash = 0U;
            component_.runtime.reserved0 = 0U;
            component_.runtime.bounds_min = Float2{.x = 0.0F, .y = 0.0F};
            component_.runtime.bounds_max = Float2{.x = 0.0F, .y = 0.0F};
        } else {
            component_.runtime.mesh_revision = 0U;
            component_.runtime.meshlet_count_hint = 0U;
            component_.runtime.reserved0 = 0U;
            component_.runtime.reserved1 = 0U;
            component_.runtime.bounds_min = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
            component_.runtime.bounds_max = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        }
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const GeometryType& component_) noexcept {
        return component_.runtime.route.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const GeometryType& component_,
                                            std::uint32_t dirty_mask_) noexcept {
        return (component_.runtime.route.dirty_flags & dirty_mask_) != 0U;
    }

    static void MarkDirty(GeometryType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.runtime.route.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(GeometryType& component_, std::uint32_t clear_mask_) noexcept {
        component_.runtime.route.dirty_flags &= ~clear_mask_;
    }

    static void SetVisible(GeometryType& component_, bool visible_) noexcept {
        component_.runtime.route.visible = visible_ ? 1U : 0U;
        MarkDirty(component_, geometry_dirty_runtime_flag);
    }

    static void SetRenderPassHint(GeometryType& component_,
                                  GeometryRenderPassHint pass_hint_) noexcept {
        component_.runtime.route.pass_hint = pass_hint_;
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetRuntimeRoute(GeometryType& component_,
                                std::uint32_t geometry_id_,
                                std::uint32_t material_id_,
                                std::uint32_t batch_tag_) noexcept {
        component_.runtime.route.geometry_id = geometry_id_;
        component_.runtime.route.material_id = material_id_;
        component_.runtime.route.batch_tag = batch_tag_;
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetGeometryId(GeometryType& component_, std::uint32_t geometry_id_) noexcept {
        component_.runtime.route.geometry_id = geometry_id_;
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetMaterialId(GeometryType& component_, std::uint32_t material_id_) noexcept {
        component_.runtime.route.material_id = material_id_;
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetBatchTag(GeometryType& component_, std::uint32_t batch_tag_) noexcept {
        component_.runtime.route.batch_tag = batch_tag_;
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetUserData(GeometryType& component_, std::uint32_t user_data_) noexcept {
        component_.runtime.route.user_data = user_data_;
        MarkDirty(component_, geometry_dirty_runtime_flag);
    }

    static void SetDepthBin(GeometryType& component_, std::uint16_t depth_bin_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.runtime.route.depth_bin = depth_bin_;
        MarkDirty(component_, geometry_dirty_runtime_flag);
        RebuildSortKey(component_);
    }

    static void SetLayer(GeometryType& component_, std::int16_t layer_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        component_.style.layer = layer_;
        MarkDirty(component_, geometry_dirty_style_flag | geometry_dirty_runtime_flag);
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
        const std::uint64_t pass_bits = static_cast<std::uint64_t>(component_.runtime.route.pass_hint) &
                                        sort_key_pass_mask;
        const std::uint64_t material_bits =
            static_cast<std::uint64_t>(component_.runtime.route.material_id) & sort_key_material_mask;
        const std::uint64_t geometry_bits =
            static_cast<std::uint64_t>(component_.runtime.route.geometry_id) & sort_key_geometry_mask;
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
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_pass_shift) & sort_key_pass_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractMaterialBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_material_shift) & sort_key_material_mask);
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
};

} // namespace vr::ecs
