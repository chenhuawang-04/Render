#pragma once

#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_curve_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <algorithm>
#include <cstdint>

namespace vr::ecs {

template<DimensionTag DimensionT>
class AnimationPropertyTrackSystem final {
public:
    using AnimationType = Animation<DimensionT, PropertyTrack>;
    using TransformType = Transform<DimensionT>;
    using CameraType = Camera<DimensionT>;
    using TextType = Text<DimensionT>;
    using ClockSystem = AnimationClockSystem<DimensionT, PropertyTrack>;

    static void Initialize(AnimationType& component_) noexcept {
        ClockSystem::InitializeCommon(component_);
        SetDefaultBinding(component_);
        SetDefaultSample(component_);
    }

    static void SetDefaultBinding(AnimationType& component_) noexcept {
        component_.binding.target = AnimationTargetRef{
            .entity_id = 0U,
            .slot = 0U,
            .domain = AnimationTargetDomain::none,
            .reserved0 = 0U,
            .sub_index = 0U,
        };
        component_.binding.semantic = PropertyTrackSemantic::none;
        component_.binding.value_encoding = AnimationValueEncoding::scalar;
        component_.binding.channel_mask = 0xFFFFU;
        component_.binding.reserved0 = 0U;
        component_.binding.binding_handle = 0U;
    }

    static void SetDefaultSample(AnimationType& component_) noexcept {
        component_.sample.value = Float4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        component_.sample.rotation_value = Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        component_.sample.interpolation_mode = AnimationInterpolationMode::linear;
        component_.sample.value_encoding = AnimationValueEncoding::scalar;
        component_.sample.channel_mask = 0xFFFFU;
        component_.sample.reserved0 = 0U;
    }

