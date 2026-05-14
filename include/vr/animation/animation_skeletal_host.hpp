#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/system/animation_curve_system.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"

#include <cstdint>

namespace vr::animation {

template<typename T>
using AnimationSkeletalMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

template<vr::ecs::DimensionTag DimensionT>
struct SkeletalTrackDesc;

template<>
struct SkeletalTrackDesc<vr::ecs::Dim2> final {
    std::uint16_t joint_index = 0U;
    std::uint16_t reserved0 = 0U;
    vr::ecs::AnimationCurveView<vr::ecs::Float2> position_curve{.keyframes = nullptr, .keyframe_count = 0U};
    vr::ecs::AnimationCurveView<float> rotation_curve{.keyframes = nullptr, .keyframe_count = 0U};
    vr::ecs::AnimationCurveView<vr::ecs::Float2> scale_curve{.keyframes = nullptr, .keyframe_count = 0U};
};

template<>
struct SkeletalTrackDesc<vr::ecs::Dim3> final {
    std::uint16_t joint_index = 0U;
    std::uint16_t reserved0 = 0U;
    vr::ecs::AnimationCurveView<vr::ecs::Float3> position_curve{.keyframes = nullptr, .keyframe_count = 0U};
    vr::ecs::AnimationCurveView<vr::ecs::Quaternion> rotation_curve{.keyframes = nullptr, .keyframe_count = 0U};
    vr::ecs::AnimationCurveView<vr::ecs::Float3> scale_curve{.keyframes = nullptr, .keyframe_count = 0U};
};

template<vr::ecs::DimensionTag DimensionT>
struct SkeletalClipDesc final {
    std::uint32_t clip_id = 0U;
    float duration_s = 1.0F;
    const vr::ecs::SkeletalJointPose<DimensionT>* base_pose = nullptr;
    std::uint32_t joint_count = 0U;
    const SkeletalTrackDesc<DimensionT>* tracks = nullptr;
    std::uint32_t track_count = 0U;
};

struct SkeletalAnimationHostCreateInfo final {
    std::uint32_t reserve_clip_count = 128U;
    std::uint32_t reserve_joint_count = 2048U;
    std::uint32_t reserve_track_count = 2048U;
    std::uint32_t reserve_keyframe_count = 16384U;
};

struct SkeletalAnimationHostStats final {
    std::uint32_t clip_count = 0U;
    std::uint32_t clip2d_count = 0U;
    std::uint32_t clip3d_count = 0U;
    std::uint32_t added_clip_count = 0U;
    std::uint32_t updated_clip_count = 0U;
    std::uint32_t removed_clip_count = 0U;
    std::uint32_t revision = 0U;
};

enum class SkeletalClipDimension : std::uint8_t {
    none = 0U,
    dim2 = 1U,
    dim3 = 2U,
};

template<vr::ecs::DimensionTag DimensionT>
struct SkeletalTrackRecord;

template<>
struct SkeletalTrackRecord<vr::ecs::Dim2> final {
    std::uint16_t joint_index = 0U;
    std::uint16_t reserved0 = 0U;
    std::uint32_t position_keyframe_begin = 0U;
    std::uint32_t position_keyframe_count = 0U;
    std::uint32_t rotation_keyframe_begin = 0U;
    std::uint32_t rotation_keyframe_count = 0U;
    std::uint32_t scale_keyframe_begin = 0U;
    std::uint32_t scale_keyframe_count = 0U;
};

template<>
struct SkeletalTrackRecord<vr::ecs::Dim3> final {
    std::uint16_t joint_index = 0U;
    std::uint16_t reserved0 = 0U;
    std::uint32_t position_keyframe_begin = 0U;
    std::uint32_t position_keyframe_count = 0U;
    std::uint32_t rotation_keyframe_begin = 0U;
    std::uint32_t rotation_keyframe_count = 0U;
    std::uint32_t scale_keyframe_begin = 0U;
    std::uint32_t scale_keyframe_count = 0U;
};

struct SkeletalClipRecord final {
    std::uint32_t clip_id = 0U;
    vr::ecs::AnimationClipHandle handle = vr::ecs::invalid_animation_clip_handle;
    SkeletalClipDimension dimension = SkeletalClipDimension::none;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
    float duration_s = 1.0F;
    std::uint32_t revision = 0U;
    std::uint32_t base_pose_begin = 0U;
    std::uint32_t joint_count = 0U;
    std::uint32_t track_begin = 0U;
    std::uint32_t track_count = 0U;
};

class SkeletalAnimationHost final {
public:
    SkeletalAnimationHost() = default;
    ~SkeletalAnimationHost() = default;

    SkeletalAnimationHost(const SkeletalAnimationHost&) = delete;
    SkeletalAnimationHost& operator=(const SkeletalAnimationHost&) = delete;
    SkeletalAnimationHost(SkeletalAnimationHost&&) = delete;
    SkeletalAnimationHost& operator=(SkeletalAnimationHost&&) = delete;

    void Initialize(const SkeletalAnimationHostCreateInfo& create_info_ = {});
    void Shutdown() noexcept;

    vr::ecs::AnimationClipHandle UpsertClip(const SkeletalClipDesc<vr::ecs::Dim2>& desc_);
    vr::ecs::AnimationClipHandle UpsertClip(const SkeletalClipDesc<vr::ecs::Dim3>& desc_);

