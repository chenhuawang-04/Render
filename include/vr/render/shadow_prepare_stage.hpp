#pragma once

#include "vr/ecs/system/shadow_caster_system.hpp"
#include "vr/ecs/system/shadow_runtime_system.hpp"

#include <concepts>
#include <cstdint>

namespace vr::render {

template<ecs::DimensionTag DimensionT>
struct ShadowPrepareStageResult final {
    ecs::ShadowRuntimeBuildStats runtime_stats{};
    ecs::ShadowCasterBuildStats caster_stats{};
    bool has_shadow_data = false;
    bool runtime_build_invoked = false;
    bool caster_build_invoked = false;
};

template<ecs::DimensionTag DimensionT>
class ShadowPrepareStage final {
public:
    using ShadowType = ecs::Shadow<DimensionT>;
    using TransformType = ecs::Transform<DimensionT>;
    using CameraType = ecs::Camera<DimensionT>;
    using BoundsType = ecs::Bounds<DimensionT>;

    using RuntimeSystemType = ecs::ShadowRuntimeSystem<DimensionT>;
    using RuntimeScratchType = ecs::ShadowRuntimeScratch<DimensionT>;
    using RuntimeBuildConfigType = ecs::ShadowRuntimeBuildConfig;
    using RuntimeBuildHintType = ecs::ShadowRuntimeBuildHint;

    using CasterSystemType = ecs::ShadowCasterSystem<DimensionT>;
    using CasterScratchType = ecs::ShadowCasterScratch<DimensionT>;
    using CasterBuildConfigType = ecs::ShadowCasterBuildConfig;

    static void Reserve(RuntimeScratchType& runtime_scratch_,
                        CasterScratchType& caster_scratch_,
                        std::uint32_t shadow_count_,
                        std::uint32_t caster_count_) {
        RuntimeSystemType::Reserve(runtime_scratch_, shadow_count_);
        CasterSystemType::Reserve(caster_scratch_, shadow_count_ * ecs::max_shadow_view_count, caster_count_);
    }

    [[nodiscard]] static ShadowPrepareStageResult<DimensionT> BuildRuntimeOnly(
        ShadowType* shadow_components_,
        const TransformType* transforms_,
        const CameraType* camera_component_,
        std::uint32_t shadow_count_,
        RuntimeScratchType& runtime_scratch_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        const std::uint32_t* transform_dirty_component_indices_,
        std::uint32_t transform_dirty_component_count_,
        const RuntimeBuildConfigType& runtime_config_ = RuntimeBuildConfigType{}) {
        ShadowPrepareStageResult<DimensionT> result{};
        if (shadow_components_ == nullptr || shadow_count_ == 0U) {
            return result;
        }

        RuntimeBuildHintType runtime_hint{};
        runtime_hint.dirty_component_indices = dirty_component_indices_;
        runtime_hint.dirty_component_count = dirty_component_count_;
        runtime_hint.use_dirty_component_indices =
            (dirty_component_indices_ != nullptr && dirty_component_count_ > 0U) ? 1U : 0U;
        runtime_hint.transform_dirty_component_indices = transform_dirty_component_indices_;
        runtime_hint.transform_dirty_component_count = transform_dirty_component_count_;
        runtime_hint.use_transform_dirty_component_indices =
            (transform_dirty_component_indices_ != nullptr && transform_dirty_component_count_ > 0U) ? 1U : 0U;

        result.runtime_stats = RuntimeSystemType::Build(shadow_components_,
                                                        transforms_,
                                                        camera_component_,
                                                        shadow_count_,
                                                        runtime_scratch_,
                                                        runtime_config_,
                                                        runtime_hint);
        result.has_shadow_data = true;
        result.runtime_build_invoked = true;
        return result;
    }

    [[nodiscard]] static ShadowPrepareStageResult<DimensionT> BuildRuntimeAndCaster(
        ShadowType* shadow_components_,
        const TransformType* transforms_,
        const CameraType* camera_component_,
        std::uint32_t shadow_count_,
        const BoundsType* caster_bounds_,
        std::uint32_t caster_count_,
        RuntimeScratchType& runtime_scratch_,
        CasterScratchType& caster_scratch_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        const std::uint32_t* transform_dirty_component_indices_,
        std::uint32_t transform_dirty_component_count_,
        const RuntimeBuildConfigType& runtime_config_ = RuntimeBuildConfigType{},
        const CasterBuildConfigType& caster_config_ = CasterBuildConfigType{}) {
        ShadowPrepareStageResult<DimensionT> result = BuildRuntimeOnly(shadow_components_,
                                                                       transforms_,
                                                                       camera_component_,
                                                                       shadow_count_,
                                                                       runtime_scratch_,
                                                                       dirty_component_indices_,
                                                                       dirty_component_count_,
                                                                       transform_dirty_component_indices_,
                                                                       transform_dirty_component_count_,
                                                                       runtime_config_);
        if (!result.has_shadow_data) {
            return result;
        }

        result.caster_stats = CasterSystemType::Build(shadow_components_,
                                                      shadow_count_,
                                                      RuntimeSystemType::ViewRecords(runtime_scratch_),
                                                      RuntimeSystemType::ViewRecordCount(runtime_scratch_),
                                                      caster_bounds_,
                                                      caster_count_,
                                                      caster_scratch_,
                                                      caster_config_);
        result.caster_build_invoked = true;
        return result;
    }
};

} // namespace vr::render
