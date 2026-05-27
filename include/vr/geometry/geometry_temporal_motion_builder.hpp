#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/geometry_runtime_system.hpp"
#include "vr/geometry/geometry_skeletal_palette_builder.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace vr::geometry {

template<typename T>
using GeometryTemporalMotionMcVector =
    Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct Geometry3DTemporalMotionInstance final {
    float previous_world_m00 = 0.0F;
    float previous_world_m01 = 0.0F;
    float previous_world_m02 = 0.0F;
    float previous_world_m03 = 0.0F;

    float previous_world_m10 = 0.0F;
    float previous_world_m11 = 0.0F;
    float previous_world_m12 = 0.0F;
    float previous_world_m13 = 0.0F;

    float previous_world_m20 = 0.0F;
    float previous_world_m21 = 0.0F;
    float previous_world_m22 = 0.0F;
    float previous_world_m23 = 0.0F;

    float previous_world_m30 = 0.0F;
    float previous_world_m31 = 0.0F;
    float previous_world_m32 = 0.0F;
    float previous_world_m33 = 0.0F;

    float previous_deform_param0_x = 0.0F;
    float previous_deform_param0_y = 0.0F;
    float previous_deform_param0_z = 0.0F;
    float previous_deform_param0_w = 0.0F;

    float previous_deform_param1_x = 0.0F;
    float previous_deform_param1_y = 0.0F;
    float previous_deform_param1_z = 0.0F;
    float previous_deform_param1_w = 0.0F;

    float confidence_seed = 0.0F;
    std::uint32_t flags = 0U;
    std::uint32_t previous_skeletal_component_index = 0U;
    std::uint32_t reserved1 = 0U;
};

struct Geometry3DTemporalMotionBuildStats final {
    std::uint32_t rigid_candidate_count = 0U;
    std::uint32_t previous_match_count = 0U;
    std::uint32_t fallback_count = 0U;
};

struct Geometry3DTemporalRuntimeWorldHistory final {
    GeometryTemporalMotionMcVector<ecs::Matrix4x4> component_world_matrices{};
    GeometryTemporalMotionMcVector<std::uint8_t>
        component_previous_available{};
    GeometryTemporalMotionMcVector<std::uint64_t> component_identity_keys{};
    GeometryTemporalMotionMcVector<std::uint8_t>
        component_explicit_identity_available{};
    std::uint64_t runtime_world_signature = 0U;
    std::uint64_t capture_id = 0U;
    bool available = false;
};

struct Geometry3DTemporalSkeletalPaletteHistory final {
    GeometryTemporalMotionMcVector<GeometrySkeletalComponentGpu>
        skeletal_components{};
    GeometryTemporalMotionMcVector<GeometrySkeletalMatrixGpu>
        skeletal_matrices{};
    std::uint64_t palette_signature = 0U;
    std::uint64_t capture_id = 0U;
    bool available = false;
};

struct Geometry3DTemporalVertexDeformHistory final {
    GeometryTemporalMotionMcVector<ecs::Float4> component_deform_param0{};
    GeometryTemporalMotionMcVector<ecs::Float4> component_deform_param1{};
    GeometryTemporalMotionMcVector<std::uint8_t>
        component_previous_available{};
    std::uint64_t deform_signature = 0U;
    std::uint64_t capture_id = 0U;
    bool available = false;
};

struct Geometry3DTemporalSkeletalPaletteView final {
    const GeometrySkeletalComponentGpu* skeletal_components = nullptr;
    const GeometrySkeletalMatrixGpu* skeletal_matrices = nullptr;
    std::uint32_t component_count = 0U;
    std::uint32_t matrix_count = 0U;
    bool available = false;
};

static_assert(std::is_standard_layout_v<Geometry3DTemporalMotionInstance>);
static_assert(std::is_trivially_copyable_v<Geometry3DTemporalMotionInstance>);

constexpr std::uint32_t geometry_3d_temporal_motion_flag_previous_valid = 1U << 0U;

inline void ResetGeometry3DTemporalRuntimeWorldHistory(
    Geometry3DTemporalRuntimeWorldHistory& history_) noexcept {
    history_.component_world_matrices.clear();
    history_.component_previous_available.clear();
    history_.component_identity_keys.clear();
    history_.component_explicit_identity_available.clear();
    history_.runtime_world_signature = 0U;
    history_.capture_id = 0U;
    history_.available = false;
}

inline void ResetGeometry3DTemporalSkeletalPaletteHistory(
    Geometry3DTemporalSkeletalPaletteHistory& history_) noexcept {
    history_.skeletal_components.clear();
    history_.skeletal_matrices.clear();
    history_.palette_signature = 0U;
    history_.capture_id = 0U;
    history_.available = false;
}

inline void ResetGeometry3DTemporalVertexDeformHistory(
    Geometry3DTemporalVertexDeformHistory& history_) noexcept {
    history_.component_deform_param0.clear();
    history_.component_deform_param1.clear();
    history_.component_previous_available.clear();
    history_.deform_signature = 0U;
    history_.capture_id = 0U;
    history_.available = false;
}

