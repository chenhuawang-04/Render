#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/spatial_math.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace vr::ecs {

template<typename T>
using TransformSystemMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

template<DimensionTag DimensionT>
struct TransformHierarchyScratch final {
    TransformSystemMcVector<std::uint32_t> stack{};
    TransformSystemMcVector<std::uint8_t> visit_state{};

    void Reserve(std::uint32_t component_count_) {
        const auto target = static_cast<std::size_t>(component_count_);
        if (stack.capacity() < target) {
            stack.reserve(target);
        }
        if (visit_state.capacity() < target) {
            visit_state.reserve(target);
        }
    }
};

template<DimensionTag DimensionT>
class TransformSystem final {
public:
    using TransformType = Transform<DimensionT>;
    using RuntimeType = typename TransformType::RuntimeType;
    using ScratchType = TransformHierarchyScratch<DimensionT>;

    static constexpr std::int32_t invalid_index = -1;

    static void Initialize(TransformType& component_) noexcept {
        SetDefaultStyle(component_);
        SetDefaultRuntime(component_);

        RebuildLocalMatrix(component_);
        component_.runtime.world_matrix = component_.runtime.local_matrix;
        component_.runtime.world_revision = 1U;
        component_.runtime.cached_parent_world_revision = 0U;
        component_.runtime.dirty_flags = 0U;
    }

