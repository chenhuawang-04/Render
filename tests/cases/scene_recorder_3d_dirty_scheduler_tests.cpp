#include "support/test_framework.hpp"

#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/light_system.hpp"
#include "vr/ecs/system/shadow_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render/scene_recorder_3d_dirty_scheduler.hpp"
#include "vr/surface/surface_renderer_3d.hpp"
#include "vr/text/text_renderer_3d.hpp"

#include <array>
#include <cstdint>

namespace vr::text {
struct TextRenderer3DTestAccess final {
    [[nodiscard]] static std::uint32_t PendingTransformDirtyHintCount(
        const TextRenderer3D& renderer_) noexcept {
        return renderer_.pending_transform_dirty_component_count;
    }
};
} // namespace vr::text

namespace {

using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
using Light3D = vr::ecs::Light<vr::ecs::Dim3>;
using LightSystem3D = vr::ecs::LightSystem<vr::ecs::Dim3>;
using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;
using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
using Text3D = vr::ecs::Text<vr::ecs::Dim3>;
using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

struct NoopSceneRenderer3D final {
    void PrepareFrame(const vr::render::SceneRecorder3DPrepareView&) noexcept {
    }

    void OnSwapchainRecreated(std::uint32_t,
                              VkExtent2D,
                              VkFormat,
                              std::uint64_t,
                              std::uint64_t) noexcept {
    }
};

void InitializeTransform(Transform3D& transform_, float x_) {
    TransformSystem3D::Initialize(transform_);
    TransformSystem3D::SetLocalPosition(
        transform_,
        vr::ecs::Float3{.x = x_, .y = 0.0F, .z = 0.0F});
}

} // namespace

VR_TEST_CASE(SceneRecorder3DDirtyScheduler_normalizes_light_and_shadow_dirty_sources,
             "unit;core;render;scene3d;dirty_scheduler") {
    vr::render::SceneRecorder3DDirtyScheduler scheduler{};
    vr::render::LightFrameCoordinator<vr::ecs::Dim3> light_coordinator{};
    vr::render::ShadowFrameCoordinator<vr::ecs::Dim3> shadow_coordinator{};

    std::array<Light3D, 2U> lights{};
    std::array<Transform3D, 2U> light_transforms{};
    std::array<Shadow3D, 2U> shadows{};
    std::array<Transform3D, 2U> shadow_transforms{};
    for (std::uint32_t index = 0U; index < lights.size(); ++index) {
        LightSystem3D::Initialize(lights[index]);
        LightSystem3D::SetIntensity(lights[index], 100.0F + static_cast<float>(index));
        ShadowSystem3D::Initialize(shadows[index]);
        ShadowSystem3D::SetMapResolution(shadows[index], 512U, 512U);
        InitializeTransform(light_transforms[index], static_cast<float>(index));
        InitializeTransform(shadow_transforms[index], static_cast<float>(index));
    }
    TransformSystem3D::UpdateHierarchy(light_transforms.data(),
                                       static_cast<std::uint32_t>(light_transforms.size()));
    TransformSystem3D::UpdateHierarchy(shadow_transforms.data(),
                                       static_cast<std::uint32_t>(shadow_transforms.size()));
    light_coordinator.SetLightData(lights.data(),
                                   light_transforms.data(),
                                   static_cast<std::uint32_t>(lights.size()));
    shadow_coordinator.SetShadowData(shadows.data(),
                                     shadow_transforms.data(),
                                     static_cast<std::uint32_t>(shadows.size()));

    scheduler.BeginPrepareCycle();
    scheduler.ScheduleLightFrameCoordinator(&light_coordinator);
    scheduler.ScheduleShadowFrameCoordinator(&shadow_coordinator);
    VR_CHECK(scheduler.FullRebuildRequested());
    VR_CHECK(scheduler.SceneSourceRevision() > 0U);
    VR_CHECK(scheduler.LightState().light_dirty.full_rebuild_requested);
    VR_CHECK(scheduler.LightState().transform_dirty.full_rebuild_requested);
    VR_CHECK(scheduler.ShadowState().shadow_dirty.full_rebuild_requested);
    VR_CHECK(scheduler.ShadowState().transform_dirty.full_rebuild_requested);

    const std::uint64_t source_revision_before_dirty =
        scheduler.SceneSourceRevision();
    const std::uint64_t light_dirty_revision_before =
        scheduler.LightState().light_dirty.dirty_revision;
    const std::uint64_t shadow_transform_dirty_revision_before =
        scheduler.ShadowState().transform_dirty.dirty_revision;

    LightSystem3D::SetIntensity(lights[1U], 240.0F);
    LightSystem3D::SetRange(lights[1U], 9.5F);
    ShadowSystem3D::SetBias(shadows[0U], 0.0020F, 0.0005F);
    ShadowSystem3D::SetBias(shadows[0U], 0.0030F, 0.0007F);
    TransformSystem3D::SetLocalPosition(
        shadow_transforms[0U],
        vr::ecs::Float3{.x = 4.0F, .y = 1.0F, .z = 0.0F});
    TransformSystem3D::SetLocalPosition(
        shadow_transforms[0U],
        vr::ecs::Float3{.x = 5.0F, .y = 1.5F, .z = 0.0F});
    TransformSystem3D::UpdateHierarchy(shadow_transforms.data(),
                                       static_cast<std::uint32_t>(shadow_transforms.size()));

    scheduler.BeginPrepareCycle();
    scheduler.ScheduleLightFrameCoordinator(&light_coordinator);
    scheduler.ScheduleShadowFrameCoordinator(&shadow_coordinator);

    VR_CHECK(!scheduler.FullRebuildRequested());
    VR_CHECK(scheduler.SceneSourceRevision() == source_revision_before_dirty);
    VR_CHECK(!scheduler.LightState().light_dirty.full_rebuild_requested);
    VR_CHECK(scheduler.LightState().light_dirty.dirty_revision >
             light_dirty_revision_before);
    VR_REQUIRE(
        scheduler.LightState().light_dirty.normalized_dirty_component_indices.size() ==
        1U);
    VR_CHECK(
        scheduler.LightState().light_dirty.normalized_dirty_component_indices[0U] ==
        1U);
    VR_REQUIRE(
        scheduler.ShadowState().shadow_dirty.normalized_dirty_component_indices.size() ==
        1U);
    VR_CHECK(
        scheduler.ShadowState().shadow_dirty.normalized_dirty_component_indices[0U] ==
        0U);
    VR_CHECK(!scheduler.ShadowState().transform_dirty.full_rebuild_requested);
    VR_CHECK(scheduler.ShadowState().transform_dirty.dirty_revision >
             shadow_transform_dirty_revision_before);
    VR_REQUIRE(scheduler.ShadowState()
                   .transform_dirty.normalized_dirty_component_indices.size() == 1U);
    VR_CHECK(scheduler.ShadowState()
                 .transform_dirty.normalized_dirty_component_indices[0U] == 0U);
}

