#include "support/test_framework.hpp"
#include "vr/render/render_view.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render/environment/sky_environment_gpu_host.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/scene/scene.hpp"
#include "vr/scene/scene_prepare.hpp"
#include "vr/scene/scene_submission_builder.hpp"

#include <type_traits>

VR_TEST_CASE(SceneBackgroundTraits_2D, "[scene][background]") {
    VR_CHECK((std::is_same_v<
              vr::scene::SceneBackgroundTraits<vr::ecs::Dim2, vr::scene::SpriteBackground>::RenderState,
              vr::scene::Background2DRenderState>));
    VR_CHECK((vr::scene::SceneBackgroundTraits<vr::ecs::Dim2, vr::scene::SpriteBackground>::uses_surface_path));
    VR_CHECK((!vr::scene::SceneBackgroundTraits<vr::ecs::Dim2, vr::scene::SpriteBackground>::uses_environment_gpu));
}

VR_TEST_CASE(SceneBackgroundTraits_3D, "[scene][background]") {
    VR_CHECK((std::is_same_v<
              vr::scene::SceneBackgroundTraits<vr::ecs::Dim3, vr::scene::SkyEnvironment>::RenderState,
              vr::scene::SkyEnvironmentRenderState>));
    VR_CHECK((!vr::scene::SceneBackgroundTraits<vr::ecs::Dim3, vr::scene::SkyEnvironment>::uses_surface_path));
    VR_CHECK((vr::scene::SceneBackgroundTraits<vr::ecs::Dim3, vr::scene::SkyEnvironment>::uses_environment_gpu));
}

VR_TEST_CASE(SpriteBackground_is_pod, "[scene][background]") {
    VR_CHECK((std::is_standard_layout_v<vr::scene::SpriteBackground>));
    VR_CHECK((std::is_trivial_v<vr::scene::SpriteBackground>));
    VR_CHECK((std::is_standard_layout_v<vr::scene::Background2DRenderState>));
    VR_CHECK((std::is_trivial_v<vr::scene::Background2DRenderState>));
}

VR_TEST_CASE(SkyEnvironment_is_pod, "[scene][background]") {
    VR_CHECK((std::is_standard_layout_v<vr::scene::SkyEnvironment>));
    VR_CHECK((std::is_trivial_v<vr::scene::SkyEnvironment>));
    VR_CHECK((std::is_standard_layout_v<vr::scene::SkyEnvironmentRenderState>));
    VR_CHECK((std::is_trivial_v<vr::scene::SkyEnvironmentRenderState>));
}

VR_TEST_CASE(ScenePacket_signature_changes_on_background_revision, "[scene][background][render]") {
    vr::render::RenderView2D view{};
    view.viewport.width = 128.0F;
    view.viewport.height = 128.0F;
    view.scissor.width = 128U;
    view.scissor.height = 128U;
    vr::render::RefreshRenderViewSignature(view);

    vr::render::RenderScenePacket2D packet =
        vr::render::MakeSingleViewScenePacket(view, 17U, vr::render::RenderScenePacketKind::world);
    const std::uint64_t base_signature = packet.signature;

    packet.extra.background.mode = vr::scene::Background2DMode::solid_color;
    packet.extra.background.revision = 42U;
    vr::render::RefreshRenderScenePacketSignature(packet);
    const std::uint64_t revised_signature = packet.signature;

    VR_CHECK(base_signature != revised_signature);
}

VR_TEST_CASE(RenderView_background_override, "[scene][background][render]") {
    vr::render::RenderView2D view{};
    view.viewport.width = 320.0F;
    view.viewport.height = 180.0F;
    view.scissor.width = 320U;
    view.scissor.height = 180U;
    vr::render::RefreshRenderViewSignature(view);
    const std::uint64_t inherit_signature = view.signature;

    view.background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    view.background_override.state.mode = vr::scene::Background2DMode::gradient;
    view.background_override.state.revision = 7U;
    vr::render::RefreshRenderViewSignature(view);
    const std::uint64_t override_signature = view.signature;

    VR_CHECK(inherit_signature != override_signature);
}

