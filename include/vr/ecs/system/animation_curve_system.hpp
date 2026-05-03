#pragma once

#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/system/spatial_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace vr::ecs {

template<typename ValueT>
struct AnimationKeyframe final {
    float time_s;
    ValueT value;
    AnimationInterpolationMode interpolation_mode;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

template<typename ValueT>
struct AnimationCurveView final {
    const AnimationKeyframe<ValueT>* keyframes;
    std::uint32_t keyframe_count;
};

struct AnimationCurveCursor final {
    std::uint32_t segment_index;
};

namespace detail {

template<typename ValueT>
[[nodiscard]] inline ValueT DefaultAnimationValue() noexcept {
    return ValueT{};
}

template<>
[[nodiscard]] inline Quaternion DefaultAnimationValue<Quaternion>() noexcept {
    return Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
}

template<typename ValueT>
[[nodiscard]] inline ValueT LerpAnimationValue(const ValueT& lhs_,
                                               const ValueT& rhs_,
                                               float alpha_) noexcept {
    if constexpr (std::is_same_v<ValueT, float>) {
        return lhs_ + (rhs_ - lhs_) * alpha_;
    } else if constexpr (std::is_same_v<ValueT, Float2>) {
        return Float2{
            .x = lhs_.x + (rhs_.x - lhs_.x) * alpha_,
            .y = lhs_.y + (rhs_.y - lhs_.y) * alpha_,
        };
    } else if constexpr (std::is_same_v<ValueT, Float3>) {
        return Float3{
            .x = lhs_.x + (rhs_.x - lhs_.x) * alpha_,
            .y = lhs_.y + (rhs_.y - lhs_.y) * alpha_,
            .z = lhs_.z + (rhs_.z - lhs_.z) * alpha_,
        };
    } else if constexpr (std::is_same_v<ValueT, Float4>) {
        return Float4{
            .x = lhs_.x + (rhs_.x - lhs_.x) * alpha_,
            .y = lhs_.y + (rhs_.y - lhs_.y) * alpha_,
            .z = lhs_.z + (rhs_.z - lhs_.z) * alpha_,
            .w = lhs_.w + (rhs_.w - lhs_.w) * alpha_,
        };
    } else if constexpr (std::is_same_v<ValueT, Quaternion>) {
        Quaternion blended{
            .x = lhs_.x + (rhs_.x - lhs_.x) * alpha_,
            .y = lhs_.y + (rhs_.y - lhs_.y) * alpha_,
            .z = lhs_.z + (rhs_.z - lhs_.z) * alpha_,
            .w = lhs_.w + (rhs_.w - lhs_.w) * alpha_,
        };
        return spatial_math::NormalizeQuaternion(blended);
    } else if constexpr (std::is_same_v<ValueT, Rgba8>) {
        const auto lerp_channel = [alpha_](std::uint8_t lhs_channel_,
                                           std::uint8_t rhs_channel_) noexcept -> std::uint8_t {
            const float lhs_value = static_cast<float>(lhs_channel_);
            const float rhs_value = static_cast<float>(rhs_channel_);
            const float blended = lhs_value + (rhs_value - lhs_value) * alpha_;
            return static_cast<std::uint8_t>(std::clamp(blended, 0.0F, 255.0F) + 0.5F);
        };

        return Rgba8{
            .r = lerp_channel(lhs_.r, rhs_.r),
            .g = lerp_channel(lhs_.g, rhs_.g),
            .b = lerp_channel(lhs_.b, rhs_.b),
            .a = lerp_channel(lhs_.a, rhs_.a),
        };
    } else {
        static_assert(std::is_same_v<ValueT, void>, "Unsupported animation curve value type");
    }
}

template<typename ValueT>
[[nodiscard]] inline std::uint32_t FindCurveSegment(const AnimationCurveView<ValueT>& curve_,
                                                    float time_s_,
                                                    AnimationCurveCursor* cursor_) noexcept {
    if (curve_.keyframe_count <= 1U) {
        if (cursor_ != nullptr) {
            cursor_->segment_index = 0U;
        }
        return 0U;
    }

    const std::uint32_t segment_count = curve_.keyframe_count - 1U;
    if (cursor_ != nullptr) {
        const std::uint32_t hint = std::min(cursor_->segment_index, segment_count - 1U);
        const auto& hinted0 = curve_.keyframes[hint];
        const auto& hinted1 = curve_.keyframes[hint + 1U];
        if (time_s_ >= hinted0.time_s && time_s_ <= hinted1.time_s) {
            cursor_->segment_index = hint;
            return hint;
        }
    }

    std::uint32_t first = 0U;
    std::uint32_t last = curve_.keyframe_count - 1U;
    while (first + 1U < last) {
        const std::uint32_t mid = first + ((last - first) / 2U);
        if (curve_.keyframes[mid].time_s <= time_s_) {
            first = mid;
        } else {
            last = mid;
        }
    }

    const std::uint32_t resolved = std::min(first, segment_count - 1U);
    if (cursor_ != nullptr) {
        cursor_->segment_index = resolved;
    }
    return resolved;
}

} // namespace detail

class AnimationCurveSystem final {
public:
    template<typename ValueT>
    [[nodiscard]] static ValueT Sample(const AnimationCurveView<ValueT>& curve_,
                                       float time_s_,
                                       AnimationCurveCursor* cursor_ = nullptr) noexcept {
        if (curve_.keyframes == nullptr || curve_.keyframe_count == 0U) {
            return detail::DefaultAnimationValue<ValueT>();
        }

        if (curve_.keyframe_count == 1U || time_s_ <= curve_.keyframes[0U].time_s) {
            if (cursor_ != nullptr) {
                cursor_->segment_index = 0U;
            }
            return curve_.keyframes[0U].value;
        }

        const AnimationKeyframe<ValueT>& last_key = curve_.keyframes[curve_.keyframe_count - 1U];
        if (time_s_ >= last_key.time_s) {
            if (cursor_ != nullptr) {
                cursor_->segment_index = curve_.keyframe_count - 2U;
            }
            return last_key.value;
        }

        const std::uint32_t segment_index = detail::FindCurveSegment(curve_, time_s_, cursor_);
        const AnimationKeyframe<ValueT>& key0 = curve_.keyframes[segment_index];
        const AnimationKeyframe<ValueT>& key1 = curve_.keyframes[segment_index + 1U];

        if (key0.interpolation_mode == AnimationInterpolationMode::step ||
            key1.time_s <= key0.time_s) {
            return key0.value;
        }

        const float alpha = std::clamp((time_s_ - key0.time_s) / (key1.time_s - key0.time_s), 0.0F, 1.0F);
        return detail::LerpAnimationValue<ValueT>(key0.value, key1.value, alpha);
    }
};

static_assert(std::is_standard_layout_v<AnimationCurveCursor> &&
              std::is_trivial_v<AnimationCurveCursor>);
static_assert(std::is_standard_layout_v<AnimationKeyframe<float>> &&
              std::is_trivial_v<AnimationKeyframe<float>>);
static_assert(std::is_standard_layout_v<AnimationKeyframe<Float3>> &&
              std::is_trivial_v<AnimationKeyframe<Float3>>);
static_assert(std::is_standard_layout_v<AnimationCurveView<Float4>> &&
              std::is_trivial_v<AnimationCurveView<Float4>>);

} // namespace vr::ecs