    static void SetTarget(AnimationType& component_,
                          AnimationTargetRef target_) noexcept {
        if (component_.binding.target.entity_id == target_.entity_id &&
            component_.binding.target.slot == target_.slot &&
            component_.binding.target.domain == target_.domain &&
            component_.binding.target.sub_index == target_.sub_index) {
            return;
        }
        component_.binding.target = target_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetSemantic(AnimationType& component_,
                            PropertyTrackSemantic semantic_) noexcept {
        if (component_.binding.semantic == semantic_) {
            return;
        }
        component_.binding.semantic = semantic_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetValueEncoding(AnimationType& component_,
                                 AnimationValueEncoding value_encoding_) noexcept {
        if (component_.binding.value_encoding == value_encoding_) {
            return;
        }
        component_.binding.value_encoding = value_encoding_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetChannelMask(AnimationType& component_,
                               std::uint16_t channel_mask_) noexcept {
        if (component_.binding.channel_mask == channel_mask_) {
            return;
        }
        component_.binding.channel_mask = channel_mask_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetBindingHandle(AnimationType& component_,
                                 std::uint32_t binding_handle_) noexcept {
        if (component_.binding.binding_handle == binding_handle_) {
            return;
        }
        component_.binding.binding_handle = binding_handle_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SampleScalarCurve(AnimationType& component_,
                                  const AnimationCurveView<float>& curve_) noexcept {
        const AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        AnimationCurveCursor mutable_cursor = cursor;
        const float sampled = AnimationCurveSystem::Sample(curve_,
                                                           component_.playback.time_s,
                                                           &mutable_cursor);
        component_.runtime.curve_hint_index = mutable_cursor.segment_index;
        component_.sample.value = Float4{.x = sampled, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        component_.sample.value_encoding = AnimationValueEncoding::scalar;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleFloat2Curve(AnimationType& component_,
                                  const AnimationCurveView<Float2>& curve_) noexcept {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        const Float2 sampled = AnimationCurveSystem::Sample(curve_,
                                                            component_.playback.time_s,
                                                            &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        component_.sample.value = Float4{.x = sampled.x, .y = sampled.y, .z = 0.0F, .w = 0.0F};
        component_.sample.value_encoding = AnimationValueEncoding::float2;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleFloat3Curve(AnimationType& component_,
                                  const AnimationCurveView<Float3>& curve_) noexcept {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        const Float3 sampled = AnimationCurveSystem::Sample(curve_,
                                                            component_.playback.time_s,
                                                            &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        component_.sample.value = Float4{.x = sampled.x, .y = sampled.y, .z = sampled.z, .w = 0.0F};
        component_.sample.value_encoding = AnimationValueEncoding::float3;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleFloat4Curve(AnimationType& component_,
                                  const AnimationCurveView<Float4>& curve_) noexcept {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        const Float4 sampled = AnimationCurveSystem::Sample(curve_,
                                                            component_.playback.time_s,
                                                            &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        component_.sample.value = sampled;
        component_.sample.value_encoding = AnimationValueEncoding::float4;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleQuaternionCurve(AnimationType& component_,
                                      const AnimationCurveView<Quaternion>& curve_) noexcept {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        component_.sample.rotation_value = AnimationCurveSystem::Sample(curve_,
                                                                        component_.playback.time_s,
                                                                        &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        component_.sample.value_encoding = AnimationValueEncoding::quaternion;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleColorCurve(AnimationType& component_,
                                 const AnimationCurveView<Rgba8>& curve_) noexcept {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        const Rgba8 sampled = AnimationCurveSystem::Sample(curve_,
                                                           component_.playback.time_s,
                                                           &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        component_.sample.value = EncodeColor(sampled);
        component_.sample.value_encoding = AnimationValueEncoding::color_rgba8;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    [[nodiscard]] static bool ApplyToTransform(AnimationType& component_,
                                               TransformType& transform_) noexcept {
        switch (component_.binding.semantic) {
            case PropertyTrackSemantic::transform_local_position:
                if constexpr (std::same_as<DimensionT, Dim2>) {
                    TransformSystem<DimensionT>::SetLocalPosition(transform_,
                                                                  component_.sample.value.x,
                                                                  component_.sample.value.y);
                } else {
                    TransformSystem<DimensionT>::SetLocalPosition(transform_,
                                                                  MaskedFloat3(transform_.style.position,
                                                                               component_.sample.value,
                                                                               component_.binding.channel_mask));
                }
                return true;
            case PropertyTrackSemantic::transform_local_rotation:
                if constexpr (std::same_as<DimensionT, Dim2>) {
                    TransformSystem<DimensionT>::SetLocalRotationRadians(transform_,
                                                                         component_.sample.value.x);
                } else {
                    TransformSystem<DimensionT>::SetLocalRotationQuaternion(transform_,
                                                                            component_.sample.rotation_value);
                }
                return true;
            case PropertyTrackSemantic::transform_local_scale:
                if constexpr (std::same_as<DimensionT, Dim2>) {
                    const Float2 scale = MaskedFloat2(transform_.style.scale,
                                                      component_.sample.value,
                                                      component_.binding.channel_mask);
                    TransformSystem<DimensionT>::SetLocalScale(transform_, scale.x, scale.y);
                } else {
                    TransformSystem<DimensionT>::SetLocalScale(transform_,
                                                               MaskedFloat3(transform_.style.scale,
                                                                            component_.sample.value,
                                                                            component_.binding.channel_mask));
                }
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static bool ApplyToCamera(AnimationType& component_,
                                            CameraType& camera_) noexcept {
        switch (component_.binding.semantic) {
            case PropertyTrackSemantic::camera_zoom:
                if constexpr (std::same_as<DimensionT, Dim2>) {
                    CameraSystem<DimensionT>::SetZoom(camera_, component_.sample.value.x);
                    return true;
                }
                return false;
            case PropertyTrackSemantic::camera_orthographic_height:
                CameraSystem<DimensionT>::SetOrthographicHeight(camera_, component_.sample.value.x);
                return true;
            case PropertyTrackSemantic::camera_vertical_fov:
                if constexpr (std::same_as<DimensionT, Dim3>) {
                    CameraSystem<DimensionT>::SetVerticalFovRadians(camera_, component_.sample.value.x);
                    return true;
                }
                return false;
            default:
                return false;
        }
    }

    [[nodiscard]] static bool ApplyToText(AnimationType& component_,
                                          TextType& text_) noexcept {
        switch (component_.binding.semantic) {
            case PropertyTrackSemantic::text_color:
                TextSystem<DimensionT>::SetColor(text_, DecodeColor(component_.sample.value));
                return true;
            case PropertyTrackSemantic::text_outline_color:
                TextSystem<DimensionT>::SetOutlineColor(text_, DecodeColor(component_.sample.value));
                return true;
            default:
                return false;
        }
    }

private:
    [[nodiscard]] static Float4 EncodeColor(Rgba8 color_) noexcept {
        return Float4{
            .x = static_cast<float>(color_.r) / 255.0F,
            .y = static_cast<float>(color_.g) / 255.0F,
            .z = static_cast<float>(color_.b) / 255.0F,
            .w = static_cast<float>(color_.a) / 255.0F,
        };
    }

    [[nodiscard]] static Rgba8 DecodeColor(const Float4& color_) noexcept {
        const auto to_channel = [](float value_) noexcept -> std::uint8_t {
            const float clamped = std::clamp(value_, 0.0F, 1.0F);
            return static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
        };

        return Rgba8{
            .r = to_channel(color_.x),
            .g = to_channel(color_.y),
            .b = to_channel(color_.z),
            .a = to_channel(color_.w),
        };
    }

    [[nodiscard]] static Float2 MaskedFloat2(const Float2& current_,
                                             const Float4& sampled_,
                                             std::uint16_t mask_) noexcept {
        const std::uint16_t mask = (mask_ == 0U) ? 0x3U : mask_;
        return Float2{
            .x = (mask & 0x1U) != 0U ? sampled_.x : current_.x,
            .y = (mask & 0x2U) != 0U ? sampled_.y : current_.y,
        };
    }

    [[nodiscard]] static Float3 MaskedFloat3(const Float3& current_,
                                             const Float4& sampled_,
                                             std::uint16_t mask_) noexcept {
        const std::uint16_t mask = (mask_ == 0U) ? 0x7U : mask_;
        return Float3{
            .x = (mask & 0x1U) != 0U ? sampled_.x : current_.x,
            .y = (mask & 0x2U) != 0U ? sampled_.y : current_.y,
            .z = (mask & 0x4U) != 0U ? sampled_.z : current_.z,
        };
    }
};

} // namespace vr::ecs
