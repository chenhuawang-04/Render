#pragma once

#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/component/appearance_component.hpp"
#include "vr/ecs/component/surface_component.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_curve_system.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/surface_system.hpp"

#include <algorithm>
#include <cstdint>

namespace vr::ecs {

template<DimensionTag DimensionT>
class AnimationMaterialTrackSystem final {
public:
    using AnimationType = Animation<DimensionT, MaterialTrack>;
    using SurfaceType = Surface<DimensionT>;
    using AppearanceType = Appearance<DimensionT>;
    using ClockSystem = AnimationClockSystem<DimensionT, MaterialTrack>;

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
        component_.binding.semantic = MaterialTrackSemantic::none;
        component_.binding.value_encoding = AnimationValueEncoding::float4;
        component_.binding.channel_mask = 0xFFFFU;
        component_.binding.reserved0 = 0U;
        component_.binding.binding_handle = 0U;
    }

    static void SetDefaultSample(AnimationType& component_) noexcept {
        component_.sample.value = Float4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        component_.sample.semantic = MaterialTrackSemantic::none;
        component_.sample.interpolation_mode = AnimationInterpolationMode::linear;
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
        ResetBindingCache(component_);
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetSemantic(AnimationType& component_,
                            MaterialTrackSemantic semantic_) noexcept {
        if (component_.binding.semantic == semantic_) {
            return;
        }
        component_.binding.semantic = semantic_;
        ResetBindingCache(component_);
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetValueEncoding(AnimationType& component_,
                                 AnimationValueEncoding value_encoding_) noexcept {
        if (component_.binding.value_encoding == value_encoding_) {
            return;
        }
        component_.binding.value_encoding = value_encoding_;
        ResetBindingCache(component_);
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetChannelMask(AnimationType& component_,
                               std::uint16_t channel_mask_) noexcept {
        if (component_.binding.channel_mask == channel_mask_) {
            return;
        }
        component_.binding.channel_mask = channel_mask_;
        ResetBindingCache(component_);
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SampleScalarCurve(AnimationType& component_,
                                  const AnimationCurveView<float>& curve_) noexcept {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        const float sampled = AnimationCurveSystem::Sample(curve_,
                                                           component_.playback.time_s,
                                                           &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        component_.sample.value = Float4{.x = sampled, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleFloat4Curve(AnimationType& component_,
                                  const AnimationCurveView<Float4>& curve_) noexcept {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        component_.sample.value = AnimationCurveSystem::Sample(curve_,
                                                               component_.playback.time_s,
                                                               &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
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
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    [[nodiscard]] static bool ApplyToSurface(AnimationType& component_,
                                             SurfaceType& surface_) noexcept {
        switch (component_.binding.semantic) {
            case MaterialTrackSemantic::surface_uv_rect:
                if constexpr (std::same_as<DimensionT, Dim2>) {
                    SurfaceSystem<DimensionT>::SetUvRect(surface_,
                                                         component_.sample.value.x,
                                                         component_.sample.value.y,
                                                         component_.sample.value.z,
                                                         component_.sample.value.w);
                    return true;
                }
                return false;
            case MaterialTrackSemantic::surface_uv_transform:
                if constexpr (std::same_as<DimensionT, Dim3>) {
                    SurfaceSystem<DimensionT>::SetUvTransform(surface_,
                                                              component_.sample.value.x,
                                                              component_.sample.value.y,
                                                              component_.sample.value.z,
                                                              component_.sample.value.w);
                    return true;
                }
                return false;
            case MaterialTrackSemantic::surface_tint_color:
                SurfaceSystem<DimensionT>::SetTintColor(surface_, DecodeColor(component_.sample.value));
                return true;
            case MaterialTrackSemantic::surface_opacity:
                SurfaceSystem<DimensionT>::SetOpacity(surface_, component_.sample.value.x);
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static bool ApplyToAppearance(AnimationType& component_,
                                                AppearanceType& appearance_) noexcept {
        switch (component_.binding.semantic) {
            case MaterialTrackSemantic::appearance_color:
                if constexpr (std::same_as<DimensionT, Dim2>) {
                    AppearanceSystem<DimensionT>::SetFillColor(appearance_,
                                                               DecodeColor(component_.sample.value));
                } else {
                    AppearanceSystem<DimensionT>::SetBaseColor(appearance_,
                                                               DecodeColor(component_.sample.value));
                }
                return true;
            case MaterialTrackSemantic::appearance_opacity:
                AppearanceSystem<DimensionT>::SetOpacity(appearance_, component_.sample.value.x);
                return true;
            case MaterialTrackSemantic::appearance_emissive_color:
                if constexpr (std::same_as<DimensionT, Dim3>) {
                    AppearanceSystem<DimensionT>::SetEmissiveColor(appearance_,
                                                                   DecodeColor(component_.sample.value));
                    return true;
                }
                return false;
            case MaterialTrackSemantic::appearance_emissive_intensity:
                if constexpr (std::same_as<DimensionT, Dim3>) {
                    AppearanceSystem<DimensionT>::SetEmissiveIntensity(appearance_,
                                                                       component_.sample.value.x);
                    return true;
                }
                return false;
            default:
                return false;
        }
    }

private:
    static void ResetBindingCache(AnimationType& component_) noexcept {
        component_.runtime.cached_channel_index = invalid_animation_handle_index;
        component_.runtime.cached_source_revision = 0U;
        component_.runtime.curve_hint_index = 0U;
    }

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
};

} // namespace vr::ecs