VR_TEST_CASE(SceneRecorder3DDirtyScheduler_tracks_surface_transform_and_appearance_dirty,
             "unit;core;render;scene3d;dirty_scheduler") {
    vr::render::SceneRecorder3DDirtyScheduler scheduler{};
    vr::surface::SurfaceRenderer3D surface_renderer{};

    std::array<Surface3D, 2U> surfaces{};
    std::array<Transform3D, 2U> transforms{};
    std::array<Appearance3D, 2U> appearances{};
    for (std::uint32_t index = 0U; index < surfaces.size(); ++index) {
        SurfaceSystem3D::Initialize(surfaces[index]);
        AppearanceSystem3D::Initialize(appearances[index]);
        InitializeTransform(transforms[index], static_cast<float>(index));
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));
    surface_renderer.SetSceneData(surfaces.data(),
                                  transforms.data(),
                                  static_cast<std::uint32_t>(surfaces.size()),
                                  nullptr,
                                  nullptr,
                                  nullptr);
    surface_renderer.SetAppearanceData(appearances.data(),
                                       static_cast<std::uint32_t>(appearances.size()));

    scheduler.BeginPrepareCycle();
    scheduler.ScheduleSceneRenderer(surface_renderer);
    const auto* initial_state =
        scheduler.FindSceneRendererState(&surface_renderer);
    VR_REQUIRE(initial_state != nullptr);
    VR_CHECK(initial_state->transform_dirty.full_rebuild_requested);
    VR_CHECK(initial_state->appearance_dirty.full_rebuild_requested);
    VR_CHECK(initial_state->transform_dirty.invalidation_reason ==
             vr::render::SceneRecorder3DDirtyInvalidationReason::initial_state);
    VR_CHECK(initial_state->appearance_dirty.invalidation_reason ==
             vr::render::SceneRecorder3DDirtyInvalidationReason::initial_state);

    const std::uint64_t source_revision_before_dirty =
        scheduler.SceneSourceRevision();
    TransformSystem3D::SetLocalPosition(
        transforms[0U],
        vr::ecs::Float3{.x = 3.0F, .y = 0.0F, .z = 0.0F});
    TransformSystem3D::SetLocalPosition(
        transforms[0U],
        vr::ecs::Float3{.x = 4.0F, .y = 0.5F, .z = 0.0F});
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));
    AppearanceSystem3D::SetMetallic(appearances[1U], 0.72F);
    AppearanceSystem3D::SetRoughness(appearances[1U], 0.18F);

    scheduler.BeginPrepareCycle();
    scheduler.ScheduleSceneRenderer(surface_renderer);
    const auto* dirty_state =
        scheduler.FindSceneRendererState(&surface_renderer);
    VR_REQUIRE(dirty_state != nullptr);
    VR_CHECK(scheduler.SceneSourceRevision() == source_revision_before_dirty);
    VR_CHECK(!dirty_state->transform_dirty.full_rebuild_requested);
    VR_REQUIRE(
        dirty_state->transform_dirty.normalized_dirty_component_indices.size() ==
        1U);
    VR_CHECK(
        dirty_state->transform_dirty.normalized_dirty_component_indices[0U] == 0U);
    VR_CHECK(!dirty_state->appearance_dirty.full_rebuild_requested);
    VR_REQUIRE(
        dirty_state->appearance_dirty.normalized_dirty_component_indices.size() ==
        1U);
    VR_CHECK(
        dirty_state->appearance_dirty.normalized_dirty_component_indices[0U] ==
        1U);
}

