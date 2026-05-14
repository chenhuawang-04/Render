#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/system/animation_curve_system.hpp"

#include <cstdint>

namespace vr::animation {

template<typename T>
using AnimationVertexDeformMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct VertexDeformTrackDesc final {
    std::uint16_t parameter_index = 0U;
    std::uint16_t reserved0 = 0U;
    vr::ecs::AnimationCurveView<vr::ecs::Float4> parameter_curve{.keyframes = nullptr, .keyframe_count = 0U};
};

struct VertexDeformClipDesc final {
    std::uint32_t clip_id = 0U;
    float duration_s = 1.0F;
    const vr::ecs::Float4* base_parameters = nullptr;
    std::uint32_t parameter_count = 0U;
    const VertexDeformTrackDesc* tracks = nullptr;
    std::uint32_t track_count = 0U;
};

struct VertexDeformAnimationHostCreateInfo final {
    std::uint32_t reserve_clip_count = 128U;
    std::uint32_t reserve_parameter_count = 4096U;
    std::uint32_t reserve_track_count = 2048U;
    std::uint32_t reserve_keyframe_count = 16384U;
};

struct VertexDeformAnimationHostStats final {
    std::uint32_t clip_count = 0U;
    std::uint32_t added_clip_count = 0U;
    std::uint32_t updated_clip_count = 0U;
    std::uint32_t removed_clip_count = 0U;
    std::uint32_t revision = 0U;
};

struct VertexDeformTrackRecord final {
    std::uint16_t parameter_index = 0U;
    std::uint16_t reserved0 = 0U;
    std::uint32_t keyframe_begin = 0U;
    std::uint32_t keyframe_count = 0U;
};

struct VertexDeformClipRecord final {
    std::uint32_t clip_id = 0U;
    vr::ecs::AnimationClipHandle handle = vr::ecs::invalid_animation_clip_handle;
    float duration_s = 1.0F;
    std::uint32_t revision = 0U;
    std::uint32_t base_parameter_begin = 0U;
    std::uint32_t parameter_count = 0U;
    std::uint32_t track_begin = 0U;
    std::uint32_t track_count = 0U;
};

class VertexDeformAnimationHost final {
public:
    VertexDeformAnimationHost() = default;
    ~VertexDeformAnimationHost() = default;

    VertexDeformAnimationHost(const VertexDeformAnimationHost&) = delete;
    VertexDeformAnimationHost& operator=(const VertexDeformAnimationHost&) = delete;
    VertexDeformAnimationHost(VertexDeformAnimationHost&&) = delete;
    VertexDeformAnimationHost& operator=(VertexDeformAnimationHost&&) = delete;

    void Initialize(const VertexDeformAnimationHostCreateInfo& create_info_ = {});
    void Shutdown() noexcept;

    vr::ecs::AnimationClipHandle UpsertClip(const VertexDeformClipDesc& desc_);
    [[nodiscard]] bool RemoveClip(std::uint32_t clip_id_) noexcept;

    [[nodiscard]] const VertexDeformClipRecord* FindClipById(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] const VertexDeformClipRecord* FindClipByHandle(vr::ecs::AnimationClipHandle handle_) const noexcept;
    [[nodiscard]] const vr::ecs::Float4* BaseParameters(const VertexDeformClipRecord& clip_) const noexcept;
    [[nodiscard]] const VertexDeformTrackRecord* Tracks(const VertexDeformClipRecord& clip_) const noexcept;
    [[nodiscard]] vr::ecs::AnimationCurveView<vr::ecs::Float4> ParameterCurveView(
        const VertexDeformTrackRecord& track_) const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const VertexDeformAnimationHostStats& Stats() const noexcept;

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
    VertexDeformAnimationHostCreateInfo create_info_cache{};
    VertexDeformAnimationHostStats stats{};

    AnimationVertexDeformMcVector<VertexDeformClipRecord> clips{};
    AnimationVertexDeformMcVector<ClipLookupEntry> lookup{};
    AnimationVertexDeformMcVector<ClipSlot> slots{};
    AnimationVertexDeformMcVector<std::uint32_t> free_slot_indices{};
    AnimationVertexDeformMcVector<vr::ecs::Float4> base_parameters{};
    AnimationVertexDeformMcVector<VertexDeformTrackRecord> tracks{};
    AnimationVertexDeformMcVector<vr::ecs::AnimationKeyframe<vr::ecs::Float4>> parameter_keyframes{};

    bool initialized = false;
};

} // namespace vr::animation

