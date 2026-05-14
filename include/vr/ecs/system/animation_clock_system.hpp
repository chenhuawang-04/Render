#pragma once

#include "vr/ecs/component/animation_component.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace vr::ecs {

[[nodiscard]] constexpr std::uint32_t NextAnimationRevision(std::uint32_t current_revision_) noexcept {
    return (current_revision_ == (std::numeric_limits<std::uint32_t>::max)()) ? 1U : (current_revision_ + 1U);
}

template<DimensionTag DimensionT, typename KindT>
class AnimationClockSystem final {
public:
    using AnimationType = Animation<DimensionT, KindT>;

    static void InitializeCommon(AnimationType& component_) noexcept {
        SetDefaultPlayback(component_);
        SetDefaultRuntime(component_);
    }

    static void SetDefaultPlayback(AnimationType& component_) noexcept {
        component_.playback.clip_handle = invalid_animation_clip_handle;
        component_.playback.time_s = 0.0F;
        component_.playback.duration_s = 1.0F;
        component_.playback.speed = 1.0F;
        component_.playback.weight = 1.0F;
        component_.playback.layer = 0U;
        component_.playback.loop_mode = AnimationLoopMode::loop;
        component_.playback.state_flags = animation_playing_flag | animation_auto_start_flag;
    }

    static void SetDefaultRuntime(AnimationType& component_) noexcept {
        component_.runtime.revision_playback = 1U;
        component_.runtime.revision_binding = 1U;
        component_.runtime.sample_revision = 0U;
        component_.runtime.dirty_flags = animation_dirty_playback_flag |
                                         animation_dirty_binding_flag |
                                         animation_dirty_sample_flag |
                                         animation_dirty_runtime_flag;
        component_.runtime.curve_hint_index = 0U;
        component_.runtime.cached_channel_index = invalid_animation_handle_index;
        component_.runtime.cached_source_revision = 0U;
        component_.runtime.reserved0 = 0U;
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const AnimationType& component_) noexcept {
        return component_.runtime.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const AnimationType& component_,
                                            std::uint32_t dirty_mask_) noexcept {
        return (component_.runtime.dirty_flags & dirty_mask_) != 0U;
    }

