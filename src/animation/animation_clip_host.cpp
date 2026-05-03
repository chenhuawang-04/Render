#include "vr/animation/animation_clip_host.hpp"

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
void ReserveIfNeeded(AnimationMcVector<T>& storage_, std::uint32_t target_) {
    if (target_ > 0U && storage_.capacity() < static_cast<std::size_t>(target_)) {
        storage_.reserve(target_);
    }
}

template<typename DescT, typename RecordT, typename KeyframeT, typename SemanticT>
AnimationChannelRange AppendChannels(AnimationMcVector<RecordT>& channel_storage_,
                                     AnimationMcVector<ecs::AnimationKeyframe<KeyframeT>>& keyframe_storage_,
                                     const DescT* descs_,
                                     std::uint32_t desc_count_) {
    AnimationChannelRange range{};
    if (descs_ == nullptr || desc_count_ == 0U) {
        return range;
    }

    range.begin = static_cast<std::uint32_t>(channel_storage_.size());
    range.count = desc_count_;
    channel_storage_.resize(channel_storage_.size() + static_cast<std::size_t>(desc_count_));

    for (std::uint32_t i = 0U; i < desc_count_; ++i) {
        const DescT& desc = descs_[i];
        RecordT record{};
        record.semantic = static_cast<SemanticT>(desc.semantic);
        record.channel_mask = desc.channel_mask;
        record.reserved0 = 0U;
        record.keyframe_begin = static_cast<std::uint32_t>(keyframe_storage_.size());
        record.keyframe_count = desc.curve.keyframe_count;

        if (desc.curve.keyframes != nullptr && desc.curve.keyframe_count > 0U) {
            const std::size_t old_size = keyframe_storage_.size();
            keyframe_storage_.resize(old_size + static_cast<std::size_t>(desc.curve.keyframe_count));
            for (std::uint32_t key_index = 0U; key_index < desc.curve.keyframe_count; ++key_index) {
                keyframe_storage_[old_size + static_cast<std::size_t>(key_index)] = desc.curve.keyframes[key_index];
            }
        }

        channel_storage_[range.begin + i] = record;
    }

    return range;
}

template<typename RecordT>
const RecordT* ChannelsOrNull(const AnimationMcVector<RecordT>& storage_,
                              AnimationChannelRange range_) noexcept {
    if (range_.count == 0U || range_.begin >= storage_.size()) {
        return nullptr;
    }
    return storage_.data() + range_.begin;
}

template<typename KeyframeT>
ecs::AnimationCurveView<KeyframeT> BuildView(const AnimationMcVector<ecs::AnimationKeyframe<KeyframeT>>& storage_,
                                             std::uint32_t begin_,
                                             std::uint32_t count_) noexcept {
    if (count_ == 0U || begin_ >= storage_.size()) {
        return ecs::AnimationCurveView<KeyframeT>{
            .keyframes = nullptr,
            .keyframe_count = 0U,
        };
    }
    return ecs::AnimationCurveView<KeyframeT>{
        .keyframes = storage_.data() + begin_,
        .keyframe_count = count_,
    };
}

template<typename T>
void InsertVectorValue(AnimationMcVector<T>& storage_,
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
void EraseVectorValue(AnimationMcVector<T>& storage_,
                      std::size_t index_) noexcept {
    const std::size_t old_size = storage_.size();
    for (std::size_t i = index_; i + 1U < old_size; ++i) {
        storage_[i] = std::move(storage_[i + 1U]);
    }
    storage_.resize(old_size - 1U);
}

} // namespace

