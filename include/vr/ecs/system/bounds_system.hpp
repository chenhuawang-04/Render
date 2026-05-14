#pragma once

#include "vr/ecs/component/bounds_component.hpp"
#include "vr/ecs/component/transform_component.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstdint>

namespace vr::ecs {

template<DimensionTag DimensionT>
class BoundsSystem final {
public:
    using BoundsType = Bounds<DimensionT>;
    using TransformType = Transform<DimensionT>;

    static void Initialize(BoundsType& component_) noexcept {
        SetDefaultStyle(component_);
        SetDefaultRuntime(component_);
        (void)Update(component_, static_cast<const TransformType*>(nullptr));
        component_.runtime.local_revision = 1U;
    }

    static void SetDefaultStyle(BoundsType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.local_min = Float2{.x = -0.5F, .y = -0.5F};
            component_.style.local_max = Float2{.x = 0.5F, .y = 0.5F};
        } else {
            component_.style.local_min = Float3{.x = -0.5F, .y = -0.5F, .z = -0.5F};
            component_.style.local_max = Float3{.x = 0.5F, .y = 0.5F, .z = 0.5F};
            component_.style.reserved0 = 0.0F;
            component_.style.reserved1 = 0.0F;
        }

        MarkDirty(component_, bounds_dirty_local_flag | bounds_dirty_runtime_flag);
    }

    static void SetDefaultRuntime(BoundsType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.runtime.world_min = Float2{.x = 0.0F, .y = 0.0F};
            component_.runtime.world_max = Float2{.x = 0.0F, .y = 0.0F};
            component_.runtime.world_center = Float2{.x = 0.0F, .y = 0.0F};
            component_.runtime.world_extents = Float2{.x = 0.0F, .y = 0.0F};
            component_.runtime.world_radius = 0.0F;
            component_.runtime.reserved0 = 0U;
        } else {
            component_.runtime.world_min = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
            component_.runtime.world_max = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
            component_.runtime.world_center = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
            component_.runtime.world_extents = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
            component_.runtime.world_radius = 0.0F;
            component_.runtime.reserved0 = 0.0F;
            component_.runtime.reserved1 = 0U;
        }

