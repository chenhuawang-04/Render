#include "vr/animation/animation_skeletal_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::animation {
namespace {

constexpr std::uint32_t k_invalid_record_index = (std::numeric_limits<std::uint32_t>::max)();

template<typename T>
void ReserveIfNeeded(AnimationSkeletalMcVector<T>& storage_, std::uint32_t target_) {
    if (target_ > 0U && storage_.capacity() < static_cast<std::size_t>(target_)) {
        storage_.reserve(target_);
    }
}

template<typename T>
void InsertVectorValue(AnimationSkeletalMcVector<T>& storage_,
                       std::size_t index_,
                       const T& value_) {
    const std::size_t old_size = storage_.size();
    storage_.resize(old_size + 1U);
    for (std::size_t i = old_size; i > index_; --i) {
        storage_[i] = std::move(storage_[i - 1U]);
    }
    storage_[index_] = value_;
}

template<typename T>
void EraseVectorValue(AnimationSkeletalMcVector<T>& storage_,
                      std::size_t index_) noexcept {
    const std::size_t old_size = storage_.size();
    for (std::size_t i = index_; i + 1U < old_size; ++i) {
        storage_[i] = std::move(storage_[i + 1U]);
    }
    storage_.resize(old_size - 1U);
}

template<typename T>
void AppendSpan(AnimationSkeletalMcVector<T>& storage_,
                const T* values_,
                std::uint32_t count_) {
    if (values_ == nullptr || count_ == 0U) {
        return;
    }
    const std::size_t old_size = storage_.size();
    storage_.resize(old_size + static_cast<std::size_t>(count_));
    for (std::uint32_t i = 0U; i < count_; ++i) {
        storage_[old_size + static_cast<std::size_t>(i)] = values_[i];
    }
}

template<typename T>
vr::ecs::AnimationCurveView<T> BuildView(const AnimationSkeletalMcVector<vr::ecs::AnimationKeyframe<T>>& storage_,
                                         std::uint32_t begin_,
                                         std::uint32_t count_) noexcept {
    if (count_ == 0U || begin_ >= storage_.size()) {
        return vr::ecs::AnimationCurveView<T>{
            .keyframes = nullptr,
            .keyframe_count = 0U,
        };
    }
    return vr::ecs::AnimationCurveView<T>{
        .keyframes = storage_.data() + begin_,
        .keyframe_count = count_,
    };
}

template<typename T>
void AppendCurve(AnimationSkeletalMcVector<vr::ecs::AnimationKeyframe<T>>& storage_,
                 const vr::ecs::AnimationCurveView<T>& curve_,
                 std::uint32_t& begin_,
                 std::uint32_t& count_) {
    begin_ = static_cast<std::uint32_t>(storage_.size());
    count_ = curve_.keyframe_count;
    if (curve_.keyframes == nullptr || curve_.keyframe_count == 0U) {
        return;
    }
    const std::size_t old_size = storage_.size();
    storage_.resize(old_size + static_cast<std::size_t>(curve_.keyframe_count));
    for (std::uint32_t i = 0U; i < curve_.keyframe_count; ++i) {
        storage_[old_size + static_cast<std::size_t>(i)] = curve_.keyframes[i];
    }
}

} // namespace