inline void HashGeometry3DTemporalRuntimeWorldHistory(
    std::uint64_t& hash_,
    const std::uint64_t value_) noexcept {
    hash_ ^= value_;
    hash_ *= 1099511628211ULL;
}

[[nodiscard]] inline std::uint32_t
Geometry3DTemporalFloatBits(const float value_) noexcept {
    return std::bit_cast<std::uint32_t>(value_);
}

struct Geometry3DTemporalVertexDeformParams final {
    ecs::Float4 param0{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    ecs::Float4 param1{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    bool available = false;
};

[[nodiscard]] inline const ecs::SkeletalPoseOutputState<ecs::Dim3>*
ResolveGeometry3DTemporalSkeletalOutput(
    const std::uint32_t component_index_,
    const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
    const std::uint32_t skeletal_output_count_) noexcept {
    if (skeletal_outputs_ == nullptr ||
        component_index_ >= skeletal_output_count_) {
        return nullptr;
    }
    return skeletal_outputs_ + component_index_;
}

[[nodiscard]] inline bool HasGeometry3DTemporalVertexDeform(
    const ecs::Geometry<ecs::Dim3>& component_) noexcept {
    return (component_.mesh.flags &
            vr::ecs::geometry_mesh_vertex_deform_shader_flag) != 0U;
}

[[nodiscard]] inline const ecs::VertexDeformOutputState*
ResolveGeometry3DTemporalVertexDeformOutput(
    const std::uint32_t component_index_,
    const ecs::VertexDeformOutputState* vertex_deform_outputs_,
    const std::uint32_t vertex_deform_output_count_) noexcept {
    if (vertex_deform_outputs_ == nullptr ||
        component_index_ >= vertex_deform_output_count_) {
        return nullptr;
    }
    return vertex_deform_outputs_ + component_index_;
}

[[nodiscard]] inline Geometry3DTemporalVertexDeformParams
ResolveGeometry3DTemporalVertexDeformParams(
    const ecs::Geometry<ecs::Dim3>& component_,
    const std::uint32_t component_index_,
    const ecs::VertexDeformOutputState* vertex_deform_outputs_,
    const std::uint32_t vertex_deform_output_count_) noexcept {
    Geometry3DTemporalVertexDeformParams params{};
    if (!HasGeometry3DTemporalVertexDeform(component_)) {
        return params;
    }

    params.available = true;
    const ecs::VertexDeformOutputState* output =
        ResolveGeometry3DTemporalVertexDeformOutput(component_index_,
                                                    vertex_deform_outputs_,
                                                    vertex_deform_output_count_);
    if (output == nullptr || output->parameters == nullptr ||
        output->sampled_parameter_count == 0U ||
        output->parameter_count == 0U) {
        return params;
    }

    params.param0 = output->parameters[0U];
    if (output->sampled_parameter_count > 1U &&
        output->parameter_count > 1U) {
        params.param1 = output->parameters[1U];
    }
    return params;
}

[[nodiscard]] inline bool HasGeometry3DTemporalExplicitObjectIdentity(
    const ecs::Geometry<ecs::Dim3>& component_) noexcept {
    return component_.runtime.route.user_data != 0U;
}

[[nodiscard]] inline std::uint64_t
ComputeGeometry3DTemporalExplicitObjectIdentityKey(
    const ecs::Geometry<ecs::Dim3>& component_) noexcept {
    return static_cast<std::uint64_t>(component_.runtime.route.user_data);
}

[[nodiscard]] inline ecs::Matrix4x4
ComposeGeometry3DTemporalRootMotionMatrix(
    const ecs::SkeletalPoseOutputState<ecs::Dim3>& output_) noexcept {
    if (output_.joints == nullptr ||
        output_.sampled_joint_count == 0U ||
        output_.joint_count == 0U) {
        return ecs::spatial_math::IdentityMatrix4x4();
    }

    const ecs::SkeletalJointPose<ecs::Dim3>& root = output_.joints[0U];
    return ecs::spatial_math::ComposeMatrix4x4Trs(root.position,
                                                  root.rotation,
                                                  root.scale);
}

[[nodiscard]] inline ecs::Matrix4x4
ComputeGeometry3DTemporalRuntimeWorldMatrix(
    const ecs::Geometry<ecs::Dim3>* geometry_components_,
    const ecs::Transform<ecs::Dim3>* transforms_,
    const std::uint32_t component_index_,
    const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
    const std::uint32_t skeletal_output_count_) noexcept {
    ecs::Matrix4x4 world_matrix = ecs::spatial_math::IdentityMatrix4x4();
    if (transforms_ != nullptr) {
        world_matrix = transforms_[component_index_].runtime.world_matrix;
    }

    if (geometry_components_ == nullptr) {
        return world_matrix;
    }

    const ecs::Geometry<ecs::Dim3>& component =
        geometry_components_[component_index_];
    if ((component.mesh.flags &
         vr::ecs::geometry_mesh_skeletal_root_motion_flag) == 0U) {
        return world_matrix;
    }

    const ecs::SkeletalPoseOutputState<ecs::Dim3>* output =
        ResolveGeometry3DTemporalSkeletalOutput(component_index_,
                                                skeletal_outputs_,
                                                skeletal_output_count_);
    if (output == nullptr ||
        output->joints == nullptr ||
        output->sampled_joint_count == 0U ||
        output->joint_count == 0U) {
        return world_matrix;
    }

    return ecs::spatial_math::MultiplyMatrix4x4(
        world_matrix,
        ComposeGeometry3DTemporalRootMotionMatrix(*output));
}

[[nodiscard]] inline std::uint64_t
ComputeGeometry3DTemporalRuntimeWorldSignature(
    const ecs::Geometry<ecs::Dim3>* geometry_components_,
    const ecs::Transform<ecs::Dim3>* transforms_,
    const std::uint32_t component_count_,
    const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
    const std::uint32_t skeletal_output_count_) noexcept {
    std::uint64_t signature = 14695981039346656037ULL;
    HashGeometry3DTemporalRuntimeWorldHistory(
        signature,
        static_cast<std::uint64_t>(component_count_));
    for (std::uint32_t component_index = 0U; component_index < component_count_;
         ++component_index) {
        const std::uint64_t component_flags =
            (geometry_components_ != nullptr)
                ? static_cast<std::uint64_t>(
                      geometry_components_[component_index].mesh.flags)
                : 0U;
        HashGeometry3DTemporalRuntimeWorldHistory(signature, component_flags);
        if (geometry_components_ != nullptr &&
            HasGeometry3DTemporalExplicitObjectIdentity(
                geometry_components_[component_index])) {
            HashGeometry3DTemporalRuntimeWorldHistory(signature, 1U);
            HashGeometry3DTemporalRuntimeWorldHistory(
                signature,
                ComputeGeometry3DTemporalExplicitObjectIdentityKey(
                    geometry_components_[component_index]));
        } else {
            HashGeometry3DTemporalRuntimeWorldHistory(signature, 0U);
        }

        const std::uint64_t world_revision =
            (transforms_ != nullptr)
                ? static_cast<std::uint64_t>(
                      transforms_[component_index].runtime.world_revision)
                : 0U;
        HashGeometry3DTemporalRuntimeWorldHistory(signature, world_revision);

        if (geometry_components_ == nullptr ||
            (geometry_components_[component_index].mesh.flags &
             vr::ecs::geometry_mesh_skeletal_root_motion_flag) == 0U) {
            continue;
        }

        const ecs::SkeletalPoseOutputState<ecs::Dim3>* output =
            ResolveGeometry3DTemporalSkeletalOutput(component_index,
                                                    skeletal_outputs_,
                                                    skeletal_output_count_);
        if (output == nullptr) {
            continue;
        }

        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(output->revision));
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(output->sampled_joint_count));
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(output->joint_count));
    }
    return signature;
}

