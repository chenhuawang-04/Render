#pragma once

#include "vr/ecs/component/appearance_component.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/surface_component.hpp"
#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::ecs {

template<DimensionTag DimensionT>
struct SkeletalJointPose;

template<>
struct SkeletalJointPose<Dim2> final {
    Float2 position;
    float rotation_radians;
    Float2 scale;
};

template<>
struct SkeletalJointPose<Dim3> final {
    Float3 position;
    Quaternion rotation;
    Float3 scale;
};

template<typename T>
struct AnimationTargetSpan final {
    T* components = nullptr;
    std::uint32_t count = 0U;

    [[nodiscard]] T* Resolve(std::uint32_t index_) const noexcept {
        if (components == nullptr || index_ >= count) {
            return nullptr;
        }
        return components + index_;
    }
};

template<DimensionTag DimensionT>
struct SkeletalPoseOutputState final {
    SkeletalJointPose<DimensionT>* joints;
    std::uint32_t joint_count;
    std::uint32_t sampled_joint_count;
    std::uint32_t revision;
    const SkeletalJointPose<DimensionT>* bind_pose_joints;
    std::uint32_t bind_pose_joint_count;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
};

struct MorphWeightOutputState final {
    float* weights;
    std::uint32_t weight_count;
    std::uint32_t sampled_weight_count;
    std::uint32_t revision;
};

struct VertexDeformOutputState final {
    Float4* parameters;
    std::uint32_t parameter_count;
    std::uint32_t sampled_parameter_count;
    std::uint32_t revision;
};

struct FrameSequenceOutputState final {
    std::uint32_t frame_index_a;
    std::uint32_t frame_index_b;
    std::uint32_t frame_count;
    std::uint32_t revision;
    float blend_alpha;
    float normalized_time;
    float frame_position;
    std::uint32_t reserved0;
};

template<DimensionTag DimensionT>
struct AnimationEvaluationContext final {
    AnimationTargetSpan<Transform<DimensionT>> transforms{};
    AnimationTargetSpan<Camera<DimensionT>> cameras{};
    AnimationTargetSpan<Surface<DimensionT>> surfaces{};
    AnimationTargetSpan<Text<DimensionT>> texts{};
    AnimationTargetSpan<Appearance<DimensionT>> appearances{};
    AnimationTargetSpan<SkeletalPoseOutputState<DimensionT>> skeletal_outputs{};
    AnimationTargetSpan<MorphWeightOutputState> morph_outputs{};
    AnimationTargetSpan<VertexDeformOutputState> vertex_deform_outputs{};
    AnimationTargetSpan<FrameSequenceOutputState> frame_sequence_outputs{};
};

static_assert(std::is_standard_layout_v<SkeletalJointPose<Dim2>> &&
              std::is_trivial_v<SkeletalJointPose<Dim2>>);
static_assert(std::is_standard_layout_v<SkeletalJointPose<Dim3>> &&
              std::is_trivial_v<SkeletalJointPose<Dim3>>);
static_assert(std::is_standard_layout_v<SkeletalPoseOutputState<Dim2>> &&
              std::is_trivial_v<SkeletalPoseOutputState<Dim2>>);
static_assert(std::is_standard_layout_v<SkeletalPoseOutputState<Dim3>> &&
              std::is_trivial_v<SkeletalPoseOutputState<Dim3>>);
static_assert(std::is_standard_layout_v<MorphWeightOutputState> &&
              std::is_trivial_v<MorphWeightOutputState>);
static_assert(std::is_standard_layout_v<VertexDeformOutputState> &&
              std::is_trivial_v<VertexDeformOutputState>);
static_assert(std::is_standard_layout_v<FrameSequenceOutputState> &&
              std::is_trivial_v<FrameSequenceOutputState>);

} // namespace vr::ecs

