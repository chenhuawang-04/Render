#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/appearance_component.hpp"
#include "vr/ecs/component/light_component.hpp"
#include "vr/ecs/component/shadow_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/shadow_frame_coordinator.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace vr::geometry {
class GeometryRenderer3D;
}

namespace vr::surface {
class SurfaceRenderer3D;
}

namespace vr::text {
class TextRenderer3D;
}

namespace vr::render {

template<typename T>
using SceneRecorder3DDirtySchedulerVector =
    Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

enum class SceneRecorder3DDirtyInvalidationReason : std::uint8_t {
    none = 0U,
    initial_state = 1U,
    source_pointer_changed = 2U,
    component_count_changed = 3U,
    scheduler_reset = 4U,
    binding_changed = 5U,
};

struct SceneRecorder3DTransformSourceView final {
    const ecs::Transform<ecs::Dim3>* components = nullptr;
    std::uint32_t component_count = 0U;
};

struct SceneRecorder3DAppearanceSourceView final {
    const ecs::Appearance<ecs::Dim3>* components = nullptr;
    std::uint32_t component_count = 0U;
};

struct SceneRecorder3DLightSourceView final {
    const ecs::Light<ecs::Dim3>* components = nullptr;
    const ecs::Transform<ecs::Dim3>* transforms = nullptr;
    std::uint32_t component_count = 0U;
};

struct SceneRecorder3DShadowSourceView final {
    const ecs::Shadow<ecs::Dim3>* components = nullptr;
    const ecs::Transform<ecs::Dim3>* transforms = nullptr;
    std::uint32_t component_count = 0U;
};

using SceneRecorder3DDescribeTransformSourceFn =
    SceneRecorder3DTransformSourceView (*)(void*);
using SceneRecorder3DDescribeAppearanceSourceFn =
    SceneRecorder3DAppearanceSourceView (*)(void*);
using SceneRecorder3DApplyDirtyHintFn = void (*)(void*,
                                                 const std::uint32_t*,
                                                 std::uint32_t);

struct SceneRecorder3DDirtySignalState final {
    std::uint64_t source_revision = 0U;
    std::uint64_t dirty_revision = 0U;
    SceneRecorder3DDirtySchedulerVector<std::uint32_t>
        normalized_dirty_component_indices{};
    SceneRecorder3DDirtySchedulerVector<std::uint64_t>
        observed_component_revisions{};
    const void* source_components = nullptr;
    std::uint32_t component_count = 0U;
    bool full_rebuild_requested = false;
    SceneRecorder3DDirtyInvalidationReason invalidation_reason =
        SceneRecorder3DDirtyInvalidationReason::none;
};

struct SceneRecorder3DSceneRendererDirtyState final {
    void* consumer = nullptr;
    SceneRecorder3DDirtySignalState transform_dirty{};
    SceneRecorder3DDirtySignalState appearance_dirty{};
};

struct SceneRecorder3DLightCoordinatorDirtyState final {
    SceneRecorder3DDirtySignalState light_dirty{};
    SceneRecorder3DDirtySignalState transform_dirty{};
};

struct SceneRecorder3DShadowCoordinatorDirtyState final {
    SceneRecorder3DDirtySignalState shadow_dirty{};
    SceneRecorder3DDirtySignalState transform_dirty{};
};

namespace detail {

struct SceneRecorder3DDirtySchedulingAccess final {
    template<typename RendererT>
        requires requires(RendererT& renderer_) {
            renderer_.transforms;
            renderer_.component_count;
        }
    static SceneRecorder3DTransformSourceView DescribeTransformSource(
        RendererT& renderer_) noexcept {
        return SceneRecorder3DTransformSourceView{
            .components = renderer_.transforms,
            .component_count = renderer_.component_count,
        };
    }

    template<typename RendererT>
        requires requires(RendererT& renderer_) {
            renderer_.appearance_components;
            renderer_.appearance_component_count;
        }
    static SceneRecorder3DAppearanceSourceView DescribeAppearanceSource(
        RendererT& renderer_) noexcept {
        return SceneRecorder3DAppearanceSourceView{
            .components = renderer_.appearance_components,
            .component_count = renderer_.appearance_component_count,
        };
    }

