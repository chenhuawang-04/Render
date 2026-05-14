#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/system/animation_curve_system.hpp"

#include <cstdint>

namespace vr::animation {

template<typename T>
using AnimationMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct AnimationScalarValueTag final {};
struct AnimationFloat2ValueTag final {};
struct AnimationFloat3ValueTag final {};
struct AnimationFloat4ValueTag final {};
struct AnimationQuaternionValueTag final {};
struct AnimationColorValueTag final {};

enum class AnimationClipKind : std::uint8_t {
    none = 0U,
    property_track = 1U,
    visual_track = 2U,
    camera_track = 3U,
};

template<typename SemanticT, typename ValueT>
struct TypedAnimationChannelDesc final {
    SemanticT semantic{};
    std::uint16_t channel_mask = 0xFFFFU;
    std::uint16_t reserved0 = 0U;
    ecs::AnimationCurveView<ValueT> curve{.keyframes = nullptr, .keyframe_count = 0U};
};

using PropertyScalarChannelDesc = TypedAnimationChannelDesc<ecs::PropertyTrackSemantic, float>;
using PropertyFloat2ChannelDesc = TypedAnimationChannelDesc<ecs::PropertyTrackSemantic, ecs::Float2>;
using PropertyFloat3ChannelDesc = TypedAnimationChannelDesc<ecs::PropertyTrackSemantic, ecs::Float3>;
using PropertyFloat4ChannelDesc = TypedAnimationChannelDesc<ecs::PropertyTrackSemantic, ecs::Float4>;
using PropertyQuaternionChannelDesc = TypedAnimationChannelDesc<ecs::PropertyTrackSemantic, ecs::Quaternion>;
using PropertyColorChannelDesc = TypedAnimationChannelDesc<ecs::PropertyTrackSemantic, ecs::Rgba8>;

using VisualScalarChannelDesc = TypedAnimationChannelDesc<ecs::VisualTrackSemantic, float>;
using VisualFloat4ChannelDesc = TypedAnimationChannelDesc<ecs::VisualTrackSemantic, ecs::Float4>;
using VisualColorChannelDesc = TypedAnimationChannelDesc<ecs::VisualTrackSemantic, ecs::Rgba8>;

using CameraScalarChannelDesc = TypedAnimationChannelDesc<ecs::CameraTrackSemantic, float>;
using CameraFloat2ChannelDesc = TypedAnimationChannelDesc<ecs::CameraTrackSemantic, ecs::Float2>;
using CameraFloat3ChannelDesc = TypedAnimationChannelDesc<ecs::CameraTrackSemantic, ecs::Float3>;
using CameraQuaternionChannelDesc = TypedAnimationChannelDesc<ecs::CameraTrackSemantic, ecs::Quaternion>;

struct PropertyAnimationClipDesc final {
    std::uint32_t clip_id = 0U;
    float duration_s = 1.0F;
    const PropertyScalarChannelDesc* scalar_channels = nullptr;
    std::uint32_t scalar_channel_count = 0U;
    const PropertyFloat2ChannelDesc* float2_channels = nullptr;
    std::uint32_t float2_channel_count = 0U;
    const PropertyFloat3ChannelDesc* float3_channels = nullptr;
    std::uint32_t float3_channel_count = 0U;
    const PropertyFloat4ChannelDesc* float4_channels = nullptr;
    std::uint32_t float4_channel_count = 0U;
    const PropertyQuaternionChannelDesc* quaternion_channels = nullptr;
    std::uint32_t quaternion_channel_count = 0U;
    const PropertyColorChannelDesc* color_channels = nullptr;
    std::uint32_t color_channel_count = 0U;
};

struct VisualAnimationClipDesc final {
    std::uint32_t clip_id = 0U;
    float duration_s = 1.0F;
    const VisualScalarChannelDesc* scalar_channels = nullptr;
    std::uint32_t scalar_channel_count = 0U;
    const VisualFloat4ChannelDesc* float4_channels = nullptr;
    std::uint32_t float4_channel_count = 0U;
    const VisualColorChannelDesc* color_channels = nullptr;
    std::uint32_t color_channel_count = 0U;
};

struct CameraAnimationClipDesc final {
    std::uint32_t clip_id = 0U;
    float duration_s = 1.0F;
    const CameraScalarChannelDesc* scalar_channels = nullptr;
    std::uint32_t scalar_channel_count = 0U;
    const CameraFloat2ChannelDesc* float2_channels = nullptr;
    std::uint32_t float2_channel_count = 0U;
    const CameraFloat3ChannelDesc* float3_channels = nullptr;
    std::uint32_t float3_channel_count = 0U;
    const CameraQuaternionChannelDesc* quaternion_channels = nullptr;
    std::uint32_t quaternion_channel_count = 0U;
};

struct AnimationClipHostCreateInfo final {
    std::uint32_t reserve_clip_count = 256U;
    std::uint32_t reserve_channel_count = 1024U;
    std::uint32_t reserve_keyframe_count = 8192U;
};

struct AnimationClipHostStats final {
    std::uint32_t clip_count = 0U;
    std::uint32_t property_clip_count = 0U;
    std::uint32_t visual_clip_count = 0U;
    std::uint32_t camera_clip_count = 0U;
    std::uint32_t added_clip_count = 0U;
    std::uint32_t updated_clip_count = 0U;
    std::uint32_t removed_clip_count = 0U;
    std::uint32_t revision = 0U;
};

struct AnimationChannelRange final {
    std::uint32_t begin = 0U;
    std::uint32_t count = 0U;
};

template<typename SemanticT, typename ValueTagT>
struct AnimationChannelRecord final {
    SemanticT semantic{};
    std::uint16_t channel_mask = 0xFFFFU;
    std::uint16_t reserved0 = 0U;
    std::uint32_t keyframe_begin = 0U;
    std::uint32_t keyframe_count = 0U;
};

struct PropertyClipLayout final {
    AnimationChannelRange scalar{};
    AnimationChannelRange float2{};
    AnimationChannelRange float3{};
    AnimationChannelRange float4{};
    AnimationChannelRange quaternion{};
    AnimationChannelRange color{};
};

struct VisualClipLayout final {
    AnimationChannelRange scalar{};
    AnimationChannelRange float4{};
    AnimationChannelRange color{};
};

struct CameraClipLayout final {
    AnimationChannelRange scalar{};
    AnimationChannelRange float2{};
    AnimationChannelRange float3{};
    AnimationChannelRange quaternion{};
};

struct AnimationClipRecord final {
    std::uint32_t clip_id = 0U;
    ecs::AnimationClipHandle handle = ecs::invalid_animation_clip_handle;
    float duration_s = 1.0F;
    AnimationClipKind kind = AnimationClipKind::none;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
    std::uint32_t revision = 0U;
    PropertyClipLayout property{};
    VisualClipLayout visual{};
    CameraClipLayout camera{};
};

using PropertyScalarChannelRecord = AnimationChannelRecord<ecs::PropertyTrackSemantic, AnimationScalarValueTag>;
using PropertyFloat2ChannelRecord = AnimationChannelRecord<ecs::PropertyTrackSemantic, AnimationFloat2ValueTag>;
using PropertyFloat3ChannelRecord = AnimationChannelRecord<ecs::PropertyTrackSemantic, AnimationFloat3ValueTag>;
using PropertyFloat4ChannelRecord = AnimationChannelRecord<ecs::PropertyTrackSemantic, AnimationFloat4ValueTag>;
using PropertyQuaternionChannelRecord = AnimationChannelRecord<ecs::PropertyTrackSemantic, AnimationQuaternionValueTag>;
using PropertyColorChannelRecord = AnimationChannelRecord<ecs::PropertyTrackSemantic, AnimationColorValueTag>;

using VisualScalarChannelRecord = AnimationChannelRecord<ecs::VisualTrackSemantic, AnimationScalarValueTag>;
using VisualFloat4ChannelRecord = AnimationChannelRecord<ecs::VisualTrackSemantic, AnimationFloat4ValueTag>;
using VisualColorChannelRecord = AnimationChannelRecord<ecs::VisualTrackSemantic, AnimationColorValueTag>;

using CameraScalarChannelRecord = AnimationChannelRecord<ecs::CameraTrackSemantic, AnimationScalarValueTag>;
using CameraFloat2ChannelRecord = AnimationChannelRecord<ecs::CameraTrackSemantic, AnimationFloat2ValueTag>;
using CameraFloat3ChannelRecord = AnimationChannelRecord<ecs::CameraTrackSemantic, AnimationFloat3ValueTag>;
using CameraQuaternionChannelRecord = AnimationChannelRecord<ecs::CameraTrackSemantic, AnimationQuaternionValueTag>;

class AnimationClipHost final {
public:
    AnimationClipHost() = default;
    ~AnimationClipHost() = default;

