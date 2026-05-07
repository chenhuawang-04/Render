#pragma once

#include "vr/ecs/system/animation_evaluation_context.hpp"

#include <cstdint>

namespace vr::render {

template<ecs::DimensionTag DimensionT>
struct AnimationFrameCoordinatorStats final {
    std::uint32_t set_outputs_call_count = 0U;
    std::uint32_t set_context_call_count = 0U;
    std::uint32_t clear_call_count = 0U;
    std::uint32_t apply_scene_call_count = 0U;
    std::uint32_t apply_shadow_call_count = 0U;
    std::uint32_t source_revision = 0U;
};

template<ecs::DimensionTag DimensionT>
class AnimationFrameCoordinator final {
public:
    using SkeletalOutputType = ecs::SkeletalPoseOutputState<DimensionT>;
    using MorphOutputType = ecs::MorphWeightOutputState;
    using VertexDeformOutputType = ecs::VertexDeformOutputState;
    using FrameSequenceOutputType = ecs::FrameSequenceOutputState;
    using EvaluationContextType = ecs::AnimationEvaluationContext<DimensionT>;

    void ResetAll() noexcept {
        skeletal_outputs = nullptr;
        skeletal_output_count = 0U;
        morph_outputs = nullptr;
        morph_output_count = 0U;
        vertex_deform_outputs = nullptr;
        vertex_deform_output_count = 0U;
        frame_sequence_outputs = nullptr;
        frame_sequence_output_count = 0U;
        stats = {};
    }

    void SetEvaluationContext(const EvaluationContextType& context_) noexcept {
        ++stats.set_context_call_count;
        SetAnimationOutputs(context_.skeletal_outputs.components,
                            context_.skeletal_outputs.count,
                            context_.vertex_deform_outputs.components,
                            context_.vertex_deform_outputs.count,
                            context_.morph_outputs.components,
                            context_.morph_outputs.count,
                            context_.frame_sequence_outputs.components,
                            context_.frame_sequence_outputs.count);
    }

    void SetAnimationOutputs(const SkeletalOutputType* skeletal_outputs_,
                             std::uint32_t skeletal_output_count_,
                             const VertexDeformOutputType* vertex_deform_outputs_,
                             std::uint32_t vertex_deform_output_count_,
                             const MorphOutputType* morph_outputs_,
                             std::uint32_t morph_output_count_,
                             const FrameSequenceOutputType* frame_sequence_outputs_,
                             std::uint32_t frame_sequence_output_count_) noexcept {
        ++stats.set_outputs_call_count;
        const bool changed =
            skeletal_outputs != skeletal_outputs_ ||
            skeletal_output_count != skeletal_output_count_ ||
            morph_outputs != morph_outputs_ ||
            morph_output_count != morph_output_count_ ||
            vertex_deform_outputs != vertex_deform_outputs_ ||
            vertex_deform_output_count != vertex_deform_output_count_ ||
            frame_sequence_outputs != frame_sequence_outputs_ ||
            frame_sequence_output_count != frame_sequence_output_count_;

        skeletal_outputs = skeletal_outputs_;
        skeletal_output_count = skeletal_output_count_;
        morph_outputs = morph_outputs_;
        morph_output_count = morph_output_count_;
        vertex_deform_outputs = vertex_deform_outputs_;
        vertex_deform_output_count = vertex_deform_output_count_;
        frame_sequence_outputs = frame_sequence_outputs_;
        frame_sequence_output_count = frame_sequence_output_count_;
        if (changed) {
            ++stats.source_revision;
        }
    }

    void ClearOutputs() noexcept {
        ++stats.clear_call_count;
        SetAnimationOutputs(nullptr, 0U, nullptr, 0U, nullptr, 0U, nullptr, 0U);
    }

    template<typename RendererT>
    void ApplyToSceneRenderer(RendererT& renderer_) noexcept {
        ++stats.apply_scene_call_count;
        if constexpr (requires(RendererT& candidate_,
                               const SkeletalOutputType* skeletal_outputs_arg_,
                               std::uint32_t skeletal_count_arg_,
                               const VertexDeformOutputType* vertex_outputs_arg_,
                               std::uint32_t vertex_count_arg_,
                               const MorphOutputType* morph_outputs_arg_,
                               std::uint32_t morph_count_arg_,
                               const FrameSequenceOutputType* frame_outputs_arg_,
                               std::uint32_t frame_count_arg_) {
                          candidate_.SetAnimationOutputs(skeletal_outputs_arg_,
                                                         skeletal_count_arg_,
                                                         vertex_outputs_arg_,
                                                         vertex_count_arg_,
                                                         morph_outputs_arg_,
                                                         morph_count_arg_,
                                                         frame_outputs_arg_,
                                                         frame_count_arg_);
                      }) {
            renderer_.SetAnimationOutputs(skeletal_outputs,
                                          skeletal_output_count,
                                          vertex_deform_outputs,
                                          vertex_deform_output_count,
                                          morph_outputs,
                                          morph_output_count,
                                          frame_sequence_outputs,
                                          frame_sequence_output_count);
        }
    }

