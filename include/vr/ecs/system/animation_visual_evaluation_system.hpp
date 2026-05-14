#pragma once

#include "vr/animation/animation_clip_host.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/animation_visual_track_system.hpp"

namespace vr::ecs {

template<DimensionTag DimensionT>
class AnimationVisualEvaluationSystem final {
public:
    using AnimationType = Animation<DimensionT, VisualTrack>;
    using TrackSystem = AnimationVisualTrackSystem<DimensionT>;
    using ClockSystem = AnimationClockSystem<DimensionT, VisualTrack>;

    [[nodiscard]] static bool Tick(AnimationType& component_,
                                   const animation::AnimationClipHost& clip_host_,
                                   AnimationEvaluationContext<DimensionT>& context_,
                                   float delta_time_s_) noexcept {
        if (delta_time_s_ != 0.0F) {
            (void)ClockSystem::Advance(component_, delta_time_s_);
        }
        if (!SampleFromClip(component_, clip_host_)) {
            return false;
        }
        return Apply(component_, context_);
    }

    [[nodiscard]] static bool SampleFromClip(AnimationType& component_,
                                             const animation::AnimationClipHost& clip_host_) noexcept {
        const animation::AnimationClipRecord* clip = clip_host_.FindVisualClipByHandle(component_.playback.clip_handle);
        if (clip == nullptr) {
            return false;
        }

        const AnimationValueEncoding preferred_encoding = component_.binding.value_encoding;
        if (preferred_encoding == AnimationValueEncoding::scalar &&
            SampleChannels(component_, clip_host_.VisualScalarChannels(*clip), clip->visual.scalar.count, clip_host_)) {
            return true;
        }
        if (preferred_encoding == AnimationValueEncoding::float4 &&
            SampleChannels(component_, clip_host_.VisualFloat4Channels(*clip), clip->visual.float4.count, clip_host_)) {
            return true;
        }
        if (preferred_encoding == AnimationValueEncoding::color_rgba8 &&
            SampleChannels(component_, clip_host_.VisualColorChannels(*clip), clip->visual.color.count, clip_host_)) {
            return true;
        }

        if (SampleChannels(component_, clip_host_.VisualScalarChannels(*clip), clip->visual.scalar.count, clip_host_)) {
            return true;
        }
        if (SampleChannels(component_, clip_host_.VisualFloat4Channels(*clip), clip->visual.float4.count, clip_host_)) {
            return true;
        }
        return SampleChannels(component_, clip_host_.VisualColorChannels(*clip), clip->visual.color.count, clip_host_);
    }

    [[nodiscard]] static bool Apply(AnimationType& component_,
                                    AnimationEvaluationContext<DimensionT>& context_) noexcept {
        switch (component_.binding.target.domain) {
            case AnimationTargetDomain::surface: {
                auto* surface = context_.surfaces.Resolve(component_.binding.target.entity_id);
                return surface != nullptr && TrackSystem::ApplyToSurface(component_, *surface);
            }
            case AnimationTargetDomain::appearance: {
                auto* appearance = context_.appearances.Resolve(component_.binding.target.entity_id);
                return appearance != nullptr && TrackSystem::ApplyToAppearance(component_, *appearance);
            }
            default:
                return false;
        }
    }

private:
    template<typename ChannelRecordT>
    [[nodiscard]] static bool SampleChannels(AnimationType& component_,
                                             const ChannelRecordT* channels_,
                                             std::uint32_t channel_count_,
                                             const animation::AnimationClipHost& clip_host_) noexcept {
        if (channels_ == nullptr || channel_count_ == 0U) {
            return false;
        }
        for (std::uint32_t i = 0U; i < channel_count_; ++i) {
            if (channels_[i].semantic != component_.binding.semantic) {
                continue;
            }
            SampleSingle(component_, channels_[i], clip_host_);
            return true;
        }
        return false;
    }

    static void SampleSingle(AnimationType& component_,
                             const animation::VisualScalarChannelRecord& channel_,
                             const animation::AnimationClipHost& clip_host_) noexcept {
        TrackSystem::SampleScalarCurve(component_, clip_host_.BuildCurveView(channel_));
        component_.sample.channel_mask = channel_.channel_mask;
    }

    static void SampleSingle(AnimationType& component_,
                             const animation::VisualFloat4ChannelRecord& channel_,
                             const animation::AnimationClipHost& clip_host_) noexcept {
        TrackSystem::SampleFloat4Curve(component_, clip_host_.BuildCurveView(channel_));
        component_.sample.channel_mask = channel_.channel_mask;
    }

    static void SampleSingle(AnimationType& component_,
                             const animation::VisualColorChannelRecord& channel_,
                             const animation::AnimationClipHost& clip_host_) noexcept {
        TrackSystem::SampleColorCurve(component_, clip_host_.BuildCurveView(channel_));
        component_.sample.channel_mask = channel_.channel_mask;
    }
};

} // namespace vr::ecs


