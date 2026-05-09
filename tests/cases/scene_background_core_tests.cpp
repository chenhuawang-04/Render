#include "support/test_framework.hpp"
#include "vr/render/render_view.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/environment/sky_environment_gpu_host.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/scene/scene.hpp"
#include "vr/scene/scene_prepare.hpp"
#include "vr/scene/scene_submission_builder.hpp"

#include <type_traits>

namespace {

[[nodiscard]] bool HasNonZeroSh9(const std::array<vr::ecs::Float4, 9U>& sh9_) {
    for (const vr::ecs::Float4& coefficient : sh9_) {
        if (coefficient.x != 0.0F || coefficient.y != 0.0F || coefficient.z != 0.0F) {
            return true;
        }
    }
    return false;
}

} // namespace

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

VR_TEST_CASE(ScenePacket_signature_changes_on_environment_binding, "[scene][background][render]") {
    vr::render::RenderView3D view{};
    view.viewport.width = 192.0F;
    view.viewport.height = 108.0F;
    view.scissor.width = 192U;
    view.scissor.height = 108U;
    vr::render::RefreshRenderViewSignature(view);

    vr::render::RenderScenePacket3D packet =
        vr::render::MakeSingleViewScenePacket(view, 19U, vr::render::RenderScenePacketKind::world);
    packet.extra.environment.mode = vr::scene::SkyEnvironmentMode::cubemap;
    packet.extra.environment.revision = 4U;
    packet.extra.environment_gpu = vr::scene::SkyEnvironmentGpuHandle{.index = 2U, .generation = 1U};
    vr::render::RefreshRenderScenePacketSignature(packet);
    const std::uint64_t signature_without_ibl = packet.signature;

    packet.extra.ibl_environment_id = 77U;
    vr::render::RefreshRenderScenePacketSignature(packet);
    const std::uint64_t signature_with_ibl = packet.signature;

    VR_CHECK(signature_without_ibl != signature_with_ibl);
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

VR_TEST_CASE(SceneSubmissionBuilder_3D_applies_active_view_environment_override,
             "[scene][background][render]") {
    vr::scene::Scene3D scene{};
    scene.background.mode = vr::scene::SkyEnvironmentMode::solid_color;
    scene.background.zenith_color = vr::ecs::Float4{.x = 0.4F, .y = 0.5F, .z = 0.6F, .w = 1.0F};
    scene.background.revision = 9U;

    vr::render::RenderView3D views[1]{};
    views[0].background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    views[0].background_override.state.mode = vr::scene::SkyEnvironmentMode::gradient;
    views[0].background_override.state.zenith_color = vr::ecs::Float4{.x = 0.1F, .y = 0.2F, .z = 0.3F, .w = 1.0F};
    views[0].background_override.state.horizon_color = vr::ecs::Float4{.x = 0.8F, .y = 0.7F, .z = 0.6F, .w = 1.0F};
    views[0].background_override.state.revision = 15U;
    views[0].background_override.gpu = vr::scene::SkyEnvironmentGpuHandle{.index = 3U, .generation = 2U};
    vr::render::RefreshRenderViewSignature(views[0]);

    const vr::render::RenderScenePacket3D packet =
        vr::scene::SceneSubmissionBuilder<vr::ecs::Dim3, vr::scene::SkyEnvironment>::Build(
            scene,
            views,
            1U,
            0U,
            12U);

    VR_CHECK(packet.extra.environment.mode == vr::scene::SkyEnvironmentMode::gradient);
    VR_CHECK(packet.extra.environment.revision == 15U);
    VR_CHECK(packet.extra.environment_gpu.index == 3U);
    VR_CHECK(packet.extra.environment.horizon_color.x == 0.8F);
}

VR_TEST_CASE(SceneSubmissionBuilder_3D_multiview_uses_active_view_environment_override,
             "[scene][background][render]") {
    vr::scene::Scene3D scene{};
    scene.background.mode = vr::scene::SkyEnvironmentMode::solid_color;
    scene.background.zenith_color = vr::ecs::Float4{.x = 0.5F, .y = 0.4F, .z = 0.3F, .w = 1.0F};
    scene.background.revision = 6U;

    vr::render::RenderView3D views[2]{};
    views[0].background_override.mode = vr::render::BackgroundOverrideMode::inherit;
    views[1].background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    views[1].background_override.state.mode = vr::scene::SkyEnvironmentMode::gradient;
    views[1].background_override.state.zenith_color =
        vr::ecs::Float4{.x = 0.12F, .y = 0.22F, .z = 0.32F, .w = 1.0F};
    views[1].background_override.state.horizon_color =
        vr::ecs::Float4{.x = 0.82F, .y = 0.72F, .z = 0.62F, .w = 1.0F};
    views[1].background_override.state.ground_color =
        vr::ecs::Float4{.x = 0.08F, .y = 0.07F, .z = 0.06F, .w = 1.0F};
    views[1].background_override.state.revision = 21U;
    views[1].background_override.gpu = vr::scene::SkyEnvironmentGpuHandle{.index = 7U, .generation = 9U};
    vr::render::RefreshRenderViewSignature(views[0]);
    vr::render::RefreshRenderViewSignature(views[1]);

    const vr::render::RenderScenePacket3D packet =
        vr::scene::SceneSubmissionBuilder<vr::ecs::Dim3, vr::scene::SkyEnvironment>::Build(
            scene,
            views,
            2U,
            1U,
            31U);

    VR_CHECK(packet.extra.environment.mode == vr::scene::SkyEnvironmentMode::gradient);
    VR_CHECK(packet.extra.environment.revision == 21U);
    VR_CHECK(packet.extra.environment_gpu.index == 7U);
    VR_CHECK(packet.extra.environment_gpu.generation == 9U);
    VR_CHECK(packet.extra.environment.horizon_color.x == 0.82F);
    VR_CHECK(packet.extra.environment.ground_color.z == 0.06F);
}

VR_TEST_CASE(ScenePrepare_3D_resolve_environment_preserves_atmosphere_params,
             "[scene][background][render]") {
    vr::scene::SkyEnvironment environment{};
    environment.mode = vr::scene::SkyEnvironmentMode::procedural_atmosphere;
    environment.draw_order = vr::scene::SkyEnvironmentDrawOrder::after_opaque_depth_tested;
    environment.sun_elevation = 0.42F;
    environment.sun_azimuth = -1.35F;
    environment.atmosphere_density = 1.8F;
    environment.mie_scattering = 2.4F;
    environment.rayleigh_scattering = 0.95F;
    environment.revision = 17U;

    const vr::scene::SkyEnvironmentRenderState state =
        vr::scene::ScenePrepare<vr::ecs::Dim3, vr::scene::SkyEnvironment>::ResolveEnvironment(
            environment);

    VR_CHECK(state.mode == vr::scene::SkyEnvironmentMode::procedural_atmosphere);
    VR_CHECK(state.draw_order == vr::scene::SkyEnvironmentDrawOrder::after_opaque_depth_tested);
    VR_CHECK(state.sun_elevation == 0.42F);
    VR_CHECK(state.sun_azimuth == -1.35F);
    VR_CHECK(state.atmosphere_density == 1.8F);
    VR_CHECK(state.mie_scattering == 2.4F);
    VR_CHECK(state.rayleigh_scattering == 0.95F);
    VR_CHECK(state.revision == 17U);
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

VR_TEST_CASE(SkyEnvironmentGpuHost_explicit_ibl_textures_are_resolved_without_bake,
             "[scene][background][render]") {
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
    state.mode = vr::scene::SkyEnvironmentMode::cubemap;
    state.sky_texture_id = 3U;
    state.irradiance_texture_id = 11U;
    state.prefiltered_texture_id = 12U;
    state.brdf_lut_texture_id = 13U;
    state.revision = 8U;

    const auto handle = host.RegisterOrUpdate(state, prepare_view);
    const auto& ibl = host.IblData(handle);

    VR_CHECK(handle.IsValid());
    VR_CHECK(ibl.irradiance_texture.value == 11U);
    VR_CHECK(ibl.prefiltered_texture.value == 12U);
    VR_CHECK(ibl.brdf_lut_texture.value == 13U);
    VR_CHECK(ibl.pending_bake == 0U);
    VR_CHECK(host.PendingBakeDesc(handle) == nullptr);

    host.Shutdown(context);
}

VR_TEST_CASE(SkyEnvironmentGpuHost_hdri_without_ibl_marks_pending_bake,
             "[scene][background][render]") {
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
    state.mode = vr::scene::SkyEnvironmentMode::equirectangular_hdr;
    state.sky_texture_id = 21U;
    state.revision = 9U;

    const auto handle = host.RegisterOrUpdate(state, prepare_view);
    const auto* bake_desc = host.PendingBakeDesc(handle);
    const auto& ibl = host.IblData(handle);

    VR_CHECK(handle.IsValid());
    VR_CHECK(bake_desc != nullptr);
    VR_CHECK(bake_desc->source_texture_id == 21U);
    VR_CHECK(bake_desc->source_mode == vr::scene::SkyEnvironmentMode::equirectangular_hdr);
    VR_CHECK(ibl.pending_bake == 1U);
    VR_CHECK(ibl.uses_shared_brdf_lut == 1U);
    VR_CHECK(host.Stats().bake_request_count == 1U);

    host.Shutdown(context);
}

VR_TEST_CASE(SkyEnvironmentGpuHost_procedural_atmosphere_parameters_affect_equivalence,
             "[scene][background][render]") {
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
    state_a.mode = vr::scene::SkyEnvironmentMode::procedural_atmosphere;
    state_a.sun_elevation = 0.35F;
    state_a.sun_azimuth = -0.4F;
    state_a.atmosphere_density = 1.2F;
    state_a.mie_scattering = 1.8F;
    state_a.rayleigh_scattering = 0.9F;
    state_a.revision = 12U;

    vr::scene::SkyEnvironmentRenderState state_b = state_a;
    state_b.sun_elevation = 0.65F;

    const auto handle_a = host.RegisterOrUpdate(state_a, prepare_view);
    const auto handle_b = host.RegisterOrUpdate(state_b, prepare_view);

    VR_CHECK(handle_a.IsValid());
    VR_CHECK(handle_b.IsValid());
    VR_CHECK(handle_a.index != handle_b.index);
    VR_CHECK(host.Stats().environment_count == 2U);
    VR_CHECK(host.Stats().cache_hit_count == 0U);

    host.Shutdown(context);
}

VR_TEST_CASE(SkyEnvironmentGpuHost_analytic_environments_build_nonzero_sh9,
             "[scene][background][render]") {
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

    vr::scene::SkyEnvironmentRenderState solid{};
    solid.mode = vr::scene::SkyEnvironmentMode::solid_color;
    solid.zenith_color = vr::ecs::Float4{.x = 0.25F, .y = 0.40F, .z = 0.60F, .w = 1.0F};
    solid.diffuse_ibl_intensity = 1.0F;
    solid.revision = 13U;

    vr::scene::SkyEnvironmentRenderState gradient{};
    gradient.mode = vr::scene::SkyEnvironmentMode::gradient;
    gradient.zenith_color = vr::ecs::Float4{.x = 0.08F, .y = 0.16F, .z = 0.42F, .w = 1.0F};
    gradient.horizon_color = vr::ecs::Float4{.x = 0.60F, .y = 0.56F, .z = 0.48F, .w = 1.0F};
    gradient.ground_color = vr::ecs::Float4{.x = 0.10F, .y = 0.08F, .z = 0.06F, .w = 1.0F};
    gradient.diffuse_ibl_intensity = 1.0F;
    gradient.revision = 14U;

    vr::scene::SkyEnvironmentRenderState atmosphere{};
    atmosphere.mode = vr::scene::SkyEnvironmentMode::procedural_atmosphere;
    atmosphere.zenith_color = vr::ecs::Float4{.x = 0.07F, .y = 0.18F, .z = 0.46F, .w = 1.0F};
    atmosphere.horizon_color = vr::ecs::Float4{.x = 0.74F, .y = 0.50F, .z = 0.24F, .w = 1.0F};
    atmosphere.ground_color = vr::ecs::Float4{.x = 0.09F, .y = 0.07F, .z = 0.06F, .w = 1.0F};
    atmosphere.tint = vr::ecs::Float4{.x = 1.0F, .y = 0.96F, .z = 0.92F, .w = 1.0F};
    atmosphere.sky_intensity = 1.15F;
    atmosphere.diffuse_ibl_intensity = 1.0F;
    atmosphere.sun_elevation = 0.52F;
    atmosphere.sun_azimuth = -0.65F;
    atmosphere.atmosphere_density = 1.45F;
    atmosphere.mie_scattering = 2.2F;
    atmosphere.rayleigh_scattering = 1.15F;
    atmosphere.revision = 15U;

    const auto solid_handle = host.RegisterOrUpdate(solid, prepare_view);
    const auto gradient_handle = host.RegisterOrUpdate(gradient, prepare_view);
    const auto atmosphere_handle = host.RegisterOrUpdate(atmosphere, prepare_view);

    VR_REQUIRE(solid_handle.IsValid());
    VR_REQUIRE(gradient_handle.IsValid());
    VR_REQUIRE(atmosphere_handle.IsValid());
    VR_CHECK(HasNonZeroSh9(host.Params(solid_handle).sh9));
    VR_CHECK(HasNonZeroSh9(host.Params(gradient_handle).sh9));
    VR_CHECK(HasNonZeroSh9(host.Params(atmosphere_handle).sh9));

    const auto& atmosphere_ibl = host.IblData(atmosphere_handle);
    VR_CHECK(atmosphere_ibl.pending_bake == 0U);
    VR_CHECK(atmosphere_ibl.uses_shared_brdf_lut == 0U);

    host.Shutdown(context);
}

VR_TEST_CASE(SkyEnvironmentGpuHost_shared_brdf_lut_resolution_updates_ibl_revision,
             "[scene][background][render]") {
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
    state.mode = vr::scene::SkyEnvironmentMode::equirectangular_hdr;
    state.sky_texture_id = 31U;
    state.revision = 10U;

    const auto handle = host.RegisterOrUpdate(state, prepare_view);
    const auto before = host.IblData(handle);
    const bool changed =
        host.ResolveSharedBrdfLut(handle, vr::asset::TextureId{.value = 77U});
    const auto after = host.IblData(handle);

    VR_CHECK(changed);
    VR_CHECK(before.brdf_lut_texture.value == 0U);
    VR_CHECK(after.brdf_lut_texture.value == 77U);
    VR_CHECK(after.uses_shared_brdf_lut == 1U);
    VR_CHECK(after.revision > before.revision);
    VR_CHECK(host.Stats().shared_brdf_lut_resolve_count == 1U);

    host.Shutdown(context);
}

VR_TEST_CASE(SkyEnvironmentGpuHost_apply_bake_result_clears_pending_state,
             "[scene][background][render]") {
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
    state.mode = vr::scene::SkyEnvironmentMode::equirectangular_hdr;
    state.sky_texture_id = 41U;
    state.revision = 11U;

    const auto handle = host.RegisterOrUpdate(state, prepare_view);
    VR_CHECK(host.HasPendingBake(handle));

    const bool changed = host.ApplyBakeResult(handle,
                                              vr::render::SkyEnvironmentBakeResult{
                                                  .irradiance_texture_id = 101U,
                                                  .prefiltered_texture_id = 102U,
                                                  .brdf_lut_texture_id = 103U,
                                                  .revision = 55U,
                                              });
    const auto ibl = host.IblData(handle);

    VR_CHECK(changed);
    VR_CHECK(!host.HasPendingBake(handle));
    VR_CHECK(host.PendingBakeDesc(handle) == nullptr);
    VR_CHECK(ibl.pending_bake == 0U);
    VR_CHECK(ibl.irradiance_texture.value == 101U);
    VR_CHECK(ibl.prefiltered_texture.value == 102U);
    VR_CHECK(ibl.brdf_lut_texture.value == 103U);
    VR_CHECK(ibl.revision == 55U);
    VR_CHECK(host.Stats().bake_apply_count == 1U);

    host.Shutdown(context);
}

VR_TEST_CASE(SceneRecorder3D_resolves_environment_gpu_handle_from_prepare_view_host,
             "[scene][background][render]") {
    vr::VulkanContext context{};
    vr::render::RenderTargetHost render_target{};
    vr::asset::TextureHost texture_host{};
    vr::render::DescriptorHost descriptor_host{};
    vr::resource::SamplerHost sampler_host{};
    vr::render::UploadHost upload_host{};
    vr::render::SkyEnvironmentGpuHost sky_environment_host{};
    sky_environment_host.Initialize(context, texture_host, descriptor_host, sampler_host, {});

    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();

    vr::render::RenderView3D view{};
    vr::render::RefreshRenderViewSignature(view);
    vr::render::RenderScenePacket3D packet =
        vr::render::MakeSingleViewScenePacket(view, 21U, vr::render::RenderScenePacketKind::world);
    packet.extra.environment.mode = vr::scene::SkyEnvironmentMode::cubemap;
    packet.extra.environment.sky_texture_id = 7U;
    packet.extra.environment.revision = 4U;

    recorder.PrepareFrame(vr::render::SceneRecorder3DPrepareView{
                              .device = context,
                              .texture = &texture_host,
                              .upload = &upload_host,
                              .descriptor = &descriptor_host,
                              .sky_environment = &sky_environment_host,
                              .render_target = render_target,
                              .sampler = &sampler_host,
                          },
                          packet);

    VR_CHECK(sky_environment_host.Stats().prepared_frame_count == 1U);
    VR_CHECK(sky_environment_host.Stats().environment_count == 1U);
    VR_CHECK(recorder.Stats().environment_gpu_resolve_count == 1U);

    recorder.Shutdown(context);
    sky_environment_host.Shutdown(context);
}

VR_TEST_CASE(SceneRecorder3D_prepare_view_propagates_environment_ibl_binding,
             "[scene][background][render]") {
    vr::VulkanContext context{};
    vr::render::RenderTargetHost render_target{};
    vr::resource::GpuMemoryHost gpu_memory_host{};
    vr::render::UploadHost upload_host{};
    vr::render::DescriptorHost descriptor_host{};
    vr::render::PipelineHost pipeline_host{};
    vr::render::IblHost ibl_host{};
    vr::resource::SamplerHost sampler_host{};

    const vr::render::SceneRecorder3DPrepareView prepare_view{
        .device = context,
        .gpu_memory = &gpu_memory_host,
        .upload = &upload_host,
        .descriptor = &descriptor_host,
        .ibl = &ibl_host,
        .pipeline = &pipeline_host,
        .render_target = render_target,
        .sampler = &sampler_host,
        .ibl_environment_id = 17U,
        .ibl_brdf_lut_texture_id = 23U,
    };

    const auto geometry_prepare_view = vr::render::MakeGeometryRenderer3DPrepareView(prepare_view);
    const auto surface_prepare_view = vr::render::MakeSurfaceRenderer3DPrepareView(prepare_view);

    VR_CHECK(geometry_prepare_view.ibl_environment_id == 17U);
    VR_CHECK(geometry_prepare_view.ibl_brdf_lut_texture_id == 23U);
    VR_CHECK(surface_prepare_view.ibl_environment_id == 17U);
    VR_CHECK(surface_prepare_view.ibl_brdf_lut_texture_id == 23U);
}