    [[nodiscard]] bool RemoveClip(std::uint32_t clip_id_) noexcept;

    [[nodiscard]] const SkeletalClipRecord* FindClipById(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] const SkeletalClipRecord* FindClipByHandle(vr::ecs::AnimationClipHandle handle_) const noexcept;
    [[nodiscard]] const SkeletalClipRecord* FindClip2DById(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] const SkeletalClipRecord* FindClip2DByHandle(vr::ecs::AnimationClipHandle handle_) const noexcept;
    [[nodiscard]] const SkeletalClipRecord* FindClip3DById(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] const SkeletalClipRecord* FindClip3DByHandle(vr::ecs::AnimationClipHandle handle_) const noexcept;

    [[nodiscard]] const vr::ecs::SkeletalJointPose<vr::ecs::Dim2>* BasePose2D(const SkeletalClipRecord& clip_) const noexcept;
    [[nodiscard]] const vr::ecs::SkeletalJointPose<vr::ecs::Dim3>* BasePose3D(const SkeletalClipRecord& clip_) const noexcept;
    [[nodiscard]] const SkeletalTrackRecord<vr::ecs::Dim2>* Tracks2D(const SkeletalClipRecord& clip_) const noexcept;
    [[nodiscard]] const SkeletalTrackRecord<vr::ecs::Dim3>* Tracks3D(const SkeletalClipRecord& clip_) const noexcept;

    [[nodiscard]] vr::ecs::AnimationCurveView<vr::ecs::Float2> PositionCurveView(const SkeletalTrackRecord<vr::ecs::Dim2>& track_) const noexcept;
    [[nodiscard]] vr::ecs::AnimationCurveView<float> RotationCurveView(const SkeletalTrackRecord<vr::ecs::Dim2>& track_) const noexcept;
    [[nodiscard]] vr::ecs::AnimationCurveView<vr::ecs::Float2> ScaleCurveView(const SkeletalTrackRecord<vr::ecs::Dim2>& track_) const noexcept;

    [[nodiscard]] vr::ecs::AnimationCurveView<vr::ecs::Float3> PositionCurveView(const SkeletalTrackRecord<vr::ecs::Dim3>& track_) const noexcept;
    [[nodiscard]] vr::ecs::AnimationCurveView<vr::ecs::Quaternion> RotationCurveView(const SkeletalTrackRecord<vr::ecs::Dim3>& track_) const noexcept;
    [[nodiscard]] vr::ecs::AnimationCurveView<vr::ecs::Float3> ScaleCurveView(const SkeletalTrackRecord<vr::ecs::Dim3>& track_) const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const SkeletalAnimationHostStats& Stats() const noexcept;

private:
    struct ClipLookupEntry final {
        std::uint32_t clip_id = 0U;
        std::uint32_t slot_index = 0U;
    };

    struct ClipSlot final {
        std::uint32_t generation = 1U;
        std::uint32_t record_index = 0U;
        std::uint8_t alive = 0U;
        std::uint8_t reserved0 = 0U;
        std::uint16_t reserved1 = 0U;
    };

    [[nodiscard]] std::size_t LowerBoundLookupIndex(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] vr::ecs::AnimationClipHandle AllocateHandle();
    void UpdateStatsByDimension(SkeletalClipDimension dimension_, int delta_) noexcept;
    void RemoveLookupIndex(std::size_t lookup_index_) noexcept;
    void UpdateMovedRecordSlot(std::uint32_t record_index_) noexcept;

private:
    SkeletalAnimationHostCreateInfo create_info_cache{};
    SkeletalAnimationHostStats stats{};

    AnimationSkeletalMcVector<SkeletalClipRecord> clips{};
    AnimationSkeletalMcVector<ClipLookupEntry> lookup{};
    AnimationSkeletalMcVector<ClipSlot> slots{};
    AnimationSkeletalMcVector<std::uint32_t> free_slot_indices{};

    AnimationSkeletalMcVector<vr::ecs::SkeletalJointPose<vr::ecs::Dim2>> base_pose_2d{};
    AnimationSkeletalMcVector<vr::ecs::SkeletalJointPose<vr::ecs::Dim3>> base_pose_3d{};
    AnimationSkeletalMcVector<SkeletalTrackRecord<vr::ecs::Dim2>> tracks_2d{};
    AnimationSkeletalMcVector<SkeletalTrackRecord<vr::ecs::Dim3>> tracks_3d{};
    AnimationSkeletalMcVector<vr::ecs::AnimationKeyframe<vr::ecs::Float2>> position_keyframes_2d{};
    AnimationSkeletalMcVector<vr::ecs::AnimationKeyframe<float>> rotation_keyframes_2d{};
    AnimationSkeletalMcVector<vr::ecs::AnimationKeyframe<vr::ecs::Float2>> scale_keyframes_2d{};
    AnimationSkeletalMcVector<vr::ecs::AnimationKeyframe<vr::ecs::Float3>> position_keyframes_3d{};
    AnimationSkeletalMcVector<vr::ecs::AnimationKeyframe<vr::ecs::Quaternion>> rotation_keyframes_3d{};
    AnimationSkeletalMcVector<vr::ecs::AnimationKeyframe<vr::ecs::Float3>> scale_keyframes_3d{};

    bool initialized = false;
};

} // namespace vr::animation

