#pragma once

#include "vr/ecs/component/geometry_component.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/geometry/geometry_types.hpp"

#include <cstdint>

namespace vr::geometry {

constexpr std::uint32_t geometry_skeletal_component_enabled_flag = 1U << 0U;
constexpr std::uint32_t geometry_skeletal_component_root_motion_extracted_flag = 1U << 1U;

struct GeometrySkeletalPaletteBuildStats final {
    std::uint32_t component_count = 0U;
    std::uint32_t skinned_component_count = 0U;
    std::uint32_t matrix_count = 0U;
    std::uint64_t signature = 0U;
};

class GeometrySkeletalPaletteBuilder final {
public:
    [[nodiscard]] static GeometrySkeletalPaletteBuildStats Build(
        const ecs::Geometry<ecs::Dim3>* geometry_components_,
        std::uint32_t component_count_,
        const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
        std::uint32_t skeletal_output_count_,
        GeometryMcVector<GeometrySkeletalComponentGpu>& out_components_,
        GeometryMcVector<GeometrySkeletalMatrixGpu>& out_matrices_) noexcept {
        GeometrySkeletalPaletteBuildStats stats{};
        stats.component_count = component_count_;

        out_components_.clear();
        out_matrices_.clear();

        if (component_count_ == 0U) {
            out_components_.resize(1U);
            out_matrices_.resize(1U);
            out_matrices_[0U].matrix = ecs::spatial_math::IdentityMatrix4x4();
            return stats;
        }

        out_components_.resize(component_count_);
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            out_components_[i] = {};
        }

        std::uint64_t signature = 14695981039346656037ULL;
        for (std::uint32_t component_index = 0U; component_index < component_count_; ++component_index) {
            const ecs::Geometry<ecs::Dim3>& component = geometry_components_[component_index];
            HashCombine(signature, static_cast<std::uint64_t>(component.mesh.flags));

            if ((component.mesh.flags & vr::ecs::geometry_mesh_skeletal_skinning_flag) == 0U ||
                skeletal_outputs_ == nullptr ||
                component_index >= skeletal_output_count_) {
                continue;
            }

            const ecs::SkeletalPoseOutputState<ecs::Dim3>& output = skeletal_outputs_[component_index];
            HashCombine(signature, static_cast<std::uint64_t>(output.revision));
            HashCombine(signature, static_cast<std::uint64_t>(output.sampled_joint_count));
            HashCombine(signature, static_cast<std::uint64_t>(output.bind_pose_joint_count));

            if (output.joints == nullptr ||
                output.bind_pose_joints == nullptr ||
                output.sampled_joint_count == 0U ||
                output.joint_count == 0U ||
                output.bind_pose_joint_count == 0U) {
                continue;
            }

            const std::uint32_t joint_count = std::min(output.sampled_joint_count,
                                                       std::min(output.joint_count, output.bind_pose_joint_count));
            if (joint_count == 0U) {
                continue;
            }

            GeometrySkeletalComponentGpu& component_gpu = out_components_[component_index];
            component_gpu.matrix_offset = static_cast<std::uint32_t>(out_matrices_.size());
            component_gpu.joint_count = joint_count;
            component_gpu.flags = geometry_skeletal_component_enabled_flag;

            ecs::Matrix4x4 inverse_root_motion = ecs::spatial_math::IdentityMatrix4x4();
            const bool extract_root_motion =
                (component.mesh.flags & vr::ecs::geometry_mesh_skeletal_root_motion_flag) != 0U;
            if (extract_root_motion) {
                component_gpu.flags |= geometry_skeletal_component_root_motion_extracted_flag;
                const ecs::Matrix4x4 root_motion =
                    ecs::spatial_math::ComposeMatrix4x4Trs(output.joints[0U].position,
                                                           output.joints[0U].rotation,
                                                           output.joints[0U].scale);
                if (!ecs::spatial_math::InvertAffineMatrix4x4(root_motion, inverse_root_motion)) {
                    inverse_root_motion = ecs::spatial_math::IdentityMatrix4x4();
                }
            }

            out_matrices_.resize(out_matrices_.size() + joint_count);
            for (std::uint32_t joint_index = 0U; joint_index < joint_count; ++joint_index) {
                const ecs::Matrix4x4 animated_joint =
                    ecs::spatial_math::ComposeMatrix4x4Trs(output.joints[joint_index].position,
                                                           output.joints[joint_index].rotation,
                                                           output.joints[joint_index].scale);
                const ecs::Matrix4x4 bind_joint =
                    ecs::spatial_math::ComposeMatrix4x4Trs(output.bind_pose_joints[joint_index].position,
                                                           output.bind_pose_joints[joint_index].rotation,
                                                           output.bind_pose_joints[joint_index].scale);
                ecs::Matrix4x4 inverse_bind = ecs::spatial_math::IdentityMatrix4x4();
                if (!ecs::spatial_math::InvertAffineMatrix4x4(bind_joint, inverse_bind)) {
                    inverse_bind = ecs::spatial_math::IdentityMatrix4x4();
                }

                ecs::Matrix4x4 skin_matrix =
                    ecs::spatial_math::MultiplyMatrix4x4(animated_joint, inverse_bind);
                if (extract_root_motion) {
                    skin_matrix = ecs::spatial_math::MultiplyMatrix4x4(inverse_root_motion, skin_matrix);
                }
                out_matrices_[component_gpu.matrix_offset + joint_index].matrix = skin_matrix;
            }

            ++stats.skinned_component_count;
        }

        if (out_matrices_.empty()) {
            out_matrices_.resize(1U);
            out_matrices_[0U].matrix = ecs::spatial_math::IdentityMatrix4x4();
        }

        stats.matrix_count = static_cast<std::uint32_t>(out_matrices_.size());
        stats.signature = signature;
        return stats;
    }

private:
    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= 1099511628211ULL;
    }
};

} // namespace vr::geometry