inline void CaptureGeometry3DTemporalRuntimeWorldHistory(
    const ecs::Geometry<ecs::Dim3>* geometry_components_,
    const ecs::Transform<ecs::Dim3>* transforms_,
    const std::uint32_t component_count_,
    const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
    const std::uint32_t skeletal_output_count_,
    Geometry3DTemporalRuntimeWorldHistory& out_history_,
    const std::uint64_t capture_id_ = 0U) {
    out_history_.component_world_matrices.resize(component_count_);
    out_history_.component_previous_available.resize(component_count_);
    out_history_.component_identity_keys.resize(component_count_);
    out_history_.component_explicit_identity_available.resize(component_count_);

    for (std::uint32_t component_index = 0U; component_index < component_count_;
         ++component_index) {
        out_history_.component_world_matrices[component_index] =
            ComputeGeometry3DTemporalRuntimeWorldMatrix(geometry_components_,
                                                        transforms_,
                                                        component_index,
                                                        skeletal_outputs_,
                                                        skeletal_output_count_);
        out_history_.component_previous_available[component_index] = 1U;
        const bool has_explicit_identity =
            geometry_components_ != nullptr &&
            HasGeometry3DTemporalExplicitObjectIdentity(
                geometry_components_[component_index]);
        out_history_.component_identity_keys[component_index] =
            has_explicit_identity
                ? ComputeGeometry3DTemporalExplicitObjectIdentityKey(
                      geometry_components_[component_index])
                : 0U;
        out_history_.component_explicit_identity_available[component_index] =
            has_explicit_identity ? 1U : 0U;
    }

    out_history_.runtime_world_signature =
        ComputeGeometry3DTemporalRuntimeWorldSignature(geometry_components_,
                                                       transforms_,
                                                       component_count_,
                                                       skeletal_outputs_,
                                                       skeletal_output_count_);
    out_history_.capture_id = capture_id_;
    out_history_.available = component_count_ > 0U;
}