        component_.runtime.local_revision = 0U;
        component_.runtime.world_revision = 0U;
        component_.runtime.transform_world_revision = 0U;
        component_.runtime.visibility_mask = 0xFFFFFFFFU;
        component_.runtime.dirty_flags = bounds_dirty_local_flag |
                                         bounds_dirty_runtime_flag;
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const BoundsType& component_) noexcept {
        return component_.runtime.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const BoundsType& component_,
                                            std::uint32_t dirty_mask_) noexcept {
        return (component_.runtime.dirty_flags & dirty_mask_) != 0U;
    }

    static void MarkDirty(BoundsType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.runtime.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(BoundsType& component_, std::uint32_t clear_mask_) noexcept {
        component_.runtime.dirty_flags &= ~clear_mask_;
    }

    [[nodiscard]] static std::uint32_t LocalRevision(const BoundsType& component_) noexcept {
        return component_.runtime.local_revision;
    }

    [[nodiscard]] static std::uint32_t WorldRevision(const BoundsType& component_) noexcept {
        return component_.runtime.world_revision;
    }

    [[nodiscard]] static std::uint32_t VisibilityMask(const BoundsType& component_) noexcept {
        return component_.runtime.visibility_mask;
    }

    static void SetVisibilityMask(BoundsType& component_, std::uint32_t visibility_mask_) noexcept {
        if (component_.runtime.visibility_mask == visibility_mask_) {
            return;
        }
        component_.runtime.visibility_mask = visibility_mask_;
        MarkDirty(component_, bounds_dirty_visibility_flag);
    }

    static void SetLocalAabb(BoundsType& component_,
                             const Float2& local_min_,
                             const Float2& local_max_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        Float2 canonical_min{};
        Float2 canonical_max{};
        CanonicalizeAabb(local_min_, local_max_, canonical_min, canonical_max);
        if (component_.style.local_min.x == canonical_min.x &&
            component_.style.local_min.y == canonical_min.y &&
            component_.style.local_max.x == canonical_max.x &&
            component_.style.local_max.y == canonical_max.y) {
            return;
        }

        component_.style.local_min = canonical_min;
        component_.style.local_max = canonical_max;
        component_.runtime.local_revision = NextBoundsRevision(component_.runtime.local_revision);
        MarkDirty(component_, bounds_dirty_local_flag | bounds_dirty_runtime_flag);
    }

    static void SetLocalAabb(BoundsType& component_,
                             const Float3& local_min_,
                             const Float3& local_max_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        Float3 canonical_min{};
        Float3 canonical_max{};
        CanonicalizeAabb(local_min_, local_max_, canonical_min, canonical_max);
        if (component_.style.local_min.x == canonical_min.x &&
            component_.style.local_min.y == canonical_min.y &&
            component_.style.local_min.z == canonical_min.z &&
            component_.style.local_max.x == canonical_max.x &&
            component_.style.local_max.y == canonical_max.y &&
            component_.style.local_max.z == canonical_max.z) {
            return;
        }

        component_.style.local_min = canonical_min;
        component_.style.local_max = canonical_max;
        component_.runtime.local_revision = NextBoundsRevision(component_.runtime.local_revision);
        MarkDirty(component_, bounds_dirty_local_flag | bounds_dirty_runtime_flag);
    }

    static void SetLocalCenterExtents(BoundsType& component_,
                                      const Float2& center_,
                                      const Float2& extents_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const float extent_x = std::max(0.0F, extents_.x);
        const float extent_y = std::max(0.0F, extents_.y);
        const Float2 local_min{
            .x = center_.x - extent_x,
            .y = center_.y - extent_y
        };
        const Float2 local_max{
            .x = center_.x + extent_x,
            .y = center_.y + extent_y
        };
        SetLocalAabb(component_, local_min, local_max);
    }

    static void SetLocalCenterExtents(BoundsType& component_,
                                      const Float3& center_,
                                      const Float3& extents_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float extent_x = std::max(0.0F, extents_.x);
        const float extent_y = std::max(0.0F, extents_.y);
        const float extent_z = std::max(0.0F, extents_.z);
        const Float3 local_min{
            .x = center_.x - extent_x,
            .y = center_.y - extent_y,
            .z = center_.z - extent_z
        };
        const Float3 local_max{
            .x = center_.x + extent_x,
            .y = center_.y + extent_y,
            .z = center_.z + extent_z
        };
        SetLocalAabb(component_, local_min, local_max);
    }

    [[nodiscard]] static bool Update(BoundsType& component_,
                                     const TransformType& transform_) noexcept {
        return UpdateWithTransform(component_, transform_);
    }

    [[nodiscard]] static bool Update(BoundsType& component_,
                                     const TransformType* transform_) noexcept {
        if (transform_ == nullptr) {
            return UpdateWithoutTransform(component_);
        }
        return UpdateWithTransform(component_, *transform_);
    }

    [[nodiscard]] static std::uint32_t UpdateAligned(BoundsType* components_,
                                                     const TransformType* transforms_,
                                                     std::uint32_t component_count_) noexcept {
        if (components_ == nullptr || component_count_ == 0U) {
            return 0U;
        }

        std::uint32_t updated_count = 0U;
        if (transforms_ == nullptr) {
            for (std::uint32_t i = 0U; i < component_count_; ++i) {
                if (UpdateWithoutTransform(components_[i])) {
                    ++updated_count;
                }
            }
            return updated_count;
        }

        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            if (UpdateWithTransform(components_[i], transforms_[i])) {
                ++updated_count;
            }
        }
        return updated_count;
    }

    [[nodiscard]] static std::uint32_t UpdateAligned(BoundsType* components_,
                                                     const TransformType* transforms_,
                                                     std::uint32_t component_count_,
                                                     const std::uint32_t* dirty_component_indices_,
                                                     std::uint32_t dirty_component_count_) noexcept {
        if (components_ == nullptr || component_count_ == 0U) {
            return 0U;
        }
        if (dirty_component_indices_ == nullptr || dirty_component_count_ == 0U) {
            return UpdateAligned(components_, transforms_, component_count_);
        }

        std::uint32_t updated_count = 0U;
        if (transforms_ != nullptr) {
            if (dirty_component_count_ == 1U) {
                const std::uint32_t component_index = dirty_component_indices_[0U];
                if (component_index < component_count_ &&
                    UpdateWithTransform(components_[component_index], transforms_[component_index])) {
                    return 1U;
                }
                return 0U;
            }

            for (std::uint32_t i = 0U; i < dirty_component_count_; ++i) {
                const std::uint32_t component_index = dirty_component_indices_[i];
                if (component_index >= component_count_) {
                    continue;
                }
                if (UpdateWithTransform(components_[component_index], transforms_[component_index])) {
                    ++updated_count;
                }
            }
            return updated_count;
        }

        for (std::uint32_t i = 0U; i < dirty_component_count_; ++i) {
            const std::uint32_t component_index = dirty_component_indices_[i];
            if (component_index >= component_count_) {
                continue;
            }
            if (UpdateWithoutTransform(components_[component_index])) {
                ++updated_count;
            }
        }
        return updated_count;
    }

    [[nodiscard]] static bool ContainsPointWorld(const BoundsType& component_,
                                                 const Float2& point_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        return point_.x >= component_.runtime.world_min.x &&
               point_.x <= component_.runtime.world_max.x &&
               point_.y >= component_.runtime.world_min.y &&
               point_.y <= component_.runtime.world_max.y;
    }

    [[nodiscard]] static bool ContainsPointWorld(const BoundsType& component_,
                                                 const Float3& point_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        return point_.x >= component_.runtime.world_min.x &&
               point_.x <= component_.runtime.world_max.x &&
               point_.y >= component_.runtime.world_min.y &&
               point_.y <= component_.runtime.world_max.y &&
               point_.z >= component_.runtime.world_min.z &&
               point_.z <= component_.runtime.world_max.z;
    }

    [[nodiscard]] static bool IntersectsWorldAabb(const BoundsType& component_,
                                                  const Float2& world_min_,
                                                  const Float2& world_max_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        Float2 canonical_min{};
        Float2 canonical_max{};
        CanonicalizeAabb(world_min_, world_max_, canonical_min, canonical_max);
        return component_.runtime.world_max.x >= canonical_min.x &&
               component_.runtime.world_min.x <= canonical_max.x &&
               component_.runtime.world_max.y >= canonical_min.y &&
               component_.runtime.world_min.y <= canonical_max.y;
    }

    [[nodiscard]] static bool IntersectsWorldAabb(const BoundsType& component_,
                                                  const Float3& world_min_,
                                                  const Float3& world_max_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        Float3 canonical_min{};
        Float3 canonical_max{};
        CanonicalizeAabb(world_min_, world_max_, canonical_min, canonical_max);
        return component_.runtime.world_max.x >= canonical_min.x &&
               component_.runtime.world_min.x <= canonical_max.x &&
               component_.runtime.world_max.y >= canonical_min.y &&
               component_.runtime.world_min.y <= canonical_max.y &&
               component_.runtime.world_max.z >= canonical_min.z &&
               component_.runtime.world_min.z <= canonical_max.z;
    }