    template<typename RendererT>
        requires (!requires(RendererT& renderer_) {
                      renderer_.transforms;
                      renderer_.component_count;
                  } &&
                  requires(RendererT& renderer_) {
                      renderer_.text_transforms;
                      renderer_.component_count;
                  })
    static SceneRecorder3DTransformSourceView DescribeTransformSource(
        RendererT& renderer_) noexcept {
        return SceneRecorder3DTransformSourceView{
            .components = renderer_.text_transforms,
            .component_count = renderer_.component_count,
        };
    }

    template<typename RendererT>
        requires requires(RendererT& renderer_,
                          const std::uint32_t* indices_,
                          std::uint32_t count_) {
            renderer_.SetTransformDirtyHint(indices_, count_);
        }
    static void ApplyTransformDirtyHint(RendererT& renderer_,
                                        const std::uint32_t* indices_,
                                        std::uint32_t count_) noexcept {
        renderer_.SetTransformDirtyHint(indices_, count_);
    }

    template<typename RendererT>
        requires (!requires(RendererT& renderer_,
                            const std::uint32_t* indices_,
                            std::uint32_t count_) {
                      renderer_.SetTransformDirtyHint(indices_, count_);
                  } &&
                  requires(RendererT& renderer_) {
                      renderer_.pending_transform_dirty_component_indices;
                      renderer_.pending_transform_dirty_component_count;
                  })
    static void ApplyTransformDirtyHint(RendererT& renderer_,
                                        const std::uint32_t* indices_,
                                        std::uint32_t count_) noexcept {
        renderer_.pending_transform_dirty_component_indices = indices_;
        renderer_.pending_transform_dirty_component_count = count_;
    }

    template<typename RendererT>
        requires requires(RendererT& renderer_,
                          const std::uint32_t* indices_,
                          std::uint32_t count_) {
            renderer_.SetAppearanceDirtyHint(indices_, count_);
        }
    static void ApplyAppearanceDirtyHint(RendererT& renderer_,
                                         const std::uint32_t* indices_,
                                         std::uint32_t count_) noexcept {
        renderer_.SetAppearanceDirtyHint(indices_, count_);
    }

    template<ecs::DimensionTag DimensionT>
    static SceneRecorder3DLightSourceView DescribeLightSource(
        LightFrameCoordinator<DimensionT>& coordinator_) noexcept {
        return SceneRecorder3DLightSourceView{
            .components = coordinator_.light_components,
            .transforms = coordinator_.transforms,
            .component_count = coordinator_.light_count,
        };
    }

    template<ecs::DimensionTag DimensionT>
    static SceneRecorder3DShadowSourceView DescribeShadowSource(
        ShadowFrameCoordinator<DimensionT>& coordinator_) noexcept {
        return SceneRecorder3DShadowSourceView{
            .components = coordinator_.shadow_components,
            .transforms = coordinator_.transforms,
            .component_count = coordinator_.shadow_count,
        };
    }
};

template<typename RendererT>
concept SceneRecorder3DTransformSourceDescribable =
    requires(RendererT& renderer_) {
        SceneRecorder3DDirtySchedulingAccess::DescribeTransformSource(renderer_);
    };

template<typename RendererT>
concept SceneRecorder3DAppearanceSourceDescribable =
    requires(RendererT& renderer_) {
        SceneRecorder3DDirtySchedulingAccess::DescribeAppearanceSource(renderer_);
    };

template<typename RendererT>
concept SceneRecorder3DTransformDirtyHintApplicable =
    requires(RendererT& renderer_,
             const std::uint32_t* indices_,
             std::uint32_t count_) {
        SceneRecorder3DDirtySchedulingAccess::ApplyTransformDirtyHint(
            renderer_,
            indices_,
            count_);
    };

template<typename RendererT>
concept SceneRecorder3DAppearanceDirtyHintApplicable =
    requires(RendererT& renderer_,
             const std::uint32_t* indices_,
             std::uint32_t count_) {
        SceneRecorder3DDirtySchedulingAccess::ApplyAppearanceDirtyHint(
            renderer_,
            indices_,
            count_);
    };

} // namespace detail

class SceneRecorder3DDirtyScheduler final {
public:
    SceneRecorder3DDirtyScheduler() = default;
    ~SceneRecorder3DDirtyScheduler() = default;