[[nodiscard]] inline std::uint64_t
ComputeGeometry3DTemporalVertexDeformSignature(
    const ecs::Geometry<ecs::Dim3>* geometry_components_,
    const std::uint32_t component_count_,
    const ecs::VertexDeformOutputState* vertex_deform_outputs_,
    const std::uint32_t vertex_deform_output_count_) noexcept {
    std::uint64_t signature = 14695981039346656037ULL;
    HashGeometry3DTemporalRuntimeWorldHistory(
        signature,
        static_cast<std::uint64_t>(component_count_));
    if (geometry_components_ == nullptr || component_count_ == 0U) {
        return signature;
    }

    for (std::uint32_t component_index = 0U; component_index < component_count_;
         ++component_index) {
        const Geometry3DTemporalVertexDeformParams params =
            ResolveGeometry3DTemporalVertexDeformParams(
                geometry_components_[component_index],
                component_index,
                vertex_deform_outputs_,
                vertex_deform_output_count_);
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(component_index));
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(params.available ? 1U : 0U));
        if (!params.available) {
            continue;
        }

        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(
                Geometry3DTemporalFloatBits(params.param0.x)));
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(
                Geometry3DTemporalFloatBits(params.param0.y)));
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(
                Geometry3DTemporalFloatBits(params.param0.z)));
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(
                Geometry3DTemporalFloatBits(params.param0.w)));
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(
                Geometry3DTemporalFloatBits(params.param1.x)));
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(
                Geometry3DTemporalFloatBits(params.param1.y)));
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(
                Geometry3DTemporalFloatBits(params.param1.z)));
        HashGeometry3DTemporalRuntimeWorldHistory(
            signature,
            static_cast<std::uint64_t>(
                Geometry3DTemporalFloatBits(params.param1.w)));
    }
    return signature;
}

inline void CaptureGeometry3DTemporalVertexDeformHistory(
    const ecs::Geometry<ecs::Dim3>* geometry_components_,
    const std::uint32_t component_count_,
    const ecs::VertexDeformOutputState* vertex_deform_outputs_,
    const std::uint32_t vertex_deform_output_count_,
    Geometry3DTemporalVertexDeformHistory& out_history_,
    const std::uint64_t capture_id_ = 0U) {
    out_history_.component_deform_param0.resize(component_count_);
    out_history_.component_deform_param1.resize(component_count_);
    out_history_.component_previous_available.resize(component_count_);

    std::uint32_t captured_component_count = 0U;
    for (std::uint32_t component_index = 0U; component_index < component_count_;
         ++component_index) {
        Geometry3DTemporalVertexDeformParams params{};
        if (geometry_components_ != nullptr) {
            params = ResolveGeometry3DTemporalVertexDeformParams(
                geometry_components_[component_index],
                component_index,
                vertex_deform_outputs_,
                vertex_deform_output_count_);
        }
        out_history_.component_deform_param0[component_index] = params.param0;
        out_history_.component_deform_param1[component_index] = params.param1;
        out_history_.component_previous_available[component_index] =
            params.available ? 1U : 0U;
        if (params.available) {
            ++captured_component_count;
        }
    }

    out_history_.deform_signature =
        ComputeGeometry3DTemporalVertexDeformSignature(
            geometry_components_,
            component_count_,
            vertex_deform_outputs_,
            vertex_deform_output_count_);
    out_history_.capture_id = capture_id_;
    out_history_.available = captured_component_count > 0U;
}

inline void CaptureGeometry3DTemporalSkeletalPaletteHistory(
    const GeometrySkeletalPaletteBuildStats& build_stats_,
    const GeometryMcVector<GeometrySkeletalComponentGpu>& skeletal_components_,
    const GeometryMcVector<GeometrySkeletalMatrixGpu>& skeletal_matrices_,
    Geometry3DTemporalSkeletalPaletteHistory& out_history_,
    const std::uint64_t capture_id_ = 0U) {
    out_history_.skeletal_components.resize(skeletal_components_.size());
    std::copy(skeletal_components_.begin(),
              skeletal_components_.end(),
              out_history_.skeletal_components.begin());
    out_history_.skeletal_matrices.resize(skeletal_matrices_.size());
    std::copy(skeletal_matrices_.begin(),
              skeletal_matrices_.end(),
              out_history_.skeletal_matrices.begin());
    out_history_.palette_signature = build_stats_.signature;
    out_history_.capture_id = capture_id_;
    out_history_.available = build_stats_.skinned_component_count > 0U &&
                             !out_history_.skeletal_components.empty() &&
                             !out_history_.skeletal_matrices.empty();
}

[[nodiscard]] inline Geometry3DTemporalSkeletalPaletteView
MakeGeometry3DTemporalSkeletalPaletteView(
    const Geometry3DTemporalSkeletalPaletteHistory& history_) noexcept {
    return Geometry3DTemporalSkeletalPaletteView{
        .skeletal_components = history_.skeletal_components.empty()
            ? nullptr
            : history_.skeletal_components.data(),
        .skeletal_matrices = history_.skeletal_matrices.empty()
            ? nullptr
            : history_.skeletal_matrices.data(),
        .component_count =
            static_cast<std::uint32_t>(history_.skeletal_components.size()),
        .matrix_count =
            static_cast<std::uint32_t>(history_.skeletal_matrices.size()),
        .available = history_.available,
    };
}

[[nodiscard]] inline Geometry3DTemporalSkeletalPaletteView
MakeGeometry3DTemporalSkeletalPaletteView(
    const GeometryMcVector<GeometrySkeletalComponentGpu>& skeletal_components_,
    const GeometryMcVector<GeometrySkeletalMatrixGpu>& skeletal_matrices_,
    const bool available_) noexcept {
    return Geometry3DTemporalSkeletalPaletteView{
        .skeletal_components =
            skeletal_components_.empty() ? nullptr : skeletal_components_.data(),
        .skeletal_matrices =
            skeletal_matrices_.empty() ? nullptr : skeletal_matrices_.data(),
        .component_count =
            static_cast<std::uint32_t>(skeletal_components_.size()),
        .matrix_count = static_cast<std::uint32_t>(skeletal_matrices_.size()),
        .available = available_,
    };
}