VR_TEST_CASE(SceneSubmissionBuilder_2D_applies_active_view_background_override,
             "[scene][background][render]") {
    vr::scene::Scene2D scene{};
    scene.background.mode = vr::scene::Background2DMode::solid_color;
    scene.background.color0 = vr::ecs::Float4{.x = 0.1F, .y = 0.2F, .z = 0.3F, .w = 1.0F};
    scene.background.revision = 3U;

    vr::render::RenderView2D views[2]{};
    views[0].background_override.mode = vr::render::BackgroundOverrideMode::inherit;
    views[1].background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    views[1].background_override.state.mode = vr::scene::Background2DMode::gradient;
    views[1].background_override.state.color0 = vr::ecs::Float4{.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    views[1].background_override.state.color1 = vr::ecs::Float4{.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 1.0F};
    views[1].background_override.state.revision = 11U;
    vr::render::RefreshRenderViewSignature(views[0]);
    vr::render::RefreshRenderViewSignature(views[1]);

    const vr::render::RenderScenePacket2D packet =
        vr::scene::SceneSubmissionBuilder<vr::ecs::Dim2, vr::scene::SpriteBackground>::Build(
            scene,
            views,
            2U,
            1U,
            9U);

    VR_CHECK(packet.extra.background.mode == vr::scene::Background2DMode::gradient);
    VR_CHECK(packet.extra.background.revision == 11U);
    VR_CHECK(packet.extra.background.color1.z == 1.0F);
}

VR_TEST_CASE(SkyEnvironmentGpuHost_descriptor_reuse, "[scene][background][render]") {
    vr::VulkanContext context{};
    vr::asset::TextureHost texture_host{};
    vr::render::DescriptorHost descriptor_host{};
    vr::resource::SamplerHost sampler_host{};
    vr::render::UploadHost upload_host{};
    vr::render::SkyEnvironmentGpuHost host{};
    host.Initialize(context, texture_host, descriptor_host, sampler_host, {});

    vr::render::SkyEnvironmentGpuPrepareView prepare_view{
        .device = context,
        .texture = texture_host,
        .upload = upload_host,
        .descriptor = descriptor_host,
        .sampler = sampler_host,
    };

    vr::scene::SkyEnvironmentRenderState state{};
    state.mode = vr::scene::SkyEnvironmentMode::gradient;
    state.revision = 5U;
    state.sky_intensity = 2.0F;

    const auto first = host.RegisterOrUpdate(state, prepare_view);
    const auto second = host.RegisterOrUpdate(state, prepare_view);

    VR_CHECK(first.IsValid());
    VR_CHECK(first.index == second.index);
    VR_CHECK(host.Stats().environment_count == 1U);
    VR_CHECK(host.Stats().cache_hit_count == 1U);

    host.Shutdown(context);
}

VR_TEST_CASE(SkyEnvironmentGpuHost_multi_environment, "[scene][background][render]") {
    vr::VulkanContext context{};
    vr::asset::TextureHost texture_host{};
    vr::render::DescriptorHost descriptor_host{};
    vr::resource::SamplerHost sampler_host{};
    vr::render::UploadHost upload_host{};
    vr::render::SkyEnvironmentGpuHost host{};
    host.Initialize(context, texture_host, descriptor_host, sampler_host, {});

    vr::render::SkyEnvironmentGpuPrepareView prepare_view{
        .device = context,
        .texture = texture_host,
        .upload = upload_host,
        .descriptor = descriptor_host,
        .sampler = sampler_host,
    };

    vr::scene::SkyEnvironmentRenderState state_a{};
    state_a.mode = vr::scene::SkyEnvironmentMode::solid_color;
    state_a.revision = 1U;
    state_a.exposure = 1.0F;

    vr::scene::SkyEnvironmentRenderState state_b{};
    state_b.mode = vr::scene::SkyEnvironmentMode::cubemap;
    state_b.sky_texture_id = 7U;
    state_b.revision = 2U;
    state_b.exposure = 3.0F;
    state_b.tint = vr::ecs::Float4{.x = 0.8F, .y = 0.7F, .z = 0.6F, .w = 1.0F};

    const auto handle_a = host.RegisterOrUpdate(state_a, prepare_view);
    const auto handle_b = host.RegisterOrUpdate(state_b, prepare_view);
    const auto& params_b = host.Params(handle_b);

    VR_CHECK(handle_a.IsValid());
    VR_CHECK(handle_b.IsValid());
    VR_CHECK(handle_a.index != handle_b.index);
    VR_CHECK(host.Stats().environment_count == 2U);
    VR_CHECK(params_b.tint_exposure.w == 3.0F);
    VR_CHECK(params_b.tint_exposure.x == 0.8F);

    host.Shutdown(context);
}
