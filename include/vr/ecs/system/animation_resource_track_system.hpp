#pragma once

#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"

#include <concepts>
#include <cstdint>

namespace vr::ecs {

template<typename KindT>
concept ResourceAnimationKind = std::same_as<KindT, Skeletal> ||
                                std::same_as<KindT, VertexDeform> ||
                                std::same_as<KindT, Morph> ||
                                std::same_as<KindT, FrameSequence> ||
                                std::same_as<KindT, PhysicsDriven> ||
                                std::same_as<KindT, ParametricGeometry> ||
                                std::same_as<KindT, ParticleSimulation>;

template<DimensionTag DimensionT, ResourceAnimationKind KindT>
class AnimationResourceTrackSystem final {
public:
    using AnimationType = Animation<DimensionT, KindT>;
    using ClockSystem = AnimationClockSystem<DimensionT, KindT>;

    static void Initialize(AnimationType& component_) noexcept {
        ClockSystem::InitializeCommon(component_);
        SetDefaultBinding(component_);
        SetDefaultSample(component_);
    }

    static void SetDefaultBinding(AnimationType& component_) noexcept {
        component_.binding.target = AnimationTargetRef{
            .entity_id = 0U,
            .slot = 0U,
            .domain = AnimationTargetDomain::custom,
            .reserved0 = 0U,
            .sub_index = 0U,
        };
        component_.binding.resource_handle = 0U;
        component_.binding.binding_handle = 0U;
        component_.binding.apply_flags = 0U;
        component_.binding.reserved0 = 0U;
    }

    static void SetDefaultSample(AnimationType& component_) noexcept {
        component_.sample.parameters0 = Float4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        component_.sample.parameters1 = Float4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
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

    static void SetResourceHandle(AnimationType& component_,
                                  std::uint32_t resource_handle_) noexcept {
        if (component_.binding.resource_handle == resource_handle_) {
            return;
        }
        component_.binding.resource_handle = resource_handle_;
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

    static void SetApplyFlags(AnimationType& component_,
                              std::uint32_t apply_flags_) noexcept {
        if (component_.binding.apply_flags == apply_flags_) {
            return;
        }
        component_.binding.apply_flags = apply_flags_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetSampleParameters(AnimationType& component_,
                                    const Float4& parameters0_,
                                    const Float4& parameters1_) noexcept {
        component_.sample.parameters0 = parameters0_;
        component_.sample.parameters1 = parameters1_;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }
};

} // namespace vr::ecs