[[nodiscard]] inline bool HasEnabledGeometry3DTemporalSkeletalComponent(
    const Geometry3DTemporalSkeletalPaletteView& palette_view_,
    const std::uint32_t component_index_) noexcept {
    return palette_view_.available &&
           palette_view_.skeletal_components != nullptr &&
           component_index_ < palette_view_.component_count &&
           (palette_view_.skeletal_components[component_index_].flags &
            geometry_skeletal_component_enabled_flag) != 0U;
}

[[nodiscard]] inline bool HasGeometry3DTemporalRuntimeWorldHistoryData(
    const Geometry3DTemporalRuntimeWorldHistory& history_) noexcept {
    return history_.available &&
           history_.component_world_matrices.size() ==
               history_.component_previous_available.size();
}

[[nodiscard]] inline bool HasGeometry3DTemporalRuntimeWorldIdentityData(
    const Geometry3DTemporalRuntimeWorldHistory& history_) noexcept {
    return HasGeometry3DTemporalRuntimeWorldHistoryData(history_) &&
           history_.component_world_matrices.size() ==
               history_.component_identity_keys.size() &&
           history_.component_world_matrices.size() ==
               history_.component_explicit_identity_available.size();
}

[[nodiscard]] inline bool HasGeometry3DTemporalVertexDeformHistoryData(
    const Geometry3DTemporalVertexDeformHistory& history_) noexcept {
    return history_.available &&
           history_.component_deform_param0.size() ==
               history_.component_deform_param1.size() &&
           history_.component_deform_param0.size() ==
               history_.component_previous_available.size();
}

[[nodiscard]] inline bool HasGeometry3DTemporalExplicitIdentityAt(
    const Geometry3DTemporalRuntimeWorldHistory& history_,
    const std::uint32_t component_index_) noexcept {
    return HasGeometry3DTemporalRuntimeWorldIdentityData(history_) &&
           component_index_ < history_.component_explicit_identity_available.size() &&
           history_.component_explicit_identity_available[component_index_] != 0U;
}

[[nodiscard]] inline std::uint32_t
FindUniqueGeometry3DTemporalPreviousComponentIndexByIdentity(
    const Geometry3DTemporalRuntimeWorldHistory& history_,
    const std::uint64_t identity_key_) noexcept {
    if (identity_key_ == 0U ||
        !HasGeometry3DTemporalRuntimeWorldIdentityData(history_)) {
        return std::numeric_limits<std::uint32_t>::max();
    }

    std::uint32_t matched_index = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t component_index = 0U;
         component_index <
         static_cast<std::uint32_t>(history_.component_identity_keys.size());
         ++component_index) {
        if (history_.component_previous_available[component_index] == 0U ||
            history_.component_explicit_identity_available[component_index] ==
                0U ||
            history_.component_identity_keys[component_index] != identity_key_) {
            continue;
        }
        if (matched_index != std::numeric_limits<std::uint32_t>::max()) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        matched_index = component_index;
    }
    return matched_index;
}

struct Geometry3DTemporalPreviousObjectMatch final {
    std::uint32_t previous_component_index =
        std::numeric_limits<std::uint32_t>::max();
    bool explicit_identity_matched = false;
    bool available = false;
};

[[nodiscard]] inline Geometry3DTemporalPreviousObjectMatch
ResolveGeometry3DTemporalPreviousObjectMatch(
    const ecs::Geometry<ecs::Dim3>& component_,
    const std::uint32_t component_index_,
    const Geometry3DTemporalRuntimeWorldHistory& previous_runtime_world_history_)
    noexcept {
    if (!HasGeometry3DTemporalRuntimeWorldHistoryData(
            previous_runtime_world_history_)) {
        return {};
    }

    if (HasGeometry3DTemporalExplicitObjectIdentity(component_)) {
        const std::uint32_t previous_component_index =
            FindUniqueGeometry3DTemporalPreviousComponentIndexByIdentity(
                previous_runtime_world_history_,
                ComputeGeometry3DTemporalExplicitObjectIdentityKey(component_));
        if (previous_component_index ==
            std::numeric_limits<std::uint32_t>::max()) {
            return {};
        }
        return Geometry3DTemporalPreviousObjectMatch{
            .previous_component_index = previous_component_index,
            .explicit_identity_matched = true,
            .available = true,
        };
    }

    if (component_index_ >=
            previous_runtime_world_history_.component_previous_available.size() ||
        previous_runtime_world_history_
                .component_previous_available[component_index_] == 0U ||
        HasGeometry3DTemporalExplicitIdentityAt(previous_runtime_world_history_,
                                                component_index_)) {
        return {};
    }

    return Geometry3DTemporalPreviousObjectMatch{
        .previous_component_index = component_index_,
        .explicit_identity_matched = false,
        .available = true,
    };
}