    AnimationClipHost(const AnimationClipHost&) = delete;
    AnimationClipHost& operator=(const AnimationClipHost&) = delete;
    AnimationClipHost(AnimationClipHost&&) = delete;
    AnimationClipHost& operator=(AnimationClipHost&&) = delete;

    void Initialize(const AnimationClipHostCreateInfo& create_info_ = {});
    void Shutdown() noexcept;

    ecs::AnimationClipHandle UpsertPropertyClip(const PropertyAnimationClipDesc& desc_);
    ecs::AnimationClipHandle UpsertVisualClip(const VisualAnimationClipDesc& desc_);
    ecs::AnimationClipHandle UpsertCameraClip(const CameraAnimationClipDesc& desc_);

    [[nodiscard]] bool RemoveClip(std::uint32_t clip_id_) noexcept;

    [[nodiscard]] const AnimationClipRecord* FindClipById(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] const AnimationClipRecord* FindClipByHandle(ecs::AnimationClipHandle handle_) const noexcept;
    [[nodiscard]] const AnimationClipRecord* FindPropertyClipById(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] const AnimationClipRecord* FindPropertyClipByHandle(ecs::AnimationClipHandle handle_) const noexcept;
    [[nodiscard]] const AnimationClipRecord* FindVisualClipById(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] const AnimationClipRecord* FindVisualClipByHandle(ecs::AnimationClipHandle handle_) const noexcept;
    [[nodiscard]] const AnimationClipRecord* FindCameraClipById(std::uint32_t clip_id_) const noexcept;
    [[nodiscard]] const AnimationClipRecord* FindCameraClipByHandle(ecs::AnimationClipHandle handle_) const noexcept;