    static void MarkDirty(AnimationType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.runtime.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(AnimationType& component_, std::uint32_t clear_mask_) noexcept {
        component_.runtime.dirty_flags &= ~clear_mask_;
    }

    [[nodiscard]] static bool IsPlaying(const AnimationType& component_) noexcept {
        return (component_.playback.state_flags & animation_playing_flag) != 0U;
    }

    [[nodiscard]] static bool IsCompleted(const AnimationType& component_) noexcept {
        return (component_.playback.state_flags & animation_completed_flag) != 0U;
    }

    [[nodiscard]] static bool IsReversed(const AnimationType& component_) noexcept {
        return (component_.playback.state_flags & animation_reverse_flag) != 0U;
    }

    [[nodiscard]] static bool IsPingPongBackward(const AnimationType& component_) noexcept {
        return (component_.playback.state_flags & animation_ping_pong_backward_flag) != 0U;
    }

    [[nodiscard]] static float TimeSeconds(const AnimationType& component_) noexcept {
        return component_.playback.time_s;
    }

    [[nodiscard]] static float DurationSeconds(const AnimationType& component_) noexcept {
        return component_.playback.duration_s;
    }

    [[nodiscard]] static float Weight(const AnimationType& component_) noexcept {
        return component_.playback.weight;
    }

    [[nodiscard]] static float Speed(const AnimationType& component_) noexcept {
        return component_.playback.speed;
    }

    static void SetClipHandle(AnimationType& component_,
                              AnimationClipHandle clip_handle_) noexcept {
        if (component_.playback.clip_handle.index == clip_handle_.index &&
            component_.playback.clip_handle.generation == clip_handle_.generation) {
            return;
        }
        component_.playback.clip_handle = clip_handle_;
        component_.runtime.curve_hint_index = 0U;
        component_.runtime.cached_channel_index = invalid_animation_handle_index;
        component_.runtime.cached_source_revision = 0U;
        MarkPlaybackRevisionDirty(component_);
    }

    static void SetDurationSeconds(AnimationType& component_,
                                   float duration_s_) noexcept {
        const float clamped = std::max(1e-6F, duration_s_);
        if (component_.playback.duration_s == clamped) {
            return;
        }
        component_.playback.duration_s = clamped;
        if (component_.playback.time_s > clamped) {
            component_.playback.time_s = clamped;
            component_.runtime.curve_hint_index = 0U;
        }
        MarkPlaybackRevisionDirty(component_);
    }

    static void SetTimeSeconds(AnimationType& component_,
                               float time_s_) noexcept {
        const float clamped = std::clamp(time_s_, 0.0F, std::max(1e-6F, component_.playback.duration_s));
        if (component_.playback.time_s == clamped) {
            return;
        }
        const bool moved_backward = clamped < component_.playback.time_s;
        component_.playback.time_s = clamped;
        if (moved_backward) {
            component_.runtime.curve_hint_index = 0U;
        }
        component_.playback.state_flags &= ~animation_completed_flag;
        MarkPlaybackRevisionDirty(component_);
    }

    static void SetSpeed(AnimationType& component_,
                         float speed_) noexcept {
        const float clamped = std::max(0.0F, speed_);
        if (component_.playback.speed == clamped) {
            return;
        }
        component_.playback.speed = clamped;
        MarkPlaybackRevisionDirty(component_);
    }

    static void SetWeight(AnimationType& component_,
                          float weight_) noexcept {
        const float clamped = std::clamp(weight_, 0.0F, 1.0F);
        if (component_.playback.weight == clamped) {
            return;
        }
        component_.playback.weight = clamped;
        MarkPlaybackRevisionDirty(component_);
    }

    static void SetLayer(AnimationType& component_,
                         std::uint16_t layer_) noexcept {
        if (component_.playback.layer == layer_) {
            return;
        }
        component_.playback.layer = layer_;
        MarkPlaybackRevisionDirty(component_);
    }

    static void SetLoopMode(AnimationType& component_,
                            AnimationLoopMode loop_mode_) noexcept {
        if (component_.playback.loop_mode == loop_mode_) {
            return;
        }
        component_.playback.loop_mode = loop_mode_;
        component_.playback.state_flags &= ~animation_ping_pong_backward_flag;
        MarkPlaybackRevisionDirty(component_);
    }

    static void SetReverse(AnimationType& component_,
                           bool reverse_) noexcept {
        const std::uint8_t mask = animation_reverse_flag;
        const bool current = (component_.playback.state_flags & mask) != 0U;
        if (current == reverse_) {
            return;
        }
        if (reverse_) {
            component_.playback.state_flags |= mask;
        } else {
            component_.playback.state_flags &= ~mask;
        }
        MarkPlaybackRevisionDirty(component_);
    }

    static void Play(AnimationType& component_) noexcept {
        if ((component_.playback.state_flags & animation_playing_flag) != 0U &&
            (component_.playback.state_flags & animation_completed_flag) == 0U) {
            return;
        }
        component_.playback.state_flags |= animation_playing_flag;
        component_.playback.state_flags &= ~animation_completed_flag;
        MarkPlaybackRevisionDirty(component_);
    }

    static void Pause(AnimationType& component_) noexcept {
        if ((component_.playback.state_flags & animation_playing_flag) == 0U) {
            return;
        }
        component_.playback.state_flags &= ~animation_playing_flag;
        MarkPlaybackRevisionDirty(component_);
    }

    static void Stop(AnimationType& component_) noexcept {
        const bool was_nonzero = component_.playback.time_s != 0.0F;
        const bool was_playing = (component_.playback.state_flags & animation_playing_flag) != 0U;
        const bool had_state_bits =
            (component_.playback.state_flags & (animation_completed_flag | animation_ping_pong_backward_flag)) != 0U;
        if (!was_nonzero && !was_playing && !had_state_bits) {
            return;
        }
        component_.playback.time_s = 0.0F;
        component_.playback.state_flags &= ~(animation_playing_flag |
                                             animation_completed_flag |
                                             animation_ping_pong_backward_flag);
        component_.runtime.curve_hint_index = 0U;
        component_.runtime.cached_channel_index = invalid_animation_handle_index;
        component_.runtime.cached_source_revision = 0U;
        MarkPlaybackRevisionDirty(component_);
    }

    static void Restart(AnimationType& component_) noexcept {
        component_.playback.time_s = 0.0F;
        component_.playback.state_flags |= animation_playing_flag;
        component_.playback.state_flags &= ~(animation_completed_flag | animation_ping_pong_backward_flag);
        component_.runtime.curve_hint_index = 0U;
        component_.runtime.cached_channel_index = invalid_animation_handle_index;
        component_.runtime.cached_source_revision = 0U;
        MarkPlaybackRevisionDirty(component_);
    }

    [[nodiscard]] static bool Advance(AnimationType& component_,
                                      float delta_time_s_) noexcept {
        if (!IsPlaying(component_)) {
            return false;
        }

        const float duration_s = std::max(1e-6F, component_.playback.duration_s);
        const float speed = std::max(0.0F, component_.playback.speed);
        if (delta_time_s_ == 0.0F || speed == 0.0F) {
            return false;
        }

        const float previous_time = component_.playback.time_s;
        float time_s = previous_time;
        float remaining = delta_time_s_ * speed;
        bool wrapped = false;

        while (remaining > 0.0F) {
            const bool moving_backward = EffectiveBackward(component_);
            const float boundary = moving_backward ? 0.0F : duration_s;
            const float distance_to_boundary = moving_backward
                ? (time_s - boundary)
                : (boundary - time_s);

            if (remaining < distance_to_boundary) {
                time_s += moving_backward ? -remaining : remaining;
                remaining = 0.0F;
                break;
            }

            time_s = boundary;
            remaining -= distance_to_boundary;
            wrapped = true;

            switch (component_.playback.loop_mode) {
                case AnimationLoopMode::once:
                    component_.playback.state_flags &= ~animation_playing_flag;
                    component_.playback.state_flags |= animation_completed_flag;
                    remaining = 0.0F;
                    break;
                case AnimationLoopMode::loop:
                    time_s = moving_backward ? duration_s : 0.0F;
                    break;
                case AnimationLoopMode::ping_pong:
                    TogglePingPongDirection(component_);
                    break;
            }
        }

        if (time_s == previous_time && !wrapped) {
            return false;
        }

        component_.playback.time_s = std::clamp(time_s, 0.0F, duration_s);
        if (wrapped || component_.playback.time_s < previous_time) {
            component_.runtime.curve_hint_index = 0U;
            component_.runtime.cached_channel_index = invalid_animation_handle_index;
        }
        MarkPlaybackRevisionDirty(component_);
        return true;
    }

    static void MarkBindingRevisionDirty(AnimationType& component_) noexcept {
        component_.runtime.revision_binding = NextAnimationRevision(component_.runtime.revision_binding);
        MarkDirty(component_, animation_dirty_binding_flag | animation_dirty_runtime_flag);
    }

    static void MarkSampleRevisionDirty(AnimationType& component_) noexcept {
        component_.runtime.sample_revision = NextAnimationRevision(component_.runtime.sample_revision);
        MarkDirty(component_, animation_dirty_sample_flag | animation_dirty_runtime_flag);
    }

private:
    [[nodiscard]] static bool EffectiveBackward(const AnimationType& component_) noexcept {
        const bool reverse = (component_.playback.state_flags & animation_reverse_flag) != 0U;
        const bool ping_pong_backward = (component_.playback.state_flags & animation_ping_pong_backward_flag) != 0U;
        return reverse != ping_pong_backward;
    }

    static void TogglePingPongDirection(AnimationType& component_) noexcept {
        component_.playback.state_flags ^= animation_ping_pong_backward_flag;
    }

    static void MarkPlaybackRevisionDirty(AnimationType& component_) noexcept {
        component_.runtime.revision_playback = NextAnimationRevision(component_.runtime.revision_playback);
        MarkDirty(component_,
                  animation_dirty_playback_flag |
                      animation_dirty_sample_flag |
                      animation_dirty_runtime_flag);
    }
};

} // namespace vr::ecs