VR_TEST_CASE(SceneRecorder3DDirtyScheduler_invalidates_on_scene_source_change,
             "unit;core;render;scene3d;dirty_scheduler") {
    vr::render::SceneRecorder3DDirtyScheduler scheduler{};
    vr::surface::SurfaceRenderer3D surface_renderer{};

    std::array<Surface3D, 2U> surfaces{};
    std::array<Transform3D, 2U> transforms{};
    std::array<Appearance3D, 2U> appearances{};
    for (std::uint32_t index = 0U; index < surfaces.size(); ++index) {
        SurfaceSystem3D::Initialize(surfaces[index]);
        AppearanceSystem3D::Initialize(appearances[index]);
        InitializeTransform(transforms[index], static_cast<float>(index));
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));
    surface_renderer.SetSceneData(surfaces.data(),
                                  transforms.data(),
                                  static_cast<std::uint32_t>(surfaces.size()),
                                  nullptr,
                                  nullptr,
                                  nullptr);
    surface_renderer.SetAppearanceData(appearances.data(),
                                       static_cast<std::uint32_t>(appearances.size()));

    scheduler.BeginPrepareCycle();
    scheduler.ScheduleSceneRenderer(surface_renderer);
    const std::uint64_t source_revision_before_swap =
        scheduler.SceneSourceRevision();

    std::array<Surface3D, 1U> replacement_surfaces{};
    std::array<Transform3D, 1U> replacement_transforms{};
    SurfaceSystem3D::Initialize(replacement_surfaces[0U]);
    InitializeTransform(replacement_transforms[0U], 9.0F);
    TransformSystem3D::UpdateHierarchy(replacement_transforms.data(), 1U);
    surface_renderer.SetSceneData(replacement_surfaces.data(),
                                  replacement_transforms.data(),
                                  1U,
                                  nullptr,
                                  nullptr,
                                  nullptr);

    scheduler.BeginPrepareCycle();
    scheduler.ScheduleSceneRenderer(surface_renderer);
    const auto* state = scheduler.FindSceneRendererState(&surface_renderer);
    VR_REQUIRE(state != nullptr);
    VR_CHECK(scheduler.FullRebuildRequested());
    VR_CHECK(scheduler.SceneSourceRevision() > source_revision_before_swap);
    VR_CHECK(state->transform_dirty.full_rebuild_requested);
    VR_CHECK(state->transform_dirty.invalidation_reason ==
                 vr::render::SceneRecorder3DDirtyInvalidationReason::
                     source_pointer_changed ||
             state->transform_dirty.invalidation_reason ==
                 vr::render::SceneRecorder3DDirtyInvalidationReason::
                     component_count_changed);
}