    static void SetDefaultStyle(TransformType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.position = Float2{.x = 0.0F, .y = 0.0F};
            component_.style.rotation_radians = 0.0F;
            component_.style.scale = Float2{.x = 1.0F, .y = 1.0F};
        } else {
            component_.style.position = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
            component_.style.rotation = Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
            component_.style.scale = Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F};
            component_.style.reserved0 = 0U;
        }

        MarkDirty(component_, transform_dirty_local_flag | transform_dirty_world_flag);
    }

    static void SetDefaultRuntime(TransformType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.runtime.local_matrix = spatial_math::IdentityAffine2x3();
            component_.runtime.world_matrix = spatial_math::IdentityAffine2x3();
        } else {
            component_.runtime.local_matrix = spatial_math::IdentityMatrix4x4();
            component_.runtime.world_matrix = spatial_math::IdentityMatrix4x4();
        }

        component_.runtime.hierarchy.parent_index = invalid_index;
        component_.runtime.hierarchy.first_child_index = invalid_index;
        component_.runtime.hierarchy.next_sibling_index = invalid_index;
        component_.runtime.hierarchy.reserved0 = 0;

        component_.runtime.local_revision = 0U;
        component_.runtime.world_revision = 0U;
        component_.runtime.cached_parent_world_revision = 0U;
        component_.runtime.dirty_flags = transform_dirty_local_flag |
                                         transform_dirty_world_flag |
                                         transform_dirty_hierarchy_flag;
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const TransformType& component_) noexcept {
        return component_.runtime.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const TransformType& component_,
                                            std::uint32_t dirty_mask_) noexcept {
        return (component_.runtime.dirty_flags & dirty_mask_) != 0U;
    }

    static void MarkDirty(TransformType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.runtime.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(TransformType& component_, std::uint32_t clear_mask_) noexcept {
        component_.runtime.dirty_flags &= ~clear_mask_;
    }

    [[nodiscard]] static std::uint32_t LocalRevision(const TransformType& component_) noexcept {
        return component_.runtime.local_revision;
    }

    [[nodiscard]] static std::uint32_t WorldRevision(const TransformType& component_) noexcept {
        return component_.runtime.world_revision;
    }

    [[nodiscard]] static std::int32_t ParentIndex(const TransformType& component_) noexcept {
        return component_.runtime.hierarchy.parent_index;
    }

    [[nodiscard]] static std::int32_t FirstChildIndex(const TransformType& component_) noexcept {
        return component_.runtime.hierarchy.first_child_index;
    }

    [[nodiscard]] static std::int32_t NextSiblingIndex(const TransformType& component_) noexcept {
        return component_.runtime.hierarchy.next_sibling_index;
    }

    static void SetParentIndex(TransformType& component_, std::int32_t parent_index_) noexcept {
        component_.runtime.hierarchy.parent_index = parent_index_;
        MarkDirty(component_, transform_dirty_hierarchy_flag | transform_dirty_world_flag);
    }

    static void SetFirstChildIndex(TransformType& component_, std::int32_t first_child_index_) noexcept {
        component_.runtime.hierarchy.first_child_index = first_child_index_;
        MarkDirty(component_, transform_dirty_hierarchy_flag);
    }

    static void SetNextSiblingIndex(TransformType& component_, std::int32_t next_sibling_index_) noexcept {
        component_.runtime.hierarchy.next_sibling_index = next_sibling_index_;
        MarkDirty(component_, transform_dirty_hierarchy_flag);
    }

    static void SetLocalPosition(TransformType& component_,
                                 float x_,
                                 float y_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        component_.style.position.x = x_;
        component_.style.position.y = y_;
        MarkDirty(component_, transform_dirty_local_flag | transform_dirty_world_flag);
    }

    static void SetLocalRotationRadians(TransformType& component_,
                                        float rotation_radians_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        component_.style.rotation_radians = rotation_radians_;
        MarkDirty(component_, transform_dirty_local_flag | transform_dirty_world_flag);
    }

    static void SetLocalScale(TransformType& component_,
                              float scale_x_,
                              float scale_y_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        component_.style.scale.x = scale_x_;
        component_.style.scale.y = scale_y_;
        MarkDirty(component_, transform_dirty_local_flag | transform_dirty_world_flag);
    }

    static void SetLocalPosition(TransformType& component_,
                                 const Float3& position_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.position = position_;
        MarkDirty(component_, transform_dirty_local_flag | transform_dirty_world_flag);
    }

    static void SetLocalScale(TransformType& component_,
                              const Float3& scale_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.scale = scale_;
        MarkDirty(component_, transform_dirty_local_flag | transform_dirty_world_flag);
    }

    static void SetLocalRotationQuaternion(TransformType& component_,
                                           const Quaternion& rotation_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.rotation = spatial_math::NormalizeQuaternion(rotation_);
        MarkDirty(component_, transform_dirty_local_flag | transform_dirty_world_flag);
    }

    static void SetLocalRotationEulerXyz(TransformType& component_,
                                         float pitch_x_radians_,
                                         float yaw_y_radians_,
                                         float roll_z_radians_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.rotation = spatial_math::QuaternionFromEulerXyz(pitch_x_radians_,
                                                                          yaw_y_radians_,
                                                                          roll_z_radians_);
        MarkDirty(component_, transform_dirty_local_flag | transform_dirty_world_flag);
    }

    [[nodiscard]] static const auto& LocalMatrix(const TransformType& component_) noexcept {
        return component_.runtime.local_matrix;
    }

    [[nodiscard]] static const auto& WorldMatrix(const TransformType& component_) noexcept {
        return component_.runtime.world_matrix;
    }

    static void RebuildLocalMatrix(TransformType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.runtime.local_matrix = spatial_math::ComposeAffine2x3Trs(component_.style.position.x,
                                                                                 component_.style.position.y,
                                                                                 component_.style.rotation_radians,
                                                                                 component_.style.scale.x,
                                                                                 component_.style.scale.y);
        } else {
            component_.style.rotation = spatial_math::NormalizeQuaternion(component_.style.rotation);
            component_.runtime.local_matrix = spatial_math::ComposeMatrix4x4Trs(component_.style.position,
                                                                                component_.style.rotation,
                                                                                component_.style.scale);
        }

        ++component_.runtime.local_revision;
        component_.runtime.dirty_flags &= ~transform_dirty_local_flag;
        component_.runtime.dirty_flags |= transform_dirty_world_flag;
    }

    static void RebuildWorldFromParent(TransformType& component_,
                                       const TransformType* parent_) noexcept {
        if (parent_ == nullptr) {
            component_.runtime.world_matrix = component_.runtime.local_matrix;
            component_.runtime.cached_parent_world_revision = 0U;
        } else {
            if constexpr (std::same_as<DimensionT, Dim2>) {
                component_.runtime.world_matrix = spatial_math::MultiplyAffine2x3(parent_->runtime.world_matrix,
                                                                                   component_.runtime.local_matrix);
            } else {
                component_.runtime.world_matrix = spatial_math::MultiplyMatrix4x4(parent_->runtime.world_matrix,
                                                                                   component_.runtime.local_matrix);
            }
            component_.runtime.cached_parent_world_revision = parent_->runtime.world_revision;
        }

        ++component_.runtime.world_revision;
        component_.runtime.dirty_flags &= ~(transform_dirty_world_flag | transform_dirty_hierarchy_flag);
    }

    static bool DetachFromParent(TransformType* transforms_,
                                 std::uint32_t component_count_,
                                 std::uint32_t child_index_) noexcept {
        if (transforms_ == nullptr || child_index_ >= component_count_) {
            return false;
        }

        TransformType& child = transforms_[child_index_];
        const std::int32_t parent_index = child.runtime.hierarchy.parent_index;
        if (parent_index < 0 || static_cast<std::uint32_t>(parent_index) >= component_count_) {
            child.runtime.hierarchy.parent_index = invalid_index;
            child.runtime.hierarchy.next_sibling_index = invalid_index;
            MarkDirty(child, transform_dirty_hierarchy_flag | transform_dirty_world_flag);
            return false;
        }

        TransformType& parent = transforms_[static_cast<std::uint32_t>(parent_index)];

        std::int32_t current_index = parent.runtime.hierarchy.first_child_index;
        std::int32_t previous_index = invalid_index;
        std::uint32_t guard = 0U;

        while (current_index >= 0 && static_cast<std::uint32_t>(current_index) < component_count_ && guard < component_count_) {
            if (current_index == static_cast<std::int32_t>(child_index_)) {
                break;
            }

            previous_index = current_index;
            current_index = transforms_[static_cast<std::uint32_t>(current_index)].runtime.hierarchy.next_sibling_index;
            ++guard;
        }

        if (current_index != static_cast<std::int32_t>(child_index_)) {
            child.runtime.hierarchy.parent_index = invalid_index;
            child.runtime.hierarchy.next_sibling_index = invalid_index;
            MarkDirty(child, transform_dirty_hierarchy_flag | transform_dirty_world_flag);
            return false;
        }

        if (previous_index == invalid_index) {
            parent.runtime.hierarchy.first_child_index = child.runtime.hierarchy.next_sibling_index;
        } else {
            transforms_[static_cast<std::uint32_t>(previous_index)].runtime.hierarchy.next_sibling_index =
                child.runtime.hierarchy.next_sibling_index;
        }

        child.runtime.hierarchy.parent_index = invalid_index;
        child.runtime.hierarchy.next_sibling_index = invalid_index;

        MarkDirty(child, transform_dirty_hierarchy_flag | transform_dirty_world_flag);
        MarkDirty(parent, transform_dirty_hierarchy_flag);
        return true;
    }

    [[nodiscard]] static bool AttachChild(TransformType* transforms_,
                                          std::uint32_t component_count_,
                                          std::uint32_t child_index_,
                                          std::uint32_t parent_index_) noexcept {
        if (transforms_ == nullptr ||
            child_index_ >= component_count_ ||
            parent_index_ >= component_count_ ||
            child_index_ == parent_index_) {
            return false;
        }

        if (WouldCreateCycle(transforms_, component_count_, child_index_, parent_index_)) {
            return false;
        }

        (void)DetachFromParent(transforms_, component_count_, child_index_);

        TransformType& parent = transforms_[parent_index_];
        TransformType& child = transforms_[child_index_];

        child.runtime.hierarchy.parent_index = static_cast<std::int32_t>(parent_index_);
        child.runtime.hierarchy.next_sibling_index = parent.runtime.hierarchy.first_child_index;
        parent.runtime.hierarchy.first_child_index = static_cast<std::int32_t>(child_index_);

        MarkDirty(child, transform_dirty_hierarchy_flag | transform_dirty_world_flag);
        MarkDirty(parent, transform_dirty_hierarchy_flag);
        return true;
    }

    static void UpdateHierarchy(TransformType* transforms_,
                                std::uint32_t component_count_) {
        ScratchType scratch{};
        UpdateHierarchy(transforms_, component_count_, scratch);
    }

    static void UpdateHierarchy(TransformType* transforms_,
                                std::uint32_t component_count_,
                                ScratchType& scratch_) {
        if (transforms_ == nullptr || component_count_ == 0U) {
            return;
        }
        if (CanUseFlatHierarchyFastPath(transforms_, component_count_)) {
            UpdateFlatHierarchy(transforms_, component_count_);
            return;
        }

        scratch_.Reserve(component_count_);
        scratch_.stack.clear();
        scratch_.visit_state.resize(component_count_);
        std::fill(scratch_.visit_state.begin(), scratch_.visit_state.end(), std::uint8_t{0U});

        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            const std::int32_t parent_index = transforms_[i].runtime.hierarchy.parent_index;
            if (parent_index < 0 || static_cast<std::uint32_t>(parent_index) >= component_count_) {
                scratch_.stack.push_back(i);
            }
        }

        while (!scratch_.stack.empty()) {
            const std::uint32_t node_index = scratch_.stack.back();
            scratch_.stack.pop_back();
            ProcessNode(transforms_, component_count_, node_index, scratch_);
        }

        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            if (scratch_.visit_state[i] != 2U) {
                transforms_[i].runtime.hierarchy.parent_index = invalid_index;
                MarkDirty(transforms_[i], transform_dirty_hierarchy_flag | transform_dirty_world_flag);
                scratch_.stack.push_back(i);
                while (!scratch_.stack.empty()) {
                    const std::uint32_t node_index = scratch_.stack.back();
                    scratch_.stack.pop_back();
                    ProcessNode(transforms_, component_count_, node_index, scratch_);
                }
            }
        }
    }

