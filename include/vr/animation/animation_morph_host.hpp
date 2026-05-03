#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/system/animation_curve_system.hpp"

#include <cstdint>

namespace vr::animation {

template<typename T>
using AnimationMorphMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct MorphTrackDesc final {
    std::uint16_t target_index = 0U;
    std::uint16_t reserved0 = 0U;
    vr::ecs::AnimationCurveView<float> weight_curve{.keyframes = nullptr, .keyframe_count = 0U};
};

struct MorphClipDesc final {
    std::uint32_t clip_id = 0U;
    float duration_s = 1.0F;
    const float* base_weights = nullptr;
    std::uint32_t weight_count = 0U;
    const MorphTrackDesc* tracks = nullptr;
    std::uint32_t track_count = 0U;
};

struct MorphAnimationHostCreateInfo final {
    std::uint32_t reserve_clip_count = 128U;
    std::uint32_t reserve_weight_count = 4096U;
    std::uint32_t reserve_track_count = 2048U;
    std::uint32_t reserve_keyframe_count = 16384U;
};

struct MorphAnimationHostStats final {
    std::uint32_t clip_count = 0U;
    std::uint32_t added_clip_count = 0U;
    std::uint32_t updated_clip_count = 0U;
    std::uint32_t removed_clip_count = 0U;
    std::uint32_t revision = 0U;
};

struct MorphTrackRecord final {
    std::uint16_t target_index = 0U;
    std::uint16_t reserved0 = 0U;
    std::uint32_t keyframe_begin = 0U;
    std::uint32_t keyframe_count = 0U;
};

struct MorphClipRecord final {
    std::uint32_t clip_id = 0U;
    vr::ecs::AnimationClipHandle handle = vr::ecs::invalid_animation_clip_handle;
    float duration_s = 1.0F;
    std::uint32_t revision = 0U;
    std::uint32_t base_weight_begin = 0U;
    std::uint32_t weight_count = 0U;
    std::uint32_t track_begin = 0U;
    std::uint32_t track_count = 0U;
};

class MorphAnimationHost final {
public:
    MorphAnimationHost() = default;
    ~MorphAnimationHost() = default;

    MorphAnimationHost(const MorphAnimationHost&) = delete;
    MorphAnimationHost& operator=(const MorphAnimationHost&) = delete;
    MorphAnimationHost(MorphAnimationHost&&) = delete;
    MorphAnimationHost& operator=(MorphAnimationHost&&) = delete;

    void Initialize(const MorphAnimationHostCreateInfo& create_info_ = {});
    void Shutdown() noexcept;

    vr::ecs::AnimationClipHandle UpsertClip(const MorphClipDesc& desc_);
    [[nodiscard]] bool RemoveClip(std::uint32_t clip_id_) noexcept;

    [[nodiscard]] const MorphClipRecord* FindClipById(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] const MorphClipRecord* FindClipByHandle(vr::ecs::AnimationClipHandle handle_) const noexcept;
    [[nodiscard]] const float* BaseWeights(const MorphClipRecord& clip_) const noexcept;
    [[nodiscard]] const MorphTrackRecord* Tracks(const MorphClipRecord& clip_) const noexcept;
    [[nodiscard]] vr::ecs::AnimationCurveView<float> WeightCurveView(const MorphTrackRecord& track_) const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const MorphAnimationHostStats& Stats() const noexcept;

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
    void RemoveLookupIndex(std::size_t lookup_index_) noexcept;
    void UpdateMovedRecordSlot(std::uint32_t record_index_) noexcept;

private:
    MorphAnimationHostCreateInfo create_info_cache{};
    MorphAnimationHostStats stats{};

    AnimationMorphMcVector<MorphClipRecord> clips{};
    AnimationMorphMcVector<ClipLookupEntry> lookup{};
    AnimationMorphMcVector<ClipSlot> slots{};
    AnimationMorphMcVector<std::uint32_t> free_slot_indices{};
    AnimationMorphMcVector<float> base_weights{};
    AnimationMorphMcVector<MorphTrackRecord> tracks{};
    AnimationMorphMcVector<vr::ecs::AnimationKeyframe<float>> weight_keyframes{};

    bool initialized = false;
};

} // namespace vr::animation