    SceneRecorder3DDirtyScheduler(const SceneRecorder3DDirtyScheduler&) = delete;
    SceneRecorder3DDirtyScheduler& operator=(const SceneRecorder3DDirtyScheduler&) =
        delete;
    SceneRecorder3DDirtyScheduler(SceneRecorder3DDirtyScheduler&&) = delete;
    SceneRecorder3DDirtyScheduler& operator=(SceneRecorder3DDirtyScheduler&&) =
        delete;

    void Reset() noexcept {
        scene_source_revision = 0U;
        full_rebuild_requested = true;
        invalidation_reason =
            SceneRecorder3DDirtyInvalidationReason::initial_state;
        scene_renderer_states.clear();
        light_state = {};
        shadow_state = {};
    }

    void BeginPrepareCycle() noexcept {
        full_rebuild_requested = false;
        invalidation_reason = SceneRecorder3DDirtyInvalidationReason::none;
    }

    void InvalidateAll(SceneRecorder3DDirtyInvalidationReason reason_) noexcept {
        MarkSignalStateForReset(light_state.light_dirty, reason_);
        MarkSignalStateForReset(light_state.transform_dirty, reason_);
        MarkSignalStateForReset(shadow_state.shadow_dirty, reason_);
        MarkSignalStateForReset(shadow_state.transform_dirty, reason_);
        for (SceneRecorder3DSceneRendererDirtyState& state :
             scene_renderer_states) {
            MarkSignalStateForReset(state.transform_dirty, reason_);
            MarkSignalStateForReset(state.appearance_dirty, reason_);
        }
        ++scene_source_revision;
        full_rebuild_requested = true;
        invalidation_reason = reason_;
    }

    void ClearSceneRendererStates() noexcept {
        scene_renderer_states.clear();
    }

    void ReserveSceneRendererStates(std::uint32_t renderer_count_) {
        const std::size_t reserve_count =
            static_cast<std::size_t>(renderer_count_);
        if (scene_renderer_states.capacity() < reserve_count) {
            scene_renderer_states.reserve(reserve_count);
        }
    }

    void ScheduleLightFrameCoordinator(
        LightFrameCoordinator<ecs::Dim3>* coordinator_) {
        if (coordinator_ == nullptr) {
            UpdateSignalState(light_state.light_dirty,
                              static_cast<const ecs::Light<ecs::Dim3>*>(nullptr),
                              0U,
                              [](const ecs::Light<ecs::Dim3>& component_)
                                  noexcept {
                                      return ComposeLightRevision(component_);
                                  });
            UpdateSignalState(light_state.transform_dirty,
                              static_cast<const ecs::Transform<ecs::Dim3>*>(
                                  nullptr),
                              0U,
                              [](const ecs::Transform<ecs::Dim3>& component_)
                                  noexcept {
                                      return ComposeTransformRevision(component_);
                                  });
            return;
        }

        const SceneRecorder3DLightSourceView source_view =
            detail::SceneRecorder3DDirtySchedulingAccess::DescribeLightSource(
                *coordinator_);
        UpdateSignalState(light_state.light_dirty,
                          source_view.components,
                          source_view.component_count,
                          [](const ecs::Light<ecs::Dim3>& component_) noexcept {
                              return ComposeLightRevision(component_);
                          });
        UpdateSignalState(light_state.transform_dirty,
                          source_view.transforms,
                          source_view.component_count,
                          [](const ecs::Transform<ecs::Dim3>& component_)
                              noexcept {
                                  return ComposeTransformRevision(component_);
                              });

        ApplyDirtyHintIfNeeded(
            light_state.light_dirty,
            [&](const std::uint32_t* indices_, std::uint32_t count_) {
                coordinator_->SetLightDirtyHint(indices_, count_);
            });
        ApplyDirtyHintIfNeeded(
            light_state.transform_dirty,
            [&](const std::uint32_t* indices_, std::uint32_t count_) {
                coordinator_->SetTransformDirtyHint(indices_, count_);
            });
    }