[[nodiscard]] inline bool
AreGeometry3DTemporalSkeletalComponentsCompatible(
    const Geometry3DTemporalSkeletalPaletteView& current_palette_view_,
    const Geometry3DTemporalSkeletalPaletteView& previous_palette_view_,
    const std::uint32_t current_component_index_,
    const std::uint32_t previous_component_index_) noexcept {
    return HasEnabledGeometry3DTemporalSkeletalComponent(
               current_palette_view_,
               current_component_index_) &&
           HasEnabledGeometry3DTemporalSkeletalComponent(
               previous_palette_view_,
               previous_component_index_) &&
           ((current_palette_view_
                 .skeletal_components[current_component_index_]
                 .flags ^
             previous_palette_view_
                 .skeletal_components[previous_component_index_]
                 .flags) &
            geometry_skeletal_component_root_motion_extracted_flag) == 0U &&
           current_palette_view_.skeletal_components[current_component_index_]
                   .joint_count ==
               previous_palette_view_
                   .skeletal_components[previous_component_index_]
                   .joint_count;
}

[[nodiscard]] inline bool
AreGeometry3DTemporalPreviousSkinnedHistoriesAligned(
    const Geometry3DTemporalRuntimeWorldHistory& previous_runtime_world_history_,
    const Geometry3DTemporalSkeletalPaletteHistory&
        previous_skeletal_palette_history_) noexcept {
    return previous_runtime_world_history_.available &&
           previous_skeletal_palette_history_.available &&
           previous_runtime_world_history_.capture_id != 0U &&
           previous_runtime_world_history_.capture_id ==
               previous_skeletal_palette_history_.capture_id;
}

[[nodiscard]] inline bool
AreGeometry3DTemporalPreviousVertexDeformHistoriesAligned(
    const Geometry3DTemporalRuntimeWorldHistory& previous_runtime_world_history_,
    const Geometry3DTemporalVertexDeformHistory&
        previous_vertex_deform_history_) noexcept {
    return previous_runtime_world_history_.available &&
           previous_vertex_deform_history_.available &&
           previous_runtime_world_history_.capture_id != 0U &&
           previous_runtime_world_history_.capture_id ==
               previous_vertex_deform_history_.capture_id;
}

[[nodiscard]] inline ecs::Matrix4x4 ReadGeometry3DInstanceWorldMatrix(
    const ecs::Geometry3DGpuInstance& instance_) noexcept {
    ecs::Matrix4x4 matrix{};
    matrix.m[0U] = instance_.world_m00;
    matrix.m[1U] = instance_.world_m01;
    matrix.m[2U] = instance_.world_m02;
    matrix.m[3U] = instance_.world_m03;
    matrix.m[4U] = instance_.world_m10;
    matrix.m[5U] = instance_.world_m11;
    matrix.m[6U] = instance_.world_m12;
    matrix.m[7U] = instance_.world_m13;
    matrix.m[8U] = instance_.world_m20;
    matrix.m[9U] = instance_.world_m21;
    matrix.m[10U] = instance_.world_m22;
    matrix.m[11U] = instance_.world_m23;
    matrix.m[12U] = instance_.world_m30;
    matrix.m[13U] = instance_.world_m31;
    matrix.m[14U] = instance_.world_m32;
    matrix.m[15U] = instance_.world_m33;
    return matrix;
}

[[nodiscard]] inline Geometry3DTemporalVertexDeformParams
ReadGeometry3DInstanceVertexDeformParams(
    const ecs::Geometry3DGpuInstance& instance_) noexcept {
    return Geometry3DTemporalVertexDeformParams{
        .param0 = ecs::Float4{
            .x = instance_.deform_param0_x,
            .y = instance_.deform_param0_y,
            .z = instance_.deform_param0_z,
            .w = instance_.deform_param0_w,
        },
        .param1 = ecs::Float4{
            .x = instance_.deform_param1_x,
            .y = instance_.deform_param1_y,
            .z = instance_.deform_param1_z,
            .w = instance_.deform_param1_w,
        },
        .available = true,
    };
}

[[nodiscard]] inline Geometry3DTemporalVertexDeformParams
ReadGeometry3DTemporalPreviousVertexDeformParams(
    const Geometry3DTemporalVertexDeformHistory& history_,
    const std::uint32_t component_index_) noexcept {
    if (!HasGeometry3DTemporalVertexDeformHistoryData(history_) ||
        component_index_ >= history_.component_previous_available.size() ||
        history_.component_previous_available[component_index_] == 0U) {
        return {};
    }

    return Geometry3DTemporalVertexDeformParams{
        .param0 = history_.component_deform_param0[component_index_],
        .param1 = history_.component_deform_param1[component_index_],
        .available = true,
    };
}