    template<typename RendererT>
    void ApplyToShadowRenderer(RendererT& renderer_) noexcept {
        ++stats.apply_shadow_call_count;
        if constexpr (requires(RendererT& candidate_,
                               const SkeletalOutputType* skeletal_outputs_arg_,
                               std::uint32_t skeletal_count_arg_,
                               const VertexDeformOutputType* vertex_outputs_arg_,
                               std::uint32_t vertex_count_arg_,
                               const MorphOutputType* morph_outputs_arg_,
                               std::uint32_t morph_count_arg_,
                               const FrameSequenceOutputType* frame_outputs_arg_,
                               std::uint32_t frame_count_arg_) {
                          candidate_.SetAnimationOutputs(skeletal_outputs_arg_,
                                                         skeletal_count_arg_,
                                                         vertex_outputs_arg_,
                                                         vertex_count_arg_,
                                                         morph_outputs_arg_,
                                                         morph_count_arg_,
                                                         frame_outputs_arg_,
                                                         frame_count_arg_);
                      }) {
            renderer_.SetAnimationOutputs(skeletal_outputs,
                                          skeletal_output_count,
                                          vertex_deform_outputs,
                                          vertex_deform_output_count,
                                          morph_outputs,
                                          morph_output_count,
                                          frame_sequence_outputs,
                                          frame_sequence_output_count);
        } else if constexpr (requires(RendererT& candidate_,
                               const SkeletalOutputType* skeletal_outputs_arg_,
                               std::uint32_t skeletal_count_arg_,
                               const MorphOutputType* morph_outputs_arg_,
                               std::uint32_t morph_count_arg_,
                               const FrameSequenceOutputType* frame_outputs_arg_,
                               std::uint32_t frame_count_arg_) {
                          candidate_.SetAnimationOutputs(skeletal_outputs_arg_,
                                                         skeletal_count_arg_,
                                                         morph_outputs_arg_,
                                                         morph_count_arg_,
                                                         frame_outputs_arg_,
                                                         frame_count_arg_);
                      }) {
            renderer_.SetAnimationOutputs(skeletal_outputs,
                                          skeletal_output_count,
                                          morph_outputs,
                                          morph_output_count,
                                          frame_sequence_outputs,
                                          frame_sequence_output_count);
        }
    }

    [[nodiscard]] const SkeletalOutputType* SkeletalOutputs() const noexcept {
        return skeletal_outputs;
    }

    [[nodiscard]] std::uint32_t SkeletalOutputCount() const noexcept {
        return skeletal_output_count;
    }

    [[nodiscard]] const MorphOutputType* MorphOutputs() const noexcept {
        return morph_outputs;
    }

    [[nodiscard]] std::uint32_t MorphOutputCount() const noexcept {
        return morph_output_count;
    }

    [[nodiscard]] const VertexDeformOutputType* VertexDeformOutputs() const noexcept {
        return vertex_deform_outputs;
    }

    [[nodiscard]] std::uint32_t VertexDeformOutputCount() const noexcept {
        return vertex_deform_output_count;
    }

    [[nodiscard]] const FrameSequenceOutputType* FrameSequenceOutputs() const noexcept {
        return frame_sequence_outputs;
    }

    [[nodiscard]] std::uint32_t FrameSequenceOutputCount() const noexcept {
        return frame_sequence_output_count;
    }

    [[nodiscard]] const AnimationFrameCoordinatorStats<DimensionT>& Stats() const noexcept {
        return stats;
    }

private:
    const SkeletalOutputType* skeletal_outputs = nullptr;
    std::uint32_t skeletal_output_count = 0U;
    const MorphOutputType* morph_outputs = nullptr;
    std::uint32_t morph_output_count = 0U;
    const VertexDeformOutputType* vertex_deform_outputs = nullptr;
    std::uint32_t vertex_deform_output_count = 0U;
    const FrameSequenceOutputType* frame_sequence_outputs = nullptr;
    std::uint32_t frame_sequence_output_count = 0U;
    AnimationFrameCoordinatorStats<DimensionT> stats{};
};

} // namespace vr::render