    [[nodiscard]] const PropertyScalarChannelRecord* PropertyScalarChannels(const AnimationClipRecord& clip_) const noexcept;
    [[nodiscard]] const PropertyFloat2ChannelRecord* PropertyFloat2Channels(const AnimationClipRecord& clip_) const noexcept;
    [[nodiscard]] const PropertyFloat3ChannelRecord* PropertyFloat3Channels(const AnimationClipRecord& clip_) const noexcept;
    [[nodiscard]] const PropertyFloat4ChannelRecord* PropertyFloat4Channels(const AnimationClipRecord& clip_) const noexcept;
    [[nodiscard]] const PropertyQuaternionChannelRecord* PropertyQuaternionChannels(const AnimationClipRecord& clip_) const noexcept;
    [[nodiscard]] const PropertyColorChannelRecord* PropertyColorChannels(const AnimationClipRecord& clip_) const noexcept;

    [[nodiscard]] const VisualScalarChannelRecord* VisualScalarChannels(const AnimationClipRecord& clip_) const noexcept;
    [[nodiscard]] const VisualFloat4ChannelRecord* VisualFloat4Channels(const AnimationClipRecord& clip_) const noexcept;
    [[nodiscard]] const VisualColorChannelRecord* VisualColorChannels(const AnimationClipRecord& clip_) const noexcept;

    [[nodiscard]] const CameraScalarChannelRecord* CameraScalarChannels(const AnimationClipRecord& clip_) const noexcept;
    [[nodiscard]] const CameraFloat2ChannelRecord* CameraFloat2Channels(const AnimationClipRecord& clip_) const noexcept;
    [[nodiscard]] const CameraFloat3ChannelRecord* CameraFloat3Channels(const AnimationClipRecord& clip_) const noexcept;
    [[nodiscard]] const CameraQuaternionChannelRecord* CameraQuaternionChannels(const AnimationClipRecord& clip_) const noexcept;

    [[nodiscard]] ecs::AnimationCurveView<float> BuildCurveView(const PropertyScalarChannelRecord& channel_) const noexcept;
    [[nodiscard]] ecs::AnimationCurveView<ecs::Float2> BuildCurveView(const PropertyFloat2ChannelRecord& channel_) const noexcept;
    [[nodiscard]] ecs::AnimationCurveView<ecs::Float3> BuildCurveView(const PropertyFloat3ChannelRecord& channel_) const noexcept;
    [[nodiscard]] ecs::AnimationCurveView<ecs::Float4> BuildCurveView(const PropertyFloat4ChannelRecord& channel_) const noexcept;
    [[nodiscard]] ecs::AnimationCurveView<ecs::Quaternion> BuildCurveView(const PropertyQuaternionChannelRecord& channel_) const noexcept;
    [[nodiscard]] ecs::AnimationCurveView<ecs::Rgba8> BuildCurveView(const PropertyColorChannelRecord& channel_) const noexcept;