inline void WriteGeometry3DTemporalMotionWorldMatrix(
    Geometry3DTemporalMotionInstance& instance_,
    const ecs::Matrix4x4& matrix_) noexcept {
    instance_.previous_world_m00 = matrix_.m[0U];
    instance_.previous_world_m01 = matrix_.m[1U];
    instance_.previous_world_m02 = matrix_.m[2U];
    instance_.previous_world_m03 = matrix_.m[3U];
    instance_.previous_world_m10 = matrix_.m[4U];
    instance_.previous_world_m11 = matrix_.m[5U];
    instance_.previous_world_m12 = matrix_.m[6U];
    instance_.previous_world_m13 = matrix_.m[7U];
    instance_.previous_world_m20 = matrix_.m[8U];
    instance_.previous_world_m21 = matrix_.m[9U];
    instance_.previous_world_m22 = matrix_.m[10U];
    instance_.previous_world_m23 = matrix_.m[11U];
    instance_.previous_world_m30 = matrix_.m[12U];
    instance_.previous_world_m31 = matrix_.m[13U];
    instance_.previous_world_m32 = matrix_.m[14U];
    instance_.previous_world_m33 = matrix_.m[15U];
}

inline void WriteGeometry3DTemporalMotionPreviousVertexDeformParams(
    Geometry3DTemporalMotionInstance& instance_,
    const Geometry3DTemporalVertexDeformParams& params_) noexcept {
    instance_.previous_deform_param0_x = params_.param0.x;
    instance_.previous_deform_param0_y = params_.param0.y;
    instance_.previous_deform_param0_z = params_.param0.z;
    instance_.previous_deform_param0_w = params_.param0.w;
    instance_.previous_deform_param1_x = params_.param1.x;
    instance_.previous_deform_param1_y = params_.param1.y;
    instance_.previous_deform_param1_z = params_.param1.z;
    instance_.previous_deform_param1_w = params_.param1.w;
}

inline void WriteGeometry3DTemporalPreviousSkeletalComponentIndex(
    Geometry3DTemporalMotionInstance& instance_,
    const std::uint32_t previous_component_index_) noexcept {
    instance_.previous_skeletal_component_index = previous_component_index_;
}

[[nodiscard]] inline bool IsGeometry3DTemporalRigidEligible(
    const ecs::Geometry<ecs::Dim3>& component_) noexcept {
    constexpr std::uint16_t k_non_rigid_flags =
        vr::ecs::geometry_mesh_vertex_deform_shader_flag |
        vr::ecs::geometry_mesh_frame_sequence_submesh_flag |
        vr::ecs::geometry_mesh_skeletal_root_motion_flag |
        vr::ecs::geometry_mesh_morph_targets_flag |
        vr::ecs::geometry_mesh_skeletal_skinning_flag;
    return (component_.mesh.flags & k_non_rigid_flags) == 0U;
}

[[nodiscard]] inline bool
HasGeometry3DTemporalUnsupportedDeformation(
    const ecs::Geometry<ecs::Dim3>& component_) noexcept {
    constexpr std::uint16_t k_unsupported_flags =
        vr::ecs::geometry_mesh_frame_sequence_submesh_flag |
        vr::ecs::geometry_mesh_morph_targets_flag;
    return (component_.mesh.flags & k_unsupported_flags) != 0U;
}

[[nodiscard]] inline bool IsGeometry3DTemporalVertexDeformDirectEligible(
    const ecs::Geometry<ecs::Dim3>& component_) noexcept {
    constexpr std::uint16_t k_direct_world_disallowed_flags =
        vr::ecs::geometry_mesh_skeletal_root_motion_flag |
        vr::ecs::geometry_mesh_skeletal_skinning_flag |
        vr::ecs::geometry_mesh_frame_sequence_submesh_flag |
        vr::ecs::geometry_mesh_morph_targets_flag;
    return HasGeometry3DTemporalVertexDeform(component_) &&
           (component_.mesh.flags & k_direct_world_disallowed_flags) == 0U;
}

[[nodiscard]] inline bool IsGeometry3DTemporalSkinnedEligible(
    const ecs::Geometry<ecs::Dim3>& component_,
    const std::uint32_t current_component_index_,
    const std::uint32_t previous_component_index_,
    const Geometry3DTemporalSkeletalPaletteView& current_palette_view_,
    const Geometry3DTemporalSkeletalPaletteView& previous_palette_view_)
    noexcept {
    return !HasGeometry3DTemporalUnsupportedDeformation(component_) &&
           (component_.mesh.flags &
            vr::ecs::geometry_mesh_skeletal_skinning_flag) != 0U &&
           AreGeometry3DTemporalSkeletalComponentsCompatible(
               current_palette_view_,
               previous_palette_view_,
               current_component_index_,
               previous_component_index_);
}