VR_TEST_CASE(SceneRecorder3DDirtyScheduler_tracks_text_transform_dirty,
             "unit;core;render;scene3d;dirty_scheduler") {
    vr::render::SceneRecorder3DDirtyScheduler scheduler{};
    vr::text::TextRenderer3D text_renderer{};

    std::array<Text3D, 2U> text_components{};
    std::array<Transform3D, 2U> transforms{};
    for (std::uint32_t index = 0U; index < text_components.size(); ++index) {
        TextSystem3D::Initialize(text_components[index]);
        (void)TextSystem3D::SetText(text_components[index], "dirty");
        InitializeTransform(transforms[index], static_cast<float>(index));
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));
    text_renderer.SetSceneData(text_components.data(),
                               transforms.data(),
                               static_cast<std::uint32_t>(text_components.size()),
                               nullptr,
                               nullptr,
                               nullptr);

    scheduler.BeginPrepareCycle();
    scheduler.ScheduleSceneRenderer(text_renderer);
    const auto* initial_state = scheduler.FindSceneRendererState(&text_renderer);
    VR_REQUIRE(initial_state != nullptr);
    VR_CHECK(initial_state->transform_dirty.full_rebuild_requested);

    TransformSystem3D::SetLocalPosition(
        transforms[1U],
        vr::ecs::Float3{.x = 4.0F, .y = 0.5F, .z = 0.0F});
    TransformSystem3D::UpdateHierarchy(transforms.data(),
                                       static_cast<std::uint32_t>(transforms.size()));

    scheduler.BeginPrepareCycle();
    scheduler.ScheduleSceneRenderer(text_renderer);
    const auto* dirty_state = scheduler.FindSceneRendererState(&text_renderer);
    VR_REQUIRE(dirty_state != nullptr);
    VR_CHECK(!dirty_state->transform_dirty.full_rebuild_requested);
    VR_REQUIRE(
        dirty_state->transform_dirty.normalized_dirty_component_indices.size() == 1U);
    VR_CHECK(
        dirty_state->transform_dirty.normalized_dirty_component_indices[0U] == 1U);
    VR_CHECK(
        vr::text::TextRenderer3DTestAccess::PendingTransformDirtyHintCount(
            text_renderer) == 1U);

    text_renderer.SetSceneData(nullptr, nullptr, 0U, nullptr, nullptr, nullptr);
    VR_CHECK(
        vr::text::TextRenderer3DTestAccess::PendingTransformDirtyHintCount(
            text_renderer) == 0U);
}

VR_TEST_CASE(SceneRecorder3DDirtyScheduler_no_visible_geometry_consumer_keeps_light_shadow_hints_bounded,
             "unit;core;render;scene3d;dirty_scheduler") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();

    vr::render::LightFrameCoordinator<vr::ecs::Dim3> light_coordinator{};
    vr::render::ShadowFrameCoordinator<vr::ecs::Dim3> shadow_coordinator{};
    recorder.BindLightFrameCoordinator(&light_coordinator);
    recorder.BindShadowRuntime(&shadow_coordinator, nullptr);

    std::array<Light3D, 1U> lights{};
    std::array<Transform3D, 1U> light_transforms{};
    std::array<Shadow3D, 1U> shadows{};
    std::array<Transform3D, 1U> shadow_transforms{};
    LightSystem3D::Initialize(lights[0U]);
    ShadowSystem3D::Initialize(shadows[0U]);
    InitializeTransform(light_transforms[0U], 0.0F);
    InitializeTransform(shadow_transforms[0U], 0.0F);
    TransformSystem3D::UpdateHierarchy(light_transforms.data(), 1U);
    TransformSystem3D::UpdateHierarchy(shadow_transforms.data(), 1U);
    light_coordinator.SetLightData(lights.data(), light_transforms.data(), 1U);
    shadow_coordinator.SetShadowData(shadows.data(), shadow_transforms.data(), 1U);

    NoopSceneRenderer3D hidden_geometry_renderer{};
    recorder.RegisterOpaqueSceneRenderer(hidden_geometry_renderer,
                                         vr::render::SceneRenderPassRole::single,
                                         0U);

    vr::VulkanContext context{};
    vr::render::RenderTargetHost render_target{};
    vr::render::SceneRecorder3DPrepareView prepare_view{
        .device = context,
        .render_target = render_target,
    };
    for (std::uint32_t frame_index = 0U; frame_index < 4U; ++frame_index) {
        LightSystem3D::SetIntensity(lights[0U], 100.0F + static_cast<float>(frame_index));
        ShadowSystem3D::SetBias(shadows[0U],
                                0.001F + 0.0001F * static_cast<float>(frame_index),
                                0.0003F);
        TransformSystem3D::SetLocalPosition(
            light_transforms[0U],
            vr::ecs::Float3{.x = static_cast<float>(frame_index),
                            .y = 0.0F,
                            .z = 0.0F});
        TransformSystem3D::SetLocalPosition(
            shadow_transforms[0U],
            vr::ecs::Float3{.x = static_cast<float>(frame_index),
                            .y = 0.5F,
                            .z = 0.0F});
        TransformSystem3D::UpdateHierarchy(light_transforms.data(), 1U);
        TransformSystem3D::UpdateHierarchy(shadow_transforms.data(), 1U);

        recorder.PrepareFrame(prepare_view);

        VR_CHECK(light_coordinator.PendingLightDirtyHintCount() <= 1U);
        VR_CHECK(light_coordinator.PendingTransformDirtyHintCount() <= 1U);
        VR_CHECK(shadow_coordinator.PendingShadowDirtyHintCount() <= 1U);
        VR_CHECK(shadow_coordinator.PendingTransformDirtyHintCount() <= 1U);
    }
}