private:
    static void CanonicalizeAabb(const Float2& in_min_,
                                 const Float2& in_max_,
                                 Float2& out_min_,
                                 Float2& out_max_) noexcept {
        out_min_.x = std::min(in_min_.x, in_max_.x);
        out_min_.y = std::min(in_min_.y, in_max_.y);
        out_max_.x = std::max(in_min_.x, in_max_.x);
        out_max_.y = std::max(in_min_.y, in_max_.y);
    }

    static void CanonicalizeAabb(const Float3& in_min_,
                                 const Float3& in_max_,
                                 Float3& out_min_,
                                 Float3& out_max_) noexcept {
        out_min_.x = std::min(in_min_.x, in_max_.x);
        out_min_.y = std::min(in_min_.y, in_max_.y);
        out_min_.z = std::min(in_min_.z, in_max_.z);
        out_max_.x = std::max(in_min_.x, in_max_.x);
        out_max_.y = std::max(in_min_.y, in_max_.y);
        out_max_.z = std::max(in_min_.z, in_max_.z);
    }

    [[nodiscard]] static bool UpdateWithTransform(BoundsType& component_,
                                                  const TransformType& transform_) noexcept {
        return UpdateForTransform(component_,
                                  &transform_,
                                  transform_.runtime.world_revision);
    }

    [[nodiscard]] static bool UpdateWithoutTransform(BoundsType& component_) noexcept {
        return UpdateForTransform(component_, nullptr, 0U);
    }

    [[nodiscard]] static bool UpdateForTransform(BoundsType& component_,
                                                 const TransformType* transform_,
                                                 std::uint32_t transform_world_revision_) noexcept {
        const std::uint32_t dirty_flags = component_.runtime.dirty_flags;
        const bool has_bounds_dirty =
            (dirty_flags & (bounds_dirty_local_flag | bounds_dirty_runtime_flag)) != 0U;
        if (!has_bounds_dirty &&
            component_.runtime.transform_world_revision == transform_world_revision_) {
            return false;
        }

        RebuildWorldBounds(component_, transform_);
        component_.runtime.transform_world_revision = transform_world_revision_;
        component_.runtime.world_revision = NextBoundsRevision(component_.runtime.world_revision);
        component_.runtime.dirty_flags = dirty_flags &
            ~(bounds_dirty_local_flag | bounds_dirty_runtime_flag);
        return true;
    }

    static void RebuildWorldBounds(BoundsType& component_,
                                   const TransformType* transform_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            Float2 local_min{};
            Float2 local_max{};
            CanonicalizeAabb(component_.style.local_min,
                             component_.style.local_max,
                             local_min,
                             local_max);
            component_.style.local_min = local_min;
            component_.style.local_max = local_max;

            const float local_center_x = (local_min.x + local_max.x) * 0.5F;
            const float local_center_y = (local_min.y + local_max.y) * 0.5F;
            const float local_extent_x = (local_max.x - local_min.x) * 0.5F;
            const float local_extent_y = (local_max.y - local_min.y) * 0.5F;

            float m00 = 1.0F;
            float m01 = 0.0F;
            float m02 = 0.0F;
            float m10 = 0.0F;
            float m11 = 1.0F;
            float m12 = 0.0F;
            if (transform_ != nullptr) {
                m00 = transform_->runtime.world_matrix.m00;
                m01 = transform_->runtime.world_matrix.m01;
                m02 = transform_->runtime.world_matrix.m02;
                m10 = transform_->runtime.world_matrix.m10;
                m11 = transform_->runtime.world_matrix.m11;
                m12 = transform_->runtime.world_matrix.m12;
            }

            const float world_center_x = m00 * local_center_x + m01 * local_center_y + m02;
            const float world_center_y = m10 * local_center_x + m11 * local_center_y + m12;

            const float world_extent_x = std::abs(m00) * local_extent_x +
                                         std::abs(m01) * local_extent_y;
            const float world_extent_y = std::abs(m10) * local_extent_x +
                                         std::abs(m11) * local_extent_y;

            component_.runtime.world_center = Float2{.x = world_center_x, .y = world_center_y};
            component_.runtime.world_extents = Float2{.x = world_extent_x, .y = world_extent_y};
            component_.runtime.world_min = Float2{
                .x = world_center_x - world_extent_x,
                .y = world_center_y - world_extent_y
            };
            component_.runtime.world_max = Float2{
                .x = world_center_x + world_extent_x,
                .y = world_center_y + world_extent_y
            };
            component_.runtime.world_radius = std::sqrt(world_extent_x * world_extent_x +
                                                        world_extent_y * world_extent_y);
        } else {
            Float3 local_min{};
            Float3 local_max{};
            CanonicalizeAabb(component_.style.local_min,
                             component_.style.local_max,
                             local_min,
                             local_max);
            component_.style.local_min = local_min;
            component_.style.local_max = local_max;

            const float local_center_x = (local_min.x + local_max.x) * 0.5F;
            const float local_center_y = (local_min.y + local_max.y) * 0.5F;
            const float local_center_z = (local_min.z + local_max.z) * 0.5F;
            const float local_extent_x = (local_max.x - local_min.x) * 0.5F;
            const float local_extent_y = (local_max.y - local_min.y) * 0.5F;
            const float local_extent_z = (local_max.z - local_min.z) * 0.5F;

            float m0 = 1.0F;
            float m1 = 0.0F;
            float m2 = 0.0F;
            float m4 = 0.0F;
            float m5 = 1.0F;
            float m6 = 0.0F;
            float m8 = 0.0F;
            float m9 = 0.0F;
            float m10 = 1.0F;
            float m12 = 0.0F;
            float m13 = 0.0F;
            float m14 = 0.0F;
            if (transform_ != nullptr) {
                m0 = transform_->runtime.world_matrix.m[0];
                m1 = transform_->runtime.world_matrix.m[1];
                m2 = transform_->runtime.world_matrix.m[2];
                m4 = transform_->runtime.world_matrix.m[4];
                m5 = transform_->runtime.world_matrix.m[5];
                m6 = transform_->runtime.world_matrix.m[6];
                m8 = transform_->runtime.world_matrix.m[8];
                m9 = transform_->runtime.world_matrix.m[9];
                m10 = transform_->runtime.world_matrix.m[10];
                m12 = transform_->runtime.world_matrix.m[12];
                m13 = transform_->runtime.world_matrix.m[13];
                m14 = transform_->runtime.world_matrix.m[14];
            }

            const float world_center_x = m0 * local_center_x +
                                         m4 * local_center_y +
                                         m8 * local_center_z +
                                         m12;
            const float world_center_y = m1 * local_center_x +
                                         m5 * local_center_y +
                                         m9 * local_center_z +
                                         m13;
            const float world_center_z = m2 * local_center_x +
                                         m6 * local_center_y +
                                         m10 * local_center_z +
                                         m14;

            const float world_extent_x = std::abs(m0) * local_extent_x +
                                         std::abs(m4) * local_extent_y +
                                         std::abs(m8) * local_extent_z;
            const float world_extent_y = std::abs(m1) * local_extent_x +
                                         std::abs(m5) * local_extent_y +
                                         std::abs(m9) * local_extent_z;
            const float world_extent_z = std::abs(m2) * local_extent_x +
                                         std::abs(m6) * local_extent_y +
                                         std::abs(m10) * local_extent_z;

            component_.runtime.world_center = Float3{
                .x = world_center_x,
                .y = world_center_y,
                .z = world_center_z
            };
            component_.runtime.world_extents = Float3{
                .x = world_extent_x,
                .y = world_extent_y,
                .z = world_extent_z
            };
            component_.runtime.world_min = Float3{
                .x = world_center_x - world_extent_x,
                .y = world_center_y - world_extent_y,
                .z = world_center_z - world_extent_z
            };
            component_.runtime.world_max = Float3{
                .x = world_center_x + world_extent_x,
                .y = world_center_y + world_extent_y,
                .z = world_center_z + world_extent_z
            };
            component_.runtime.world_radius = std::sqrt(world_extent_x * world_extent_x +
                                                        world_extent_y * world_extent_y +
                                                        world_extent_z * world_extent_z);
        }
    }
};

} // namespace vr::ecs