void SkeletalAnimationHost::Initialize(const SkeletalAnimationHostCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    clips.clear();
    lookup.clear();
    slots.clear();
    free_slot_indices.clear();
    base_pose_2d.clear();
    base_pose_3d.clear();
    tracks_2d.clear();
    tracks_3d.clear();
    position_keyframes_2d.clear();
    rotation_keyframes_2d.clear();
    scale_keyframes_2d.clear();
    position_keyframes_3d.clear();
    rotation_keyframes_3d.clear();
    scale_keyframes_3d.clear();

    ReserveIfNeeded(clips, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(lookup, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(slots, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(free_slot_indices, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(base_pose_2d, create_info_cache.reserve_joint_count);
    ReserveIfNeeded(base_pose_3d, create_info_cache.reserve_joint_count);
    ReserveIfNeeded(tracks_2d, create_info_cache.reserve_track_count);
    ReserveIfNeeded(tracks_3d, create_info_cache.reserve_track_count);
    ReserveIfNeeded(position_keyframes_2d, create_info_cache.reserve_keyframe_count);
    ReserveIfNeeded(rotation_keyframes_2d, create_info_cache.reserve_keyframe_count);
    ReserveIfNeeded(scale_keyframes_2d, create_info_cache.reserve_keyframe_count);
    ReserveIfNeeded(position_keyframes_3d, create_info_cache.reserve_keyframe_count);
    ReserveIfNeeded(rotation_keyframes_3d, create_info_cache.reserve_keyframe_count);
    ReserveIfNeeded(scale_keyframes_3d, create_info_cache.reserve_keyframe_count);

    initialized = true;
}

void SkeletalAnimationHost::Shutdown() noexcept {
    clips.clear();
    lookup.clear();
    slots.clear();
    free_slot_indices.clear();
    base_pose_2d.clear();
    base_pose_3d.clear();
    tracks_2d.clear();
    tracks_3d.clear();
    position_keyframes_2d.clear();
    rotation_keyframes_2d.clear();
    scale_keyframes_2d.clear();
    position_keyframes_3d.clear();
    rotation_keyframes_3d.clear();
    scale_keyframes_3d.clear();
    create_info_cache = {};
    stats = {};
    initialized = false;
}

vr::ecs::AnimationClipHandle SkeletalAnimationHost::UpsertClip(const SkeletalClipDesc<vr::ecs::Dim2>& desc_) {
    if (!initialized) {
        throw std::runtime_error("SkeletalAnimationHost::UpsertClip(2D) called before Initialize");
    }
    if (desc_.clip_id == 0U) {
        throw std::invalid_argument("SkeletalAnimationHost::UpsertClip(2D) clip_id must be non-zero");
    }

    const std::size_t lookup_index = LowerBoundLookupIndex(desc_.clip_id);
    const bool exists = lookup_index < lookup.size() && lookup[lookup_index].clip_id == desc_.clip_id;
    SkeletalClipRecord* record = nullptr;
    if (exists) {
        const std::uint32_t slot_index = lookup[lookup_index].slot_index;
        if (slot_index >= slots.size() || slots[slot_index].alive == 0U || slots[slot_index].record_index >= clips.size()) {
            throw std::runtime_error("SkeletalAnimationHost::UpsertClip(2D) invalid slot state");
        }
        record = &clips[slots[slot_index].record_index];
        if (record->dimension != SkeletalClipDimension::dim2) {
            throw std::invalid_argument("SkeletalAnimationHost::UpsertClip(2D) dimension mismatch");
        }
        ++record->revision;
        ++stats.updated_clip_count;
    } else {
        const vr::ecs::AnimationClipHandle handle = AllocateHandle();
        clips.push_back(SkeletalClipRecord{});
        record = &clips.back();
        record->clip_id = desc_.clip_id;
        record->handle = handle;
        record->dimension = SkeletalClipDimension::dim2;
        record->revision = 1U;

        ClipLookupEntry entry{};
        entry.clip_id = desc_.clip_id;
        entry.slot_index = handle.index;
        InsertVectorValue(lookup, lookup_index, entry);
        slots[handle.index].record_index = static_cast<std::uint32_t>(clips.size() - 1U);

        UpdateStatsByDimension(SkeletalClipDimension::dim2, +1);
        ++stats.added_clip_count;
    }

    record->duration_s = std::max(1e-6F, desc_.duration_s);
    record->base_pose_begin = static_cast<std::uint32_t>(base_pose_2d.size());
    record->joint_count = desc_.joint_count;
    AppendSpan(base_pose_2d, desc_.base_pose, desc_.joint_count);

    record->track_begin = static_cast<std::uint32_t>(tracks_2d.size());
    record->track_count = desc_.track_count;
    if (desc_.tracks != nullptr && desc_.track_count > 0U) {
        const std::size_t old_size = tracks_2d.size();
        tracks_2d.resize(old_size + static_cast<std::size_t>(desc_.track_count));
        for (std::uint32_t i = 0U; i < desc_.track_count; ++i) {
            SkeletalTrackRecord<vr::ecs::Dim2> track{};
            track.joint_index = desc_.tracks[i].joint_index;
            AppendCurve(position_keyframes_2d, desc_.tracks[i].position_curve, track.position_keyframe_begin, track.position_keyframe_count);
            AppendCurve(rotation_keyframes_2d, desc_.tracks[i].rotation_curve, track.rotation_keyframe_begin, track.rotation_keyframe_count);
            AppendCurve(scale_keyframes_2d, desc_.tracks[i].scale_curve, track.scale_keyframe_begin, track.scale_keyframe_count);
            tracks_2d[old_size + static_cast<std::size_t>(i)] = track;
        }
    }

    stats.clip_count = static_cast<std::uint32_t>(clips.size());
    ++stats.revision;
    return record->handle;
}

vr::ecs::AnimationClipHandle SkeletalAnimationHost::UpsertClip(const SkeletalClipDesc<vr::ecs::Dim3>& desc_) {
    if (!initialized) {
        throw std::runtime_error("SkeletalAnimationHost::UpsertClip(3D) called before Initialize");
    }
    if (desc_.clip_id == 0U) {
        throw std::invalid_argument("SkeletalAnimationHost::UpsertClip(3D) clip_id must be non-zero");
    }

    const std::size_t lookup_index = LowerBoundLookupIndex(desc_.clip_id);
    const bool exists = lookup_index < lookup.size() && lookup[lookup_index].clip_id == desc_.clip_id;
    SkeletalClipRecord* record = nullptr;
    if (exists) {
        const std::uint32_t slot_index = lookup[lookup_index].slot_index;
        if (slot_index >= slots.size() || slots[slot_index].alive == 0U || slots[slot_index].record_index >= clips.size()) {
            throw std::runtime_error("SkeletalAnimationHost::UpsertClip(3D) invalid slot state");
        }
        record = &clips[slots[slot_index].record_index];
        if (record->dimension != SkeletalClipDimension::dim3) {
            throw std::invalid_argument("SkeletalAnimationHost::UpsertClip(3D) dimension mismatch");
        }
        ++record->revision;
        ++stats.updated_clip_count;
    } else {
        const vr::ecs::AnimationClipHandle handle = AllocateHandle();
        clips.push_back(SkeletalClipRecord{});
        record = &clips.back();
        record->clip_id = desc_.clip_id;
        record->handle = handle;
        record->dimension = SkeletalClipDimension::dim3;
        record->revision = 1U;

        ClipLookupEntry entry{};
        entry.clip_id = desc_.clip_id;
        entry.slot_index = handle.index;
        InsertVectorValue(lookup, lookup_index, entry);
        slots[handle.index].record_index = static_cast<std::uint32_t>(clips.size() - 1U);

        UpdateStatsByDimension(SkeletalClipDimension::dim3, +1);
        ++stats.added_clip_count;
    }

    record->duration_s = std::max(1e-6F, desc_.duration_s);
    record->base_pose_begin = static_cast<std::uint32_t>(base_pose_3d.size());
    record->joint_count = desc_.joint_count;
    AppendSpan(base_pose_3d, desc_.base_pose, desc_.joint_count);

    record->track_begin = static_cast<std::uint32_t>(tracks_3d.size());
    record->track_count = desc_.track_count;
    if (desc_.tracks != nullptr && desc_.track_count > 0U) {
        const std::size_t old_size = tracks_3d.size();
        tracks_3d.resize(old_size + static_cast<std::size_t>(desc_.track_count));
        for (std::uint32_t i = 0U; i < desc_.track_count; ++i) {
            SkeletalTrackRecord<vr::ecs::Dim3> track{};
            track.joint_index = desc_.tracks[i].joint_index;
            AppendCurve(position_keyframes_3d, desc_.tracks[i].position_curve, track.position_keyframe_begin, track.position_keyframe_count);
            AppendCurve(rotation_keyframes_3d, desc_.tracks[i].rotation_curve, track.rotation_keyframe_begin, track.rotation_keyframe_count);
            AppendCurve(scale_keyframes_3d, desc_.tracks[i].scale_curve, track.scale_keyframe_begin, track.scale_keyframe_count);
            tracks_3d[old_size + static_cast<std::size_t>(i)] = track;
        }
    }

    stats.clip_count = static_cast<std::uint32_t>(clips.size());
    ++stats.revision;
    return record->handle;
}

bool SkeletalAnimationHost::RemoveClip(std::uint32_t clip_id_) noexcept {
    if (!initialized || clip_id_ == 0U) {
        return false;
    }
    const std::size_t lookup_index = LowerBoundLookupIndex(clip_id_);
    if (lookup_index >= lookup.size() || lookup[lookup_index].clip_id != clip_id_) {
        return false;
    }
    const std::uint32_t slot_index = lookup[lookup_index].slot_index;
    if (slot_index >= slots.size() || slots[slot_index].alive == 0U || slots[slot_index].record_index >= clips.size()) {
        return false;
    }

    const std::uint32_t record_index = slots[slot_index].record_index;
    UpdateStatsByDimension(clips[record_index].dimension, -1);

    const std::uint32_t last_index = static_cast<std::uint32_t>(clips.size() - 1U);
    if (record_index != last_index) {
        clips[record_index] = std::move(clips[last_index]);
        UpdateMovedRecordSlot(record_index);
    }
    clips.pop_back();

    slots[slot_index].alive = 0U;
    ++slots[slot_index].generation;
    slots[slot_index].record_index = k_invalid_record_index;
    free_slot_indices.push_back(slot_index);
    RemoveLookupIndex(lookup_index);

    ++stats.removed_clip_count;
    stats.clip_count = static_cast<std::uint32_t>(clips.size());
    ++stats.revision;
    return true;
}

const SkeletalClipRecord* SkeletalAnimationHost::FindClipById(std::uint32_t clip_id_) const noexcept {
    if (!initialized || clip_id_ == 0U) {
        return nullptr;
    }
    const std::size_t lookup_index = LowerBoundLookupIndex(clip_id_);
    if (lookup_index >= lookup.size() || lookup[lookup_index].clip_id != clip_id_) {
        return nullptr;
    }
    const std::uint32_t slot_index = lookup[lookup_index].slot_index;
    if (slot_index >= slots.size()) {
        return nullptr;
    }
    const ClipSlot& slot = slots[slot_index];
    if (slot.alive == 0U || slot.record_index >= clips.size()) {
        return nullptr;
    }
    return &clips[slot.record_index];
}

const SkeletalClipRecord* SkeletalAnimationHost::FindClipByHandle(vr::ecs::AnimationClipHandle handle_) const noexcept {
    if (!initialized || handle_.index == vr::ecs::invalid_animation_handle_index || handle_.index >= slots.size()) {
        return nullptr;
    }
    const ClipSlot& slot = slots[handle_.index];
    if (slot.alive == 0U || slot.generation != handle_.generation || slot.record_index >= clips.size()) {
        return nullptr;
    }
    return &clips[slot.record_index];
}

const SkeletalClipRecord* SkeletalAnimationHost::FindClip2DById(std::uint32_t clip_id_) const noexcept {
    const SkeletalClipRecord* record = FindClipById(clip_id_);
    return (record != nullptr && record->dimension == SkeletalClipDimension::dim2) ? record : nullptr;
}

const SkeletalClipRecord* SkeletalAnimationHost::FindClip2DByHandle(vr::ecs::AnimationClipHandle handle_) const noexcept {
    const SkeletalClipRecord* record = FindClipByHandle(handle_);
    return (record != nullptr && record->dimension == SkeletalClipDimension::dim2) ? record : nullptr;
}

const SkeletalClipRecord* SkeletalAnimationHost::FindClip3DById(std::uint32_t clip_id_) const noexcept {
    const SkeletalClipRecord* record = FindClipById(clip_id_);
    return (record != nullptr && record->dimension == SkeletalClipDimension::dim3) ? record : nullptr;
}

const SkeletalClipRecord* SkeletalAnimationHost::FindClip3DByHandle(vr::ecs::AnimationClipHandle handle_) const noexcept {
    const SkeletalClipRecord* record = FindClipByHandle(handle_);
    return (record != nullptr && record->dimension == SkeletalClipDimension::dim3) ? record : nullptr;
}

const vr::ecs::SkeletalJointPose<vr::ecs::Dim2>* SkeletalAnimationHost::BasePose2D(const SkeletalClipRecord& clip_) const noexcept {
    if (clip_.dimension != SkeletalClipDimension::dim2 || clip_.joint_count == 0U || clip_.base_pose_begin >= base_pose_2d.size()) {
        return nullptr;
    }
    return base_pose_2d.data() + clip_.base_pose_begin;
}

const vr::ecs::SkeletalJointPose<vr::ecs::Dim3>* SkeletalAnimationHost::BasePose3D(const SkeletalClipRecord& clip_) const noexcept {
    if (clip_.dimension != SkeletalClipDimension::dim3 || clip_.joint_count == 0U || clip_.base_pose_begin >= base_pose_3d.size()) {
        return nullptr;
    }
    return base_pose_3d.data() + clip_.base_pose_begin;
}

const SkeletalTrackRecord<vr::ecs::Dim2>* SkeletalAnimationHost::Tracks2D(const SkeletalClipRecord& clip_) const noexcept {
    if (clip_.dimension != SkeletalClipDimension::dim2 || clip_.track_count == 0U || clip_.track_begin >= tracks_2d.size()) {
        return nullptr;
    }
    return tracks_2d.data() + clip_.track_begin;
}

const SkeletalTrackRecord<vr::ecs::Dim3>* SkeletalAnimationHost::Tracks3D(const SkeletalClipRecord& clip_) const noexcept {
    if (clip_.dimension != SkeletalClipDimension::dim3 || clip_.track_count == 0U || clip_.track_begin >= tracks_3d.size()) {
        return nullptr;
    }
    return tracks_3d.data() + clip_.track_begin;
}

vr::ecs::AnimationCurveView<vr::ecs::Float2> SkeletalAnimationHost::PositionCurveView(const SkeletalTrackRecord<vr::ecs::Dim2>& track_) const noexcept {
    return BuildView(position_keyframes_2d, track_.position_keyframe_begin, track_.position_keyframe_count);
}

vr::ecs::AnimationCurveView<float> SkeletalAnimationHost::RotationCurveView(const SkeletalTrackRecord<vr::ecs::Dim2>& track_) const noexcept {
    return BuildView(rotation_keyframes_2d, track_.rotation_keyframe_begin, track_.rotation_keyframe_count);
}

vr::ecs::AnimationCurveView<vr::ecs::Float2> SkeletalAnimationHost::ScaleCurveView(const SkeletalTrackRecord<vr::ecs::Dim2>& track_) const noexcept {
    return BuildView(scale_keyframes_2d, track_.scale_keyframe_begin, track_.scale_keyframe_count);
}

vr::ecs::AnimationCurveView<vr::ecs::Float3> SkeletalAnimationHost::PositionCurveView(const SkeletalTrackRecord<vr::ecs::Dim3>& track_) const noexcept {
    return BuildView(position_keyframes_3d, track_.position_keyframe_begin, track_.position_keyframe_count);
}

vr::ecs::AnimationCurveView<vr::ecs::Quaternion> SkeletalAnimationHost::RotationCurveView(const SkeletalTrackRecord<vr::ecs::Dim3>& track_) const noexcept {
    return BuildView(rotation_keyframes_3d, track_.rotation_keyframe_begin, track_.rotation_keyframe_count);
}

vr::ecs::AnimationCurveView<vr::ecs::Float3> SkeletalAnimationHost::ScaleCurveView(const SkeletalTrackRecord<vr::ecs::Dim3>& track_) const noexcept {
    return BuildView(scale_keyframes_3d, track_.scale_keyframe_begin, track_.scale_keyframe_count);
}

bool SkeletalAnimationHost::IsInitialized() const noexcept {
    return initialized;
}

const SkeletalAnimationHostStats& SkeletalAnimationHost::Stats() const noexcept {
    return stats;
}

std::size_t SkeletalAnimationHost::LowerBoundLookupIndex(std::uint32_t clip_id_) const noexcept {
    std::size_t first = 0U;
    std::size_t count = lookup.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (lookup[it].clip_id < clip_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

vr::ecs::AnimationClipHandle SkeletalAnimationHost::AllocateHandle() {
    if (!free_slot_indices.empty()) {
        const std::uint32_t slot_index = free_slot_indices.back();
        free_slot_indices.pop_back();
        ClipSlot& slot = slots[slot_index];
        slot.alive = 1U;
        slot.record_index = k_invalid_record_index;
        return vr::ecs::AnimationClipHandle{
            .index = slot_index,
            .generation = slot.generation,
        };
    }

    const std::uint32_t slot_index = static_cast<std::uint32_t>(slots.size());
    ClipSlot slot{};
    slot.generation = 1U;
    slot.record_index = k_invalid_record_index;
    slot.alive = 1U;
    slots.push_back(slot);
    return vr::ecs::AnimationClipHandle{
        .index = slot_index,
        .generation = 1U,
    };
}

void SkeletalAnimationHost::UpdateStatsByDimension(SkeletalClipDimension dimension_, int delta_) noexcept {
    auto apply = [delta_](std::uint32_t& value_) noexcept {
        value_ = static_cast<std::uint32_t>(static_cast<int>(value_) + delta_);
    };
    switch (dimension_) {
        case SkeletalClipDimension::dim2:
            apply(stats.clip2d_count);
            break;
        case SkeletalClipDimension::dim3:
            apply(stats.clip3d_count);
            break;
        case SkeletalClipDimension::none:
        default:
            break;
    }
}

void SkeletalAnimationHost::RemoveLookupIndex(std::size_t lookup_index_) noexcept {
    EraseVectorValue(lookup, lookup_index_);
}

void SkeletalAnimationHost::UpdateMovedRecordSlot(std::uint32_t record_index_) noexcept {
    if (record_index_ >= clips.size()) {
        return;
    }
    const std::uint32_t slot_index = clips[record_index_].handle.index;
    if (slot_index < slots.size()) {
        slots[slot_index].record_index = record_index_;
    }
}

} // namespace vr::animation