void AnimationClipHost::Initialize(const AnimationClipHostCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    clips.clear();
    lookup.clear();
    slots.clear();
    free_slot_indices.clear();
    property_scalar_channels.clear();
    property_float2_channels.clear();
    property_float3_channels.clear();
    property_float4_channels.clear();
    property_quaternion_channels.clear();
    property_color_channels.clear();
    material_scalar_channels.clear();
    material_float4_channels.clear();
    material_color_channels.clear();
    camera_scalar_channels.clear();
    camera_float2_channels.clear();
    camera_float3_channels.clear();
    camera_quaternion_channels.clear();
    scalar_keyframes.clear();
    float2_keyframes.clear();
    float3_keyframes.clear();
    float4_keyframes.clear();
    quaternion_keyframes.clear();
    color_keyframes.clear();

    ReserveIfNeeded(clips, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(lookup, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(slots, create_info_cache.reserve_clip_count);
    ReserveIfNeeded(free_slot_indices, create_info_cache.reserve_clip_count);

    ReserveIfNeeded(property_scalar_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(property_float2_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(property_float3_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(property_float4_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(property_quaternion_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(property_color_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(material_scalar_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(material_float4_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(material_color_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(camera_scalar_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(camera_float2_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(camera_float3_channels, create_info_cache.reserve_channel_count);
    ReserveIfNeeded(camera_quaternion_channels, create_info_cache.reserve_channel_count);

    ReserveIfNeeded(scalar_keyframes, create_info_cache.reserve_keyframe_count);
    ReserveIfNeeded(float2_keyframes, create_info_cache.reserve_keyframe_count);
    ReserveIfNeeded(float3_keyframes, create_info_cache.reserve_keyframe_count);
    ReserveIfNeeded(float4_keyframes, create_info_cache.reserve_keyframe_count);
    ReserveIfNeeded(quaternion_keyframes, create_info_cache.reserve_keyframe_count);
    ReserveIfNeeded(color_keyframes, create_info_cache.reserve_keyframe_count);

    initialized = true;
}

void AnimationClipHost::Shutdown() noexcept {
    clips.clear();
    lookup.clear();
    slots.clear();
    free_slot_indices.clear();
    property_scalar_channels.clear();
    property_float2_channels.clear();
    property_float3_channels.clear();
    property_float4_channels.clear();
    property_quaternion_channels.clear();
    property_color_channels.clear();
    material_scalar_channels.clear();
    material_float4_channels.clear();
    material_color_channels.clear();
    camera_scalar_channels.clear();
    camera_float2_channels.clear();
    camera_float3_channels.clear();
    camera_quaternion_channels.clear();
    scalar_keyframes.clear();
    float2_keyframes.clear();
    float3_keyframes.clear();
    float4_keyframes.clear();
    quaternion_keyframes.clear();
    color_keyframes.clear();
    create_info_cache = {};
    stats = {};
    initialized = false;
}

ecs::AnimationClipHandle AnimationClipHost::UpsertPropertyClip(const PropertyAnimationClipDesc& desc_) {
    if (!initialized) {
        throw std::runtime_error("AnimationClipHost::UpsertPropertyClip called before Initialize");
    }
    if (desc_.clip_id == 0U) {
        throw std::invalid_argument("AnimationClipHost::UpsertPropertyClip clip_id must be non-zero");
    }

    AnimationClipRecord* record = FindMutableClipById(desc_.clip_id);
    const bool exists = record != nullptr;
    if (exists && record->kind != AnimationClipKind::property_track) {
        throw std::invalid_argument("AnimationClipHost::UpsertPropertyClip clip kind mismatch");
    }

    if (!exists) {
        const ecs::AnimationClipHandle handle = AllocateHandle();
        clips.push_back(AnimationClipRecord{});
        record = &clips.back();
        record->clip_id = desc_.clip_id;
        record->handle = handle;
        record->kind = AnimationClipKind::property_track;
        record->revision = 1U;

        ClipLookupEntry lookup_entry{};
        lookup_entry.clip_id = desc_.clip_id;
        lookup_entry.slot_index = handle.index;
        const std::size_t lookup_index = LowerBoundLookupIndex(desc_.clip_id);
        InsertVectorValue(lookup, lookup_index, lookup_entry);

        slots[handle.index].record_index = static_cast<std::uint32_t>(clips.size() - 1U);
        UpdateStatsByKind(AnimationClipKind::property_track, +1);
        ++stats.added_clip_count;
    } else {
        ++record->revision;
        ++stats.updated_clip_count;
    }

    record->duration_s = std::max(1e-6F, desc_.duration_s);
    record->property.scalar = AppendChannels<PropertyScalarChannelDesc,
                                             PropertyScalarChannelRecord,
                                             float,
                                             ecs::PropertyTrackSemantic>(
        property_scalar_channels, scalar_keyframes, desc_.scalar_channels, desc_.scalar_channel_count);
    record->property.float2 = AppendChannels<PropertyFloat2ChannelDesc,
                                             PropertyFloat2ChannelRecord,
                                             ecs::Float2,
                                             ecs::PropertyTrackSemantic>(
        property_float2_channels, float2_keyframes, desc_.float2_channels, desc_.float2_channel_count);
    record->property.float3 = AppendChannels<PropertyFloat3ChannelDesc,
                                             PropertyFloat3ChannelRecord,
                                             ecs::Float3,
                                             ecs::PropertyTrackSemantic>(
        property_float3_channels, float3_keyframes, desc_.float3_channels, desc_.float3_channel_count);
    record->property.float4 = AppendChannels<PropertyFloat4ChannelDesc,
                                             PropertyFloat4ChannelRecord,
                                             ecs::Float4,
                                             ecs::PropertyTrackSemantic>(
        property_float4_channels, float4_keyframes, desc_.float4_channels, desc_.float4_channel_count);
    record->property.quaternion = AppendChannels<PropertyQuaternionChannelDesc,
                                                 PropertyQuaternionChannelRecord,
                                                 ecs::Quaternion,
                                                 ecs::PropertyTrackSemantic>(
        property_quaternion_channels, quaternion_keyframes, desc_.quaternion_channels, desc_.quaternion_channel_count);
    record->property.color = AppendChannels<PropertyColorChannelDesc,
                                            PropertyColorChannelRecord,
                                            ecs::Rgba8,
                                            ecs::PropertyTrackSemantic>(
        property_color_channels, color_keyframes, desc_.color_channels, desc_.color_channel_count);

    stats.clip_count = static_cast<std::uint32_t>(clips.size());
    ++stats.revision;
    return record->handle;
}

ecs::AnimationClipHandle AnimationClipHost::UpsertMaterialClip(const MaterialAnimationClipDesc& desc_) {
    if (!initialized) {
        throw std::runtime_error("AnimationClipHost::UpsertMaterialClip called before Initialize");
    }
    if (desc_.clip_id == 0U) {
        throw std::invalid_argument("AnimationClipHost::UpsertMaterialClip clip_id must be non-zero");
    }

    AnimationClipRecord* record = FindMutableClipById(desc_.clip_id);
    const bool exists = record != nullptr;
    if (exists && record->kind != AnimationClipKind::material_track) {
        throw std::invalid_argument("AnimationClipHost::UpsertMaterialClip clip kind mismatch");
    }

    if (!exists) {
        const ecs::AnimationClipHandle handle = AllocateHandle();
        clips.push_back(AnimationClipRecord{});
        record = &clips.back();
        record->clip_id = desc_.clip_id;
        record->handle = handle;
        record->kind = AnimationClipKind::material_track;
        record->revision = 1U;

        ClipLookupEntry lookup_entry{};
        lookup_entry.clip_id = desc_.clip_id;
        lookup_entry.slot_index = handle.index;
        const std::size_t lookup_index = LowerBoundLookupIndex(desc_.clip_id);
        InsertVectorValue(lookup, lookup_index, lookup_entry);

        slots[handle.index].record_index = static_cast<std::uint32_t>(clips.size() - 1U);
        UpdateStatsByKind(AnimationClipKind::material_track, +1);
        ++stats.added_clip_count;
    } else {
        ++record->revision;
        ++stats.updated_clip_count;
    }

    record->duration_s = std::max(1e-6F, desc_.duration_s);
    record->material.scalar = AppendChannels<MaterialScalarChannelDesc,
                                             MaterialScalarChannelRecord,
                                             float,
                                             ecs::MaterialTrackSemantic>(
        material_scalar_channels, scalar_keyframes, desc_.scalar_channels, desc_.scalar_channel_count);
    record->material.float4 = AppendChannels<MaterialFloat4ChannelDesc,
                                             MaterialFloat4ChannelRecord,
                                             ecs::Float4,
                                             ecs::MaterialTrackSemantic>(
        material_float4_channels, float4_keyframes, desc_.float4_channels, desc_.float4_channel_count);
    record->material.color = AppendChannels<MaterialColorChannelDesc,
                                            MaterialColorChannelRecord,
                                            ecs::Rgba8,
                                            ecs::MaterialTrackSemantic>(
        material_color_channels, color_keyframes, desc_.color_channels, desc_.color_channel_count);

    stats.clip_count = static_cast<std::uint32_t>(clips.size());
    ++stats.revision;
    return record->handle;
}

ecs::AnimationClipHandle AnimationClipHost::UpsertCameraClip(const CameraAnimationClipDesc& desc_) {
    if (!initialized) {
        throw std::runtime_error("AnimationClipHost::UpsertCameraClip called before Initialize");
    }
    if (desc_.clip_id == 0U) {
        throw std::invalid_argument("AnimationClipHost::UpsertCameraClip clip_id must be non-zero");
    }

    AnimationClipRecord* record = FindMutableClipById(desc_.clip_id);
    const bool exists = record != nullptr;
    if (exists && record->kind != AnimationClipKind::camera_track) {
        throw std::invalid_argument("AnimationClipHost::UpsertCameraClip clip kind mismatch");
    }

    if (!exists) {
        const ecs::AnimationClipHandle handle = AllocateHandle();
        clips.push_back(AnimationClipRecord{});
        record = &clips.back();
        record->clip_id = desc_.clip_id;
        record->handle = handle;
        record->kind = AnimationClipKind::camera_track;
        record->revision = 1U;

        ClipLookupEntry lookup_entry{};
        lookup_entry.clip_id = desc_.clip_id;
        lookup_entry.slot_index = handle.index;
        const std::size_t lookup_index = LowerBoundLookupIndex(desc_.clip_id);
        InsertVectorValue(lookup, lookup_index, lookup_entry);

        slots[handle.index].record_index = static_cast<std::uint32_t>(clips.size() - 1U);
        UpdateStatsByKind(AnimationClipKind::camera_track, +1);
        ++stats.added_clip_count;
    } else {
        ++record->revision;
        ++stats.updated_clip_count;
    }

    record->duration_s = std::max(1e-6F, desc_.duration_s);
    record->camera.scalar = AppendChannels<CameraScalarChannelDesc,
                                           CameraScalarChannelRecord,
                                           float,
                                           ecs::CameraTrackSemantic>(
        camera_scalar_channels, scalar_keyframes, desc_.scalar_channels, desc_.scalar_channel_count);
    record->camera.float2 = AppendChannels<CameraFloat2ChannelDesc,
                                           CameraFloat2ChannelRecord,
                                           ecs::Float2,
                                           ecs::CameraTrackSemantic>(
        camera_float2_channels, float2_keyframes, desc_.float2_channels, desc_.float2_channel_count);
    record->camera.float3 = AppendChannels<CameraFloat3ChannelDesc,
                                           CameraFloat3ChannelRecord,
                                           ecs::Float3,
                                           ecs::CameraTrackSemantic>(
        camera_float3_channels, float3_keyframes, desc_.float3_channels, desc_.float3_channel_count);
    record->camera.quaternion = AppendChannels<CameraQuaternionChannelDesc,
                                               CameraQuaternionChannelRecord,
                                               ecs::Quaternion,
                                               ecs::CameraTrackSemantic>(
        camera_quaternion_channels, quaternion_keyframes, desc_.quaternion_channels, desc_.quaternion_channel_count);

    stats.clip_count = static_cast<std::uint32_t>(clips.size());
    ++stats.revision;
    return record->handle;
}

bool AnimationClipHost::RemoveClip(std::uint32_t clip_id_) noexcept {
    if (!initialized || clip_id_ == 0U) {
        return false;
    }

    const std::size_t lookup_index = LowerBoundLookupIndex(clip_id_);
    if (lookup_index >= lookup.size() || lookup[lookup_index].clip_id != clip_id_) {
        return false;
    }

    const std::uint32_t slot_index = lookup[lookup_index].slot_index;
    if (slot_index >= slots.size() || slots[slot_index].alive == 0U) {
        return false;
    }

    const std::uint32_t record_index = slots[slot_index].record_index;
    if (record_index >= clips.size()) {
        return false;
    }

    const AnimationClipKind kind = clips[record_index].kind;
    UpdateStatsByKind(kind, -1);

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

const AnimationClipRecord* AnimationClipHost::FindClipById(std::uint32_t clip_id_) const noexcept {
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

const AnimationClipRecord* AnimationClipHost::FindClipByHandle(ecs::AnimationClipHandle handle_) const noexcept {
    if (!initialized || handle_.index == ecs::invalid_animation_handle_index || handle_.index >= slots.size()) {
        return nullptr;
    }
    const ClipSlot& slot = slots[handle_.index];
    if (slot.alive == 0U || slot.generation != handle_.generation || slot.record_index >= clips.size()) {
        return nullptr;
    }
    return &clips[slot.record_index];
}

const AnimationClipRecord* AnimationClipHost::FindPropertyClipById(std::uint32_t clip_id_) const noexcept {
    const AnimationClipRecord* record = FindClipById(clip_id_);
    return (record != nullptr && record->kind == AnimationClipKind::property_track) ? record : nullptr;
}

const AnimationClipRecord* AnimationClipHost::FindPropertyClipByHandle(ecs::AnimationClipHandle handle_) const noexcept {
    const AnimationClipRecord* record = FindClipByHandle(handle_);
    return (record != nullptr && record->kind == AnimationClipKind::property_track) ? record : nullptr;
}

const AnimationClipRecord* AnimationClipHost::FindMaterialClipById(std::uint32_t clip_id_) const noexcept {
    const AnimationClipRecord* record = FindClipById(clip_id_);
    return (record != nullptr && record->kind == AnimationClipKind::material_track) ? record : nullptr;
}

const AnimationClipRecord* AnimationClipHost::FindMaterialClipByHandle(ecs::AnimationClipHandle handle_) const noexcept {
    const AnimationClipRecord* record = FindClipByHandle(handle_);
    return (record != nullptr && record->kind == AnimationClipKind::material_track) ? record : nullptr;
}

const AnimationClipRecord* AnimationClipHost::FindCameraClipById(std::uint32_t clip_id_) const noexcept {
    const AnimationClipRecord* record = FindClipById(clip_id_);
    return (record != nullptr && record->kind == AnimationClipKind::camera_track) ? record : nullptr;
}

const AnimationClipRecord* AnimationClipHost::FindCameraClipByHandle(ecs::AnimationClipHandle handle_) const noexcept {
    const AnimationClipRecord* record = FindClipByHandle(handle_);
    return (record != nullptr && record->kind == AnimationClipKind::camera_track) ? record : nullptr;
}

const PropertyScalarChannelRecord* AnimationClipHost::PropertyScalarChannels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(property_scalar_channels, clip_.property.scalar);
}

const PropertyFloat2ChannelRecord* AnimationClipHost::PropertyFloat2Channels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(property_float2_channels, clip_.property.float2);
}

const PropertyFloat3ChannelRecord* AnimationClipHost::PropertyFloat3Channels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(property_float3_channels, clip_.property.float3);
}

const PropertyFloat4ChannelRecord* AnimationClipHost::PropertyFloat4Channels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(property_float4_channels, clip_.property.float4);
}

const PropertyQuaternionChannelRecord* AnimationClipHost::PropertyQuaternionChannels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(property_quaternion_channels, clip_.property.quaternion);
}

const PropertyColorChannelRecord* AnimationClipHost::PropertyColorChannels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(property_color_channels, clip_.property.color);
}

const MaterialScalarChannelRecord* AnimationClipHost::MaterialScalarChannels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(material_scalar_channels, clip_.material.scalar);
}

const MaterialFloat4ChannelRecord* AnimationClipHost::MaterialFloat4Channels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(material_float4_channels, clip_.material.float4);
}

const MaterialColorChannelRecord* AnimationClipHost::MaterialColorChannels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(material_color_channels, clip_.material.color);
}

const CameraScalarChannelRecord* AnimationClipHost::CameraScalarChannels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(camera_scalar_channels, clip_.camera.scalar);
}

const CameraFloat2ChannelRecord* AnimationClipHost::CameraFloat2Channels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(camera_float2_channels, clip_.camera.float2);
}

const CameraFloat3ChannelRecord* AnimationClipHost::CameraFloat3Channels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(camera_float3_channels, clip_.camera.float3);
}

const CameraQuaternionChannelRecord* AnimationClipHost::CameraQuaternionChannels(const AnimationClipRecord& clip_) const noexcept {
    return ChannelsOrNull(camera_quaternion_channels, clip_.camera.quaternion);
}

ecs::AnimationCurveView<float> AnimationClipHost::BuildCurveView(const PropertyScalarChannelRecord& channel_) const noexcept {
    return BuildView(scalar_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<ecs::Float2> AnimationClipHost::BuildCurveView(const PropertyFloat2ChannelRecord& channel_) const noexcept {
    return BuildView(float2_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<ecs::Float3> AnimationClipHost::BuildCurveView(const PropertyFloat3ChannelRecord& channel_) const noexcept {
    return BuildView(float3_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<ecs::Float4> AnimationClipHost::BuildCurveView(const PropertyFloat4ChannelRecord& channel_) const noexcept {
    return BuildView(float4_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<ecs::Quaternion> AnimationClipHost::BuildCurveView(const PropertyQuaternionChannelRecord& channel_) const noexcept {
    return BuildView(quaternion_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<ecs::Rgba8> AnimationClipHost::BuildCurveView(const PropertyColorChannelRecord& channel_) const noexcept {
    return BuildView(color_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<float> AnimationClipHost::BuildCurveView(const MaterialScalarChannelRecord& channel_) const noexcept {
    return BuildView(scalar_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<ecs::Float4> AnimationClipHost::BuildCurveView(const MaterialFloat4ChannelRecord& channel_) const noexcept {
    return BuildView(float4_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<ecs::Rgba8> AnimationClipHost::BuildCurveView(const MaterialColorChannelRecord& channel_) const noexcept {
    return BuildView(color_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<float> AnimationClipHost::BuildCurveView(const CameraScalarChannelRecord& channel_) const noexcept {
    return BuildView(scalar_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<ecs::Float2> AnimationClipHost::BuildCurveView(const CameraFloat2ChannelRecord& channel_) const noexcept {
    return BuildView(float2_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<ecs::Float3> AnimationClipHost::BuildCurveView(const CameraFloat3ChannelRecord& channel_) const noexcept {
    return BuildView(float3_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

ecs::AnimationCurveView<ecs::Quaternion> AnimationClipHost::BuildCurveView(const CameraQuaternionChannelRecord& channel_) const noexcept {
    return BuildView(quaternion_keyframes, channel_.keyframe_begin, channel_.keyframe_count);
}

bool AnimationClipHost::IsInitialized() const noexcept {
    return initialized;
}

const AnimationClipHostStats& AnimationClipHost::Stats() const noexcept {
    return stats;
}

std::size_t AnimationClipHost::LowerBoundLookupIndex(std::uint32_t clip_id_) const noexcept {
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

ecs::AnimationClipHandle AnimationClipHost::AllocateHandle() {
    if (!free_slot_indices.empty()) {
        const std::uint32_t slot_index = free_slot_indices.back();
        free_slot_indices.pop_back();
        ClipSlot& slot = slots[slot_index];
        slot.alive = 1U;
        slot.record_index = k_invalid_record_index;
        return ecs::AnimationClipHandle{
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
    return ecs::AnimationClipHandle{
        .index = slot_index,
        .generation = 1U,
    };
}

void AnimationClipHost::UpdateStatsByKind(AnimationClipKind kind_, int delta_) noexcept {
    auto apply = [delta_](std::uint32_t& value_) noexcept {
        value_ = static_cast<std::uint32_t>(static_cast<int>(value_) + delta_);
    };
    switch (kind_) {
        case AnimationClipKind::property_track:
            apply(stats.property_clip_count);
            break;
        case AnimationClipKind::material_track:
            apply(stats.material_clip_count);
            break;
        case AnimationClipKind::camera_track:
            apply(stats.camera_clip_count);
            break;
        case AnimationClipKind::none:
        default:
            break;
    }
}

void AnimationClipHost::RemoveLookupIndex(std::size_t lookup_index_) noexcept {
    EraseVectorValue(lookup, lookup_index_);
}

void AnimationClipHost::UpdateMovedRecordSlot(std::uint32_t record_index_) noexcept {
    if (record_index_ >= clips.size()) {
        return;
    }
    const std::uint32_t slot_index = clips[record_index_].handle.index;
    if (slot_index < slots.size()) {
        slots[slot_index].record_index = record_index_;
    }
}

AnimationClipRecord* AnimationClipHost::FindMutableClipByHandle(ecs::AnimationClipHandle handle_) noexcept {
    return const_cast<AnimationClipRecord*>(std::as_const(*this).FindClipByHandle(handle_));
}

AnimationClipRecord* AnimationClipHost::FindMutableClipById(std::uint32_t clip_id_) noexcept {
    return const_cast<AnimationClipRecord*>(std::as_const(*this).FindClipById(clip_id_));
}

} // namespace vr::animation
