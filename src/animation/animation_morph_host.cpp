#include "vr/animation/animation_morph_host.hpp"

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
void ReserveIfNeeded(AnimationMorphMcVector<T>& storage_, std::uint32_t target_) {
    if (target_ > 0U && storage_.capacity() < static_cast<std::size_t>(target_)) {
        storage_.reserve(target_);
    }
}

template<typename T>
void InsertVectorValue(AnimationMorphMcVector<T>& storage_,
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
void EraseVectorValue(AnimationMorphMcVector<T>& storage_,
                      std::size_t index_) noexcept {
    const std::size_t old_size = storage_.size();
    for (std::size_t i = index_; i + 1U < old_size; ++i) {
        storage_[i] = std::move(storage_[i + 1U]);
    }
    storage_.resize(old_size - 1U);
}

template<typename T>
void AppendSpan(AnimationMorphMcVector<T>& storage_,
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

} // namespace

void MorphAnimationHost::Initialize(const MorphAnimationHostCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    clips.clear();
    lookup.clear();
    slots.clear();
    free_slot_indices.clear();
    base_weights.clear();
    tracks.clear();
    weight_keyframes.clear();

    ReserveIfNeeded(clips, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(lookup, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(slots, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(free_slot_indices, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(base_weights, create_info_cache.reserve_weight_count);
    ReserveIfNeeded(tracks, create_info_cache.reserve_track_count);
    ReserveIfNeeded(weight_keyframes, create_info_cache.reserve_keyframe_count);

    initialized = true;
}

void MorphAnimationHost::Shutdown() noexcept {
    clips.clear();
    lookup.clear();
    slots.clear();
    free_slot_indices.clear();
    base_weights.clear();
    tracks.clear();
    weight_keyframes.clear();
    create_info_cache = {};
    stats = {};
    initialized = false;
}

vr::ecs::AnimationClipHandle MorphAnimationHost::UpsertClip(const MorphClipDesc& desc_) {
    if (!initialized) {
        throw std::runtime_error("MorphAnimationHost::UpsertClip called before Initialize");
    }
    if (desc_.clip_id == 0U) {
        throw std::invalid_argument("MorphAnimationHost::UpsertClip clip_id must be non-zero");
    }

    const std::size_t lookup_index = LowerBoundLookupIndex(desc_.clip_id);
    const bool exists = lookup_index < lookup.size() && lookup[lookup_index].clip_id == desc_.clip_id;
    MorphClipRecord* record = nullptr;
    if (exists) {
        const std::uint32_t slot_index = lookup[lookup_index].slot_index;
        if (slot_index >= slots.size() || slots[slot_index].alive == 0U || slots[slot_index].record_index >= clips.size()) {
            throw std::runtime_error("MorphAnimationHost::UpsertClip invalid slot state");
        }
        record = &clips[slots[slot_index].record_index];
        ++record->revision;
        ++stats.updated_clip_count;
    } else {
        const vr::ecs::AnimationClipHandle handle = AllocateHandle();
        clips.push_back(MorphClipRecord{});
        record = &clips.back();
        record->clip_id = desc_.clip_id;
        record->handle = handle;
        record->revision = 1U;

        ClipLookupEntry entry{};
        entry.clip_id = desc_.clip_id;
        entry.slot_index = handle.index;
        InsertVectorValue(lookup, lookup_index, entry);
        slots[handle.index].record_index = static_cast<std::uint32_t>(clips.size() - 1U);
        ++stats.added_clip_count;
    }

    record->duration_s = std::max(1e-6F, desc_.duration_s);
    record->base_weight_begin = static_cast<std::uint32_t>(base_weights.size());
    record->weight_count = desc_.weight_count;
    if (desc_.base_weights != nullptr && desc_.weight_count > 0U) {
        AppendSpan(base_weights, desc_.base_weights, desc_.weight_count);
    } else if (desc_.weight_count > 0U) {
        const std::size_t old_size = base_weights.size();
        base_weights.resize(old_size + static_cast<std::size_t>(desc_.weight_count));
        for (std::uint32_t i = 0U; i < desc_.weight_count; ++i) {
            base_weights[old_size + static_cast<std::size_t>(i)] = 0.0F;
        }
    }

    record->track_begin = static_cast<std::uint32_t>(tracks.size());
    record->track_count = desc_.track_count;
    if (desc_.tracks != nullptr && desc_.track_count > 0U) {
        const std::size_t old_size = tracks.size();
        tracks.resize(old_size + static_cast<std::size_t>(desc_.track_count));
        for (std::uint32_t i = 0U; i < desc_.track_count; ++i) {
            MorphTrackRecord track{};
            track.target_index = desc_.tracks[i].target_index;
            track.keyframe_begin = static_cast<std::uint32_t>(weight_keyframes.size());
            track.keyframe_count = desc_.tracks[i].weight_curve.keyframe_count;
            if (desc_.tracks[i].weight_curve.keyframes != nullptr && desc_.tracks[i].weight_curve.keyframe_count > 0U) {
                const std::size_t key_old_size = weight_keyframes.size();
                weight_keyframes.resize(key_old_size + static_cast<std::size_t>(desc_.tracks[i].weight_curve.keyframe_count));
                for (std::uint32_t key_index = 0U; key_index < desc_.tracks[i].weight_curve.keyframe_count; ++key_index) {
                    weight_keyframes[key_old_size + static_cast<std::size_t>(key_index)] =
                        desc_.tracks[i].weight_curve.keyframes[key_index];
                }
            }
            tracks[old_size + static_cast<std::size_t>(i)] = track;
        }
    }

    stats.clip_count = static_cast<std::uint32_t>(clips.size());
    ++stats.revision;
    return record->handle;
}

bool MorphAnimationHost::RemoveClip(std::uint32_t clip_id_) noexcept {
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

const MorphClipRecord* MorphAnimationHost::FindClipById(std::uint32_t clip_id_) const noexcept {
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

const MorphClipRecord* MorphAnimationHost::FindClipByHandle(vr::ecs::AnimationClipHandle handle_) const noexcept {
    if (!initialized || handle_.index == vr::ecs::invalid_animation_handle_index || handle_.index >= slots.size()) {
        return nullptr;
    }
    const ClipSlot& slot = slots[handle_.index];
    if (slot.alive == 0U || slot.generation != handle_.generation || slot.record_index >= clips.size()) {
        return nullptr;
    }
    return &clips[slot.record_index];
}

const float* MorphAnimationHost::BaseWeights(const MorphClipRecord& clip_) const noexcept {
    if (clip_.weight_count == 0U || clip_.base_weight_begin >= base_weights.size()) {
        return nullptr;
    }
    return base_weights.data() + clip_.base_weight_begin;
}

const MorphTrackRecord* MorphAnimationHost::Tracks(const MorphClipRecord& clip_) const noexcept {
    if (clip_.track_count == 0U || clip_.track_begin >= tracks.size()) {
        return nullptr;
    }
    return tracks.data() + clip_.track_begin;
}

vr::ecs::AnimationCurveView<float> MorphAnimationHost::WeightCurveView(const MorphTrackRecord& track_) const noexcept {
    if (track_.keyframe_count == 0U || track_.keyframe_begin >= weight_keyframes.size()) {
        return vr::ecs::AnimationCurveView<float>{.keyframes = nullptr, .keyframe_count = 0U};
    }
    return vr::ecs::AnimationCurveView<float>{
        .keyframes = weight_keyframes.data() + track_.keyframe_begin,
        .keyframe_count = track_.keyframe_count,
    };
}

bool MorphAnimationHost::IsInitialized() const noexcept {
    return initialized;
}

const MorphAnimationHostStats& MorphAnimationHost::Stats() const noexcept {
    return stats;
}

std::size_t MorphAnimationHost::LowerBoundLookupIndex(std::uint32_t clip_id_) const noexcept {
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

vr::ecs::AnimationClipHandle MorphAnimationHost::AllocateHandle() {
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

void MorphAnimationHost::RemoveLookupIndex(std::size_t lookup_index_) noexcept {
    EraseVectorValue(lookup, lookup_index_);
}

void MorphAnimationHost::UpdateMovedRecordSlot(std::uint32_t record_index_) noexcept {
    if (record_index_ >= clips.size()) {
        return;
    }
    const std::uint32_t slot_index = clips[record_index_].handle.index;
    if (slot_index < slots.size()) {
        slots[slot_index].record_index = record_index_;
    }
}

} // namespace vr::animation