VR_TEST_CASE(SceneRecorder3DDirtyScheduler_surface_only_scene_keeps_light_shadow_hints_bounded,
             "unit;core;render;scene3d;dirty_scheduler") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();

    vr::render::LightFrameCoordinator<vr::ecs::Dim3> light_coordinator{};
    vr::render::ShadowFrameCoordinator<vr::ecs::Dim3> shadow_coordinator{};
    recorder.BindLightFrameCoordinator(&light_coordinator);
    recorder.BindShadowRuntime(&shadow_coordinator, nullptr);

    std::array<Light3D, 1U> lights{};
    std::array<Transform3D, 1U> light_transforms{};
    std::array<Shadow3D, 1U> shadows{};
    std::array<Transform3D, 1U> shadow_transforms{};
    LightSystem3D::Initialize(lights[0U]);
    ShadowSystem3D::Initialize(shadows[0U]);
    InitializeTransform(light_transforms[0U], 0.0F);
    InitializeTransform(shadow_transforms[0U], 0.0F);
    TransformSystem3D::UpdateHierarchy(light_transforms.data(), 1U);
    TransformSystem3D::UpdateHierarchy(shadow_transforms.data(), 1U);
    light_coordinator.SetLightData(lights.data(), light_transforms.data(), 1U);
    shadow_coordinator.SetShadowData(shadows.data(), shadow_transforms.data(), 1U);

    NoopSceneRenderer3D surface_only_renderer{};
    recorder.RegisterTransparentSceneRenderer(surface_only_renderer,
                                              vr::render::SceneRenderPassRole::single);

    vr::VulkanContext context{};
    vr::render::RenderTargetHost render_target{};
    vr::render::SceneRecorder3DPrepareView prepare_view{
        .device = context,
        .render_target = render_target,
    };
    for (std::uint32_t frame_index = 0U; frame_index < 4U; ++frame_index) {
        LightSystem3D::SetRange(lights[0U], 5.0F + static_cast<float>(frame_index));
        ShadowSystem3D::SetMapResolution(shadows[0U], 512U + frame_index, 512U);
        TransformSystem3D::SetLocalPosition(
            light_transforms[0U],
            vr::ecs::Float3{.x = 0.25F * static_cast<float>(frame_index),
                            .y = 1.0F,
                            .z = 0.0F});
        TransformSystem3D::SetLocalPosition(
            shadow_transforms[0U],
            vr::ecs::Float3{.x = 0.25F * static_cast<float>(frame_index),
                            .y = 1.5F,
                            .z = 0.0F});
        TransformSystem3D::UpdateHierarchy(light_transforms.data(), 1U);
        TransformSystem3D::UpdateHierarchy(shadow_transforms.data(), 1U);

        recorder.PrepareFrame(prepare_view);

        VR_CHECK(light_coordinator.PendingLightDirtyHintCount() <= 1U);
        VR_CHECK(light_coordinator.PendingTransformDirtyHintCount() <= 1U);
        VR_CHECK(shadow_coordinator.PendingShadowDirtyHintCount() <= 1U);
        VR_CHECK(shadow_coordinator.PendingTransformDirtyHintCount() <= 1U);
    }
}