    void ScheduleShadowFrameCoordinator(
        ShadowFrameCoordinator<ecs::Dim3>* coordinator_) {
        if (coordinator_ == nullptr) {
            UpdateSignalState(shadow_state.shadow_dirty,
                              static_cast<const ecs::Shadow<ecs::Dim3>*>(
                                  nullptr),
                              0U,
                              [](const ecs::Shadow<ecs::Dim3>& component_)
                                  noexcept {
                                      return ComposeShadowRevision(component_);
                                  });
            UpdateSignalState(shadow_state.transform_dirty,
                              static_cast<const ecs::Transform<ecs::Dim3>*>(
                                  nullptr),
                              0U,
                              [](const ecs::Transform<ecs::Dim3>& component_)
                                  noexcept {
                                      return ComposeTransformRevision(component_);
                                  });
            return;
        }

        const SceneRecorder3DShadowSourceView source_view =
            detail::SceneRecorder3DDirtySchedulingAccess::DescribeShadowSource(
                *coordinator_);
        UpdateSignalState(shadow_state.shadow_dirty,
                          source_view.components,
                          source_view.component_count,
                          [](const ecs::Shadow<ecs::Dim3>& component_) noexcept {
                              return ComposeShadowRevision(component_);
                          });
        UpdateSignalState(shadow_state.transform_dirty,
                          source_view.transforms,
                          source_view.component_count,
                          [](const ecs::Transform<ecs::Dim3>& component_)
                              noexcept {
                                  return ComposeTransformRevision(component_);
                              });

        ApplyDirtyHintIfNeeded(
            shadow_state.shadow_dirty,
            [&](const std::uint32_t* indices_, std::uint32_t count_) {
                coordinator_->SetShadowDirtyHint(indices_, count_);
            });
        ApplyDirtyHintIfNeeded(
            shadow_state.transform_dirty,
            [&](const std::uint32_t* indices_, std::uint32_t count_) {
                coordinator_->SetTransformDirtyHint(indices_, count_);
            });
    }

    template<typename RendererT>
    void ScheduleSceneRenderer(RendererT& renderer_) {
        auto& state = ResolveSceneRendererState(&renderer_);
        if constexpr (detail::SceneRecorder3DTransformSourceDescribable<
                          RendererT>) {
            const SceneRecorder3DTransformSourceView source_view =
                detail::SceneRecorder3DDirtySchedulingAccess::
                    DescribeTransformSource(renderer_);
            UpdateSignalState(
                state.transform_dirty,
                source_view.components,
                source_view.component_count,
                [](const ecs::Transform<ecs::Dim3>& component_) noexcept {
                    return ComposeTransformRevision(component_);
                });
            if constexpr (detail::SceneRecorder3DTransformDirtyHintApplicable<
                              RendererT>) {
                ApplyDirtyHintIfNeeded(
                    state.transform_dirty,
                    [&](const std::uint32_t* indices_, std::uint32_t count_) {
                        detail::SceneRecorder3DDirtySchedulingAccess::
                            ApplyTransformDirtyHint(renderer_,
                                                    indices_,
                                                    count_);
                    });
            }
        }

        if constexpr (detail::SceneRecorder3DAppearanceSourceDescribable<
                          RendererT>) {
            const SceneRecorder3DAppearanceSourceView source_view =
                detail::SceneRecorder3DDirtySchedulingAccess::
                    DescribeAppearanceSource(renderer_);
            UpdateSignalState(
                state.appearance_dirty,
                source_view.components,
                source_view.component_count,
                [](const ecs::Appearance<ecs::Dim3>& component_) noexcept {
                    return ComposeAppearanceRevision(component_);
                });
            if constexpr (
                detail::SceneRecorder3DAppearanceDirtyHintApplicable<
                    RendererT>) {
                ApplyDirtyHintIfNeeded(
                    state.appearance_dirty,
                    [&](const std::uint32_t* indices_, std::uint32_t count_) {
                        detail::SceneRecorder3DDirtySchedulingAccess::
                            ApplyAppearanceDirtyHint(renderer_,
                                                     indices_,
                                                     count_);
                    });
            }
        }
    }

