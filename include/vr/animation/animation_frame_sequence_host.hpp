#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/system/animation_curve_system.hpp"

#include <cstdint>

namespace vr::animation {

template<typename T>
using AnimationFrameSequenceMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct FrameSequenceClipDesc final {
    std::uint32_t clip_id = 0U;
    float duration_s = 1.0F;
    std::uint32_t frame_count = 0U;
    vr::ecs::AnimationCurveView<float> frame_curve{.keyframes = nullptr, .keyframe_count = 0U};
};

struct FrameSequenceAnimationHostCreateInfo final {
    std::uint32_t reserve_clip_count = 128U;
    std::uint32_t reserve_keyframe_count = 8192U;
};

struct FrameSequenceAnimationHostStats final {
    std::uint32_t clip_count = 0U;
    std::uint32_t added_clip_count = 0U;
    std::uint32_t updated_clip_count = 0U;
    std::uint32_t removed_clip_count = 0U;
    std::uint32_t revision = 0U;
};

struct FrameSequenceClipRecord final {
    std::uint32_t clip_id = 0U;
    vr::ecs::AnimationClipHandle handle = vr::ecs::invalid_animation_clip_handle;
    float duration_s = 1.0F;
    std::uint32_t revision = 0U;
    std::uint32_t frame_count = 0U;
    std::uint32_t keyframe_begin = 0U;
    std::uint32_t keyframe_count = 0U;
};

class FrameSequenceAnimationHost final {
public:
    FrameSequenceAnimationHost() = default;
    ~FrameSequenceAnimationHost() = default;

    FrameSequenceAnimationHost(const FrameSequenceAnimationHost&) = delete;
    FrameSequenceAnimationHost& operator=(const FrameSequenceAnimationHost&) = delete;
    FrameSequenceAnimationHost(FrameSequenceAnimationHost&&) = delete;
    FrameSequenceAnimationHost& operator=(FrameSequenceAnimationHost&&) = delete;

    void Initialize(const FrameSequenceAnimationHostCreateInfo& create_info_ = {});
    void Shutdown() noexcept;

    vr::ecs::AnimationClipHandle UpsertClip(const FrameSequenceClipDesc& desc_);
    [[nodiscard]] bool RemoveClip(std::uint32_t clip_id_) noexcept;

    [[nodiscard]] const FrameSequenceClipRecord* FindClipById(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] const FrameSequenceClipRecord* FindClipByHandle(vr::ecs::AnimationClipHandle handle_) const noexcept;
    [[nodiscard]] vr::ecs::AnimationCurveView<float> FrameCurveView(const FrameSequenceClipRecord& clip_) const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const FrameSequenceAnimationHostStats& Stats() const noexcept;

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
    FrameSequenceAnimationHostCreateInfo create_info_cache{};
    FrameSequenceAnimationHostStats stats{};

    AnimationFrameSequenceMcVector<FrameSequenceClipRecord> clips{};
    AnimationFrameSequenceMcVector<ClipLookupEntry> lookup{};
    AnimationFrameSequenceMcVector<ClipSlot> slots{};
    AnimationFrameSequenceMcVector<std::uint32_t> free_slot_indices{};
    AnimationFrameSequenceMcVector<vr::ecs::AnimationKeyframe<float>> frame_keyframes{};

    bool initialized = false;
};

} // namespace vr::animation