    [[nodiscard]] ecs::AnimationCurveView<float> BuildCurveView(const VisualScalarChannelRecord& channel_) const noexcept;
    [[nodiscard]] ecs::AnimationCurveView<ecs::Float4> BuildCurveView(const VisualFloat4ChannelRecord& channel_) const noexcept;
    [[nodiscard]] ecs::AnimationCurveView<ecs::Rgba8> BuildCurveView(const VisualColorChannelRecord& channel_) const noexcept;

    [[nodiscard]] ecs::AnimationCurveView<float> BuildCurveView(const CameraScalarChannelRecord& channel_) const noexcept;
    [[nodiscard]] ecs::AnimationCurveView<ecs::Float2> BuildCurveView(const CameraFloat2ChannelRecord& channel_) const noexcept;
    [[nodiscard]] ecs::AnimationCurveView<ecs::Float3> BuildCurveView(const CameraFloat3ChannelRecord& channel_) const noexcept;
    [[nodiscard]] ecs::AnimationCurveView<ecs::Quaternion> BuildCurveView(const CameraQuaternionChannelRecord& channel_) const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const AnimationClipHostStats& Stats() const noexcept;

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
    [[nodiscard]] ecs::AnimationClipHandle AllocateHandle();
    void UpdateStatsByKind(AnimationClipKind kind_, int delta_) noexcept;
    void RemoveLookupIndex(std::size_t lookup_index_) noexcept;
    void UpdateMovedRecordSlot(std::uint32_t record_index_) noexcept;

    [[nodiscard]] AnimationClipRecord* FindMutableClipByHandle(ecs::AnimationClipHandle handle_) noexcept;
    [[nodiscard]] AnimationClipRecord* FindMutableClipById(std::uint32_t clip_id_) noexcept;

private:
    AnimationClipHostCreateInfo create_info_cache{};
    AnimationClipHostStats stats{};

    AnimationMcVector<AnimationClipRecord> clips{};
    AnimationMcVector<ClipLookupEntry> lookup{};
    AnimationMcVector<ClipSlot> slots{};
    AnimationMcVector<std::uint32_t> free_slot_indices{};

    AnimationMcVector<PropertyScalarChannelRecord> property_scalar_channels{};
    AnimationMcVector<PropertyFloat2ChannelRecord> property_float2_channels{};
    AnimationMcVector<PropertyFloat3ChannelRecord> property_float3_channels{};
    AnimationMcVector<PropertyFloat4ChannelRecord> property_float4_channels{};
    AnimationMcVector<PropertyQuaternionChannelRecord> property_quaternion_channels{};
    AnimationMcVector<PropertyColorChannelRecord> property_color_channels{};

    AnimationMcVector<VisualScalarChannelRecord> visual_scalar_channels{};
    AnimationMcVector<VisualFloat4ChannelRecord> visual_float4_channels{};
    AnimationMcVector<VisualColorChannelRecord> visual_color_channels{};

    AnimationMcVector<CameraScalarChannelRecord> camera_scalar_channels{};
    AnimationMcVector<CameraFloat2ChannelRecord> camera_float2_channels{};
    AnimationMcVector<CameraFloat3ChannelRecord> camera_float3_channels{};
    AnimationMcVector<CameraQuaternionChannelRecord> camera_quaternion_channels{};

    AnimationMcVector<ecs::AnimationKeyframe<float>> scalar_keyframes{};
    AnimationMcVector<ecs::AnimationKeyframe<ecs::Float2>> float2_keyframes{};
    AnimationMcVector<ecs::AnimationKeyframe<ecs::Float3>> float3_keyframes{};
    AnimationMcVector<ecs::AnimationKeyframe<ecs::Float4>> float4_keyframes{};
    AnimationMcVector<ecs::AnimationKeyframe<ecs::Quaternion>> quaternion_keyframes{};
    AnimationMcVector<ecs::AnimationKeyframe<ecs::Rgba8>> color_keyframes{};

    bool initialized = false;
};

} // namespace vr::animation