    void ScheduleSceneRenderer(
        void* consumer_,
        SceneRecorder3DDescribeTransformSourceFn describe_transform_source_fn_,
        SceneRecorder3DApplyDirtyHintFn apply_transform_dirty_fn_,
        SceneRecorder3DDescribeAppearanceSourceFn describe_appearance_source_fn_,
        SceneRecorder3DApplyDirtyHintFn apply_appearance_dirty_fn_) {
        if (consumer_ == nullptr ||
            (describe_transform_source_fn_ == nullptr &&
             describe_appearance_source_fn_ == nullptr)) {
            return;
        }

        SceneRecorder3DSceneRendererDirtyState& state =
            ResolveSceneRendererState(consumer_);
        if (describe_transform_source_fn_ != nullptr) {
            const SceneRecorder3DTransformSourceView source_view =
                describe_transform_source_fn_(consumer_);
            UpdateSignalState(
                state.transform_dirty,
                source_view.components,
                source_view.component_count,
                [](const ecs::Transform<ecs::Dim3>& component_) noexcept {
                    return ComposeTransformRevision(component_);
                });
            if (apply_transform_dirty_fn_ != nullptr) {
                ApplyDirtyHintIfNeeded(
                    state.transform_dirty,
                    [&](const std::uint32_t* indices_, std::uint32_t count_) {
                        apply_transform_dirty_fn_(consumer_, indices_, count_);
                    });
            }
        }

        if (describe_appearance_source_fn_ != nullptr) {
            const SceneRecorder3DAppearanceSourceView source_view =
                describe_appearance_source_fn_(consumer_);
            UpdateSignalState(
                state.appearance_dirty,
                source_view.components,
                source_view.component_count,
                [](const ecs::Appearance<ecs::Dim3>& component_) noexcept {
                    return ComposeAppearanceRevision(component_);
                });
            if (apply_appearance_dirty_fn_ != nullptr) {
                ApplyDirtyHintIfNeeded(
                    state.appearance_dirty,
                    [&](const std::uint32_t* indices_, std::uint32_t count_) {
                        apply_appearance_dirty_fn_(consumer_, indices_, count_);
                    });
            }
        }
    }

    [[nodiscard]] std::uint64_t SceneSourceRevision() const noexcept {
        return scene_source_revision;
    }

    [[nodiscard]] bool FullRebuildRequested() const noexcept {
        return full_rebuild_requested;
    }

    [[nodiscard]] SceneRecorder3DDirtyInvalidationReason InvalidationReason()
        const noexcept {
        return invalidation_reason;
    }

    [[nodiscard]] const SceneRecorder3DLightCoordinatorDirtyState& LightState()
        const noexcept {
        return light_state;
    }

    [[nodiscard]] const SceneRecorder3DShadowCoordinatorDirtyState&
    ShadowState() const noexcept {
        return shadow_state;
    }

    [[nodiscard]] const SceneRecorder3DSceneRendererDirtyState* FindSceneRendererState(
        void* consumer_) const noexcept {
        for (const SceneRecorder3DSceneRendererDirtyState& state :
             scene_renderer_states) {
            if (state.consumer == consumer_) {
                return &state;
            }
        }
        return nullptr;
    }

private:
    [[nodiscard]] static std::uint64_t ComposeTransformRevision(
        const ecs::Transform<ecs::Dim3>& component_) noexcept {
        return static_cast<std::uint64_t>(component_.runtime.world_revision);
    }

    [[nodiscard]] static std::uint64_t ComposeAppearanceRevision(
        const ecs::Appearance<ecs::Dim3>& component_) noexcept {
        return (static_cast<std::uint64_t>(component_.runtime.revision_style)
                << 32U) ^
               (static_cast<std::uint64_t>(component_.runtime.revision_binding)
                << 1U) ^
               static_cast<std::uint64_t>(component_.runtime.dirty_flags);
    }

    [[nodiscard]] static std::uint64_t ComposeLightRevision(
        const ecs::Light<ecs::Dim3>& component_) noexcept {
        return (static_cast<std::uint64_t>(component_.state.revision_style)
                << 32U) ^
               (static_cast<std::uint64_t>(component_.state.revision_binding)
                << 1U) ^
               static_cast<std::uint64_t>(component_.state.dirty_flags);
    }

    [[nodiscard]] static std::uint64_t ComposeShadowRevision(
        const ecs::Shadow<ecs::Dim3>& component_) noexcept {
        return (static_cast<std::uint64_t>(component_.state.revision_style)
                << 32U) ^
               (static_cast<std::uint64_t>(component_.state.revision_binding)
                << 1U) ^
               static_cast<std::uint64_t>(component_.state.dirty_flags);
    }

    static void MarkSignalStateForReset(
        SceneRecorder3DDirtySignalState& state_,
        SceneRecorder3DDirtyInvalidationReason reason_) noexcept {
        state_.source_components = nullptr;
        state_.component_count = 0U;
        state_.observed_component_revisions.clear();
        state_.normalized_dirty_component_indices.clear();
        state_.full_rebuild_requested = true;
        state_.invalidation_reason = reason_;
    }