inline Geometry3DTemporalMotionBuildStats BuildGeometry3DTemporalMotionInstances(
    const ecs::Geometry<ecs::Dim3>* components_,
    const std::uint32_t component_count_,
    const GeometryTemporalMotionMcVector<ecs::Geometry3DGpuInstance>&
        current_instances_,
    const Geometry3DTemporalRuntimeWorldHistory&
        previous_runtime_world_history_,
    const Geometry3DTemporalSkeletalPaletteView& current_skeletal_palette_view_,
    const Geometry3DTemporalSkeletalPaletteHistory&
        previous_skeletal_palette_history_,
    const Geometry3DTemporalVertexDeformHistory&
        previous_vertex_deform_history_,
    GeometryTemporalMotionMcVector<Geometry3DTemporalMotionInstance>&
        out_instances_) {
    out_instances_.resize(current_instances_.size());
    Geometry3DTemporalMotionBuildStats stats{};
    const auto previous_skeletal_palette_view =
        MakeGeometry3DTemporalSkeletalPaletteView(
            previous_skeletal_palette_history_);
    const bool has_previous_runtime_world_history =
        HasGeometry3DTemporalRuntimeWorldHistoryData(
            previous_runtime_world_history_);
    const bool has_aligned_previous_skinned_handoff =
        has_previous_runtime_world_history &&
        AreGeometry3DTemporalPreviousSkinnedHistoriesAligned(
            previous_runtime_world_history_,
            previous_skeletal_palette_history_);
    const bool has_aligned_previous_vertex_deform_handoff =
        has_previous_runtime_world_history &&
        AreGeometry3DTemporalPreviousVertexDeformHistoriesAligned(
            previous_runtime_world_history_,
            previous_vertex_deform_history_);

    for (std::size_t index = 0U; index < current_instances_.size(); ++index) {
        const ecs::Geometry3DGpuInstance& current_instance =
            current_instances_[index];
        Geometry3DTemporalMotionInstance temporal_instance{};
        const ecs::Matrix4x4 current_world =
            ReadGeometry3DInstanceWorldMatrix(current_instance);
        ecs::Matrix4x4 previous_world = current_world;
        const Geometry3DTemporalVertexDeformParams current_vertex_deform =
            ReadGeometry3DInstanceVertexDeformParams(current_instance);
        Geometry3DTemporalVertexDeformParams previous_vertex_deform =
            current_vertex_deform;

        const std::uint32_t component_index = current_instance.component_index;
        WriteGeometry3DTemporalPreviousSkeletalComponentIndex(temporal_instance,
                                                              component_index);
        bool previous_valid = false;
        if (components_ != nullptr && component_index < component_count_) {
            const ecs::Geometry<ecs::Dim3>& component =
                components_[component_index];
            const Geometry3DTemporalPreviousObjectMatch previous_object_match =
                has_previous_runtime_world_history
                    ? ResolveGeometry3DTemporalPreviousObjectMatch(
                          component,
                          component_index,
                          previous_runtime_world_history_)
                    : Geometry3DTemporalPreviousObjectMatch{};
            const bool requires_previous_vertex_deform =
                HasGeometry3DTemporalVertexDeform(component);
            bool previous_vertex_deform_valid =
                !requires_previous_vertex_deform;
            if (requires_previous_vertex_deform &&
                previous_object_match.available &&
                has_aligned_previous_vertex_deform_handoff) {
                previous_vertex_deform =
                    ReadGeometry3DTemporalPreviousVertexDeformParams(
                        previous_vertex_deform_history_,
                        previous_object_match.previous_component_index);
                previous_vertex_deform_valid =
                    previous_vertex_deform.available;
            }
            if (IsGeometry3DTemporalRigidEligible(component)) {
                ++stats.rigid_candidate_count;
                if (previous_object_match.available) {
                    previous_world = previous_runtime_world_history_
                                         .component_world_matrices
                                             [previous_object_match
                                                  .previous_component_index];
                    previous_valid = previous_vertex_deform_valid;
                }
            } else if (IsGeometry3DTemporalVertexDeformDirectEligible(
                           component)) {
                if (previous_object_match.available) {
                    previous_world = previous_runtime_world_history_
                                         .component_world_matrices
                                             [previous_object_match
                                                  .previous_component_index];
                    previous_valid = previous_vertex_deform_valid;
                }
            } else if (IsGeometry3DTemporalSkinnedEligible(
                           component,
                           component_index,
                           previous_object_match.previous_component_index,
                           current_skeletal_palette_view_,
                           previous_skeletal_palette_view) &&
                       has_aligned_previous_skinned_handoff &&
                       previous_object_match.available) {
                previous_world = previous_runtime_world_history_
                                     .component_world_matrices
                                         [previous_object_match
                                              .previous_component_index];
                WriteGeometry3DTemporalPreviousSkeletalComponentIndex(
                    temporal_instance,
                    previous_object_match.previous_component_index);
                previous_valid = previous_vertex_deform_valid;
            }

            if (HasGeometry3DTemporalUnsupportedDeformation(component)) {
                previous_valid = false;
            }

            if (previous_valid) {
                ++stats.previous_match_count;
            }
        }

        if (!previous_valid) {
            ++stats.fallback_count;
            previous_world = current_world;
            previous_vertex_deform = current_vertex_deform;
            WriteGeometry3DTemporalPreviousSkeletalComponentIndex(
                temporal_instance,
                component_index);
        }

        WriteGeometry3DTemporalMotionWorldMatrix(temporal_instance,
                                                 previous_world);
        WriteGeometry3DTemporalMotionPreviousVertexDeformParams(
            temporal_instance,
            previous_vertex_deform);
        temporal_instance.confidence_seed = previous_valid ? 1.0F : 0.0F;
        temporal_instance.flags =
            previous_valid ? geometry_3d_temporal_motion_flag_previous_valid
                           : 0U;
        out_instances_[index] = temporal_instance;
    }

    return stats;
}

} // namespace vr::geometry