private:
    [[nodiscard]] static bool CanUseFlatHierarchyFastPath(const TransformType* transforms_,
                                                          std::uint32_t component_count_) noexcept {
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            const auto& hierarchy = transforms_[i].runtime.hierarchy;
            if (hierarchy.parent_index != invalid_index ||
                hierarchy.first_child_index != invalid_index ||
                hierarchy.next_sibling_index != invalid_index) {
                return false;
            }
        }
        return true;
    }

    static void UpdateFlatHierarchy(TransformType* transforms_,
                                    std::uint32_t component_count_) {
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            TransformType& node = transforms_[i];
            if (HasDirtyFlags(node, transform_dirty_local_flag)) {
                RebuildLocalMatrix(node);
            }

            bool needs_world_update = HasDirtyFlags(node,
                                                    transform_dirty_world_flag | transform_dirty_hierarchy_flag);
            if (!needs_world_update) {
                needs_world_update = node.runtime.cached_parent_world_revision != 0U;
            }
            if (!needs_world_update) {
                continue;
            }

            node.runtime.world_matrix = node.runtime.local_matrix;
            node.runtime.cached_parent_world_revision = 0U;
            ++node.runtime.world_revision;
            node.runtime.dirty_flags &= ~(transform_dirty_world_flag | transform_dirty_hierarchy_flag);
        }
    }

    [[nodiscard]] static bool WouldCreateCycle(const TransformType* transforms_,
                                               std::uint32_t component_count_,
                                               std::uint32_t child_index_,
                                               std::uint32_t parent_index_) noexcept {
        std::int32_t cursor = static_cast<std::int32_t>(parent_index_);
        std::uint32_t guard = 0U;

        while (cursor >= 0 && static_cast<std::uint32_t>(cursor) < component_count_ && guard < component_count_) {
            if (cursor == static_cast<std::int32_t>(child_index_)) {
                return true;
            }
            cursor = transforms_[static_cast<std::uint32_t>(cursor)].runtime.hierarchy.parent_index;
            ++guard;
        }

        return false;
    }

    static void ProcessNode(TransformType* transforms_,
                            std::uint32_t component_count_,
                            std::uint32_t node_index_,
                            ScratchType& scratch_) {
        if (node_index_ >= component_count_) {
            return;
        }

        std::uint8_t& state = scratch_.visit_state[node_index_];
        if (state == 2U) {
            return;
        }

        TransformType& node = transforms_[node_index_];
        std::int32_t parent_index = node.runtime.hierarchy.parent_index;
        TransformType* parent = nullptr;

        if (parent_index >= 0 && static_cast<std::uint32_t>(parent_index) < component_count_) {
            parent = &transforms_[static_cast<std::uint32_t>(parent_index)];
            const std::uint8_t parent_state = scratch_.visit_state[static_cast<std::uint32_t>(parent_index)];
            if (parent_state != 2U) {
                if (state == 1U) {
                    node.runtime.hierarchy.parent_index = invalid_index;
                    parent = nullptr;
                    MarkDirty(node, transform_dirty_hierarchy_flag | transform_dirty_world_flag);
                } else {
                    state = 1U;
                    scratch_.stack.push_back(node_index_);
                    scratch_.stack.push_back(static_cast<std::uint32_t>(parent_index));
                    return;
                }
            }
        } else {
            parent = nullptr;
        }

        if (HasDirtyFlags(node, transform_dirty_local_flag)) {
            RebuildLocalMatrix(node);
        }

        bool needs_world_update = HasDirtyFlags(node,
                                                transform_dirty_world_flag | transform_dirty_hierarchy_flag);
        if (!needs_world_update && parent != nullptr) {
            needs_world_update = node.runtime.cached_parent_world_revision != parent->runtime.world_revision;
        }

        if (needs_world_update) {
            RebuildWorldFromParent(node, parent);
        }

        state = 2U;

        std::int32_t child_index = node.runtime.hierarchy.first_child_index;
        std::uint32_t guard = 0U;
        while (child_index >= 0 &&
               static_cast<std::uint32_t>(child_index) < component_count_ &&
               guard < component_count_) {
            scratch_.stack.push_back(static_cast<std::uint32_t>(child_index));
            child_index = transforms_[static_cast<std::uint32_t>(child_index)].runtime.hierarchy.next_sibling_index;
            ++guard;
        }
    }
};

} // namespace vr::ecs