    void AccumulateInvalidation(
        SceneRecorder3DDirtyInvalidationReason reason_) noexcept {
        if (reason_ == SceneRecorder3DDirtyInvalidationReason::none) {
            return;
        }
        full_rebuild_requested = true;
        if (invalidation_reason == SceneRecorder3DDirtyInvalidationReason::none) {
            invalidation_reason = reason_;
        }
    }

    template<typename ApplyFn>
    static void ApplyDirtyHintIfNeeded(
        const SceneRecorder3DDirtySignalState& state_,
        ApplyFn&& apply_fn_) {
        if (state_.full_rebuild_requested ||
            state_.normalized_dirty_component_indices.empty()) {
            return;
        }
        apply_fn_(state_.normalized_dirty_component_indices.data(),
                  static_cast<std::uint32_t>(
                      state_.normalized_dirty_component_indices.size()));
    }

    template<typename ComponentT, typename RevisionFn>
    void UpdateSignalState(SceneRecorder3DDirtySignalState& state_,
                           const ComponentT* components_,
                           std::uint32_t component_count_,
                           RevisionFn&& revision_fn_) {
        state_.normalized_dirty_component_indices.clear();
        state_.full_rebuild_requested = false;
        state_.invalidation_reason =
            SceneRecorder3DDirtyInvalidationReason::none;

        const bool first_observation =
            state_.source_revision == 0U &&
            state_.observed_component_revisions.empty() &&
            state_.source_components == nullptr &&
            state_.component_count == 0U;
        const bool source_pointer_changed =
            state_.source_components != components_;
        const bool component_count_changed =
            state_.component_count != component_count_;
        const bool storage_mismatch =
            state_.observed_component_revisions.size() != component_count_;
        if (first_observation ||
            source_pointer_changed ||
            component_count_changed ||
            storage_mismatch) {
            state_.source_components = components_;
            state_.component_count = component_count_;
            state_.observed_component_revisions.clear();
            if (components_ != nullptr && component_count_ > 0U) {
                state_.observed_component_revisions.resize(component_count_);
                for (std::uint32_t index = 0U; index < component_count_;
                     ++index) {
                    state_.observed_component_revisions[index] =
                        revision_fn_(components_[index]);
                }
            }
            ++state_.source_revision;
            ++state_.dirty_revision;
            ++scene_source_revision;
            state_.full_rebuild_requested = true;
            if (first_observation) {
                state_.invalidation_reason =
                    SceneRecorder3DDirtyInvalidationReason::initial_state;
            } else if (source_pointer_changed) {
                state_.invalidation_reason =
                    SceneRecorder3DDirtyInvalidationReason::
                        source_pointer_changed;
            } else {
                state_.invalidation_reason =
                    SceneRecorder3DDirtyInvalidationReason::
                        component_count_changed;
            }
            AccumulateInvalidation(state_.invalidation_reason);
            return;
        }

        if (components_ == nullptr || component_count_ == 0U) {
            return;
        }

        for (std::uint32_t index = 0U; index < component_count_; ++index) {
            const std::uint64_t revision = revision_fn_(components_[index]);
            if (state_.observed_component_revisions[index] == revision) {
                continue;
            }
            state_.observed_component_revisions[index] = revision;
            state_.normalized_dirty_component_indices.push_back(index);
        }

        if (!state_.normalized_dirty_component_indices.empty()) {
            ++state_.dirty_revision;
        }
    }

    [[nodiscard]] SceneRecorder3DSceneRendererDirtyState& ResolveSceneRendererState(
        void* consumer_) {
        for (SceneRecorder3DSceneRendererDirtyState& state :
             scene_renderer_states) {
            if (state.consumer == consumer_) {
                return state;
            }
        }
        scene_renderer_states.push_back(
            SceneRecorder3DSceneRendererDirtyState{.consumer = consumer_});
        return scene_renderer_states.back();
    }

private:
    std::uint64_t scene_source_revision = 0U;
    bool full_rebuild_requested = true;
    SceneRecorder3DDirtyInvalidationReason invalidation_reason =
        SceneRecorder3DDirtyInvalidationReason::initial_state;
    SceneRecorder3DLightCoordinatorDirtyState light_state{};
    SceneRecorder3DShadowCoordinatorDirtyState shadow_state{};
    SceneRecorder3DDirtySchedulerVector<SceneRecorder3DSceneRendererDirtyState>
        scene_renderer_states{};
};

} // namespace vr::render
