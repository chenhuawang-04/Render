#pragma once

#include "vr/ecs/system/light_culling_system.hpp"
#include "vr/ecs/system/light_runtime_system.hpp"

#include <concepts>
#include <cstdint>

namespace vr::render {

template<ecs::DimensionTag DimensionT>
struct LightPrepareStageResult final {
    ecs::LightRuntimeBuildStats runtime_stats{};
    ecs::LightCullingBuildStats culling_stats{};
    bool has_light_data = false;
    bool runtime_build_invoked = false;
    bool culling_build_invoked = false;
};

template<ecs::DimensionTag DimensionT>
class LightPrepareStage final {
public:
    using LightType = ecs::Light<DimensionT>;
    using TransformType = ecs::Transform<DimensionT>;
    using CameraType = ecs::Camera<DimensionT>;

    using RuntimeSystemType = ecs::LightRuntimeSystem<DimensionT>;
    using RuntimeScratchType = ecs::LightRuntimeScratch<DimensionT>;
    using RuntimeBuildConfigType = ecs::LightRuntimeBuildConfig;
    using RuntimeBuildHintType = ecs::LightRuntimeBuildHint;

    using CullingSystemType = ecs::LightCullingSystem<DimensionT>;
    using CullingScratchType = ecs::LightCullingScratch<DimensionT>;
    using CullingBuildConfigType = ecs::LightCullingBuildConfig<DimensionT>;

    static void Reserve(RuntimeScratchType& runtime_scratch_,
                        CullingScratchType& culling_scratch_,
                        std::uint32_t light_count_,
                        const CullingBuildConfigType& culling_config_ =
                            CullingSystemType::DefaultBuildConfig()) {
        RuntimeSystemType::Reserve(runtime_scratch_, light_count_);
        CullingSystemType::Reserve(culling_scratch_, light_count_, culling_config_);
    }

    [[nodiscard]] static LightPrepareStageResult<DimensionT> BuildRuntimeOnly(
        LightType* light_components_,
        const TransformType* transforms_,
        std::uint32_t light_count_,
        RuntimeScratchType& runtime_scratch_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        const std::uint32_t* transform_dirty_component_indices_,
        std::uint32_t transform_dirty_component_count_,
        const RuntimeBuildConfigType& runtime_config_ = RuntimeBuildConfigType{}) {
        LightPrepareStageResult<DimensionT> result{};
        if (light_components_ == nullptr || light_count_ == 0U) {
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

        result.runtime_stats = RuntimeSystemType::Build(light_components_,
                                                        transforms_,
                                                        light_count_,
                                                        runtime_scratch_,
                                                        runtime_config_,
                                                        runtime_hint);
        result.has_light_data = true;
        result.runtime_build_invoked = true;
        return result;
    }

    [[nodiscard]] static LightPrepareStageResult<DimensionT> BuildRuntimeAndCulling(
        LightType* light_components_,
        const TransformType* transforms_,
        std::uint32_t light_count_,
        const CameraType* camera_component_,
        RuntimeScratchType& runtime_scratch_,
        CullingScratchType& culling_scratch_,
        const std::uint32_t* dirty_component_indices_,
        std::uint32_t dirty_component_count_,
        const std::uint32_t* transform_dirty_component_indices_,
        std::uint32_t transform_dirty_component_count_,
        const RuntimeBuildConfigType& runtime_config_ = RuntimeBuildConfigType{},
        const CullingBuildConfigType& culling_config_ = CullingSystemType::DefaultBuildConfig()) {
        LightPrepareStageResult<DimensionT> result = BuildRuntimeOnly(light_components_,
                                                                      transforms_,
                                                                      light_count_,
                                                                      runtime_scratch_,
                                                                      dirty_component_indices_,
                                                                      dirty_component_count_,
                                                                      transform_dirty_component_indices_,
                                                                      transform_dirty_component_count_,
                                                                      runtime_config_);
        if (!result.has_light_data) {
            return result;
        }

        result.culling_stats = CullingSystemType::Build(light_components_,
                                                        RuntimeSystemType::DerivedGeomData(runtime_scratch_),
                                                        RuntimeSystemType::DerivedOpticalData(runtime_scratch_),
                                                        light_count_,
                                                        camera_component_,
                                                        culling_scratch_,
                                                        culling_config_);
        result.culling_build_invoked = true;
        return result;
    }
};

} // namespace vr::render

