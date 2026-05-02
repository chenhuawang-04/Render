#include "support/test_framework.hpp"
#include "vr/render/render_pass_preset.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/render_target_desc.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render/render_target_types.hpp"
#include "vr/render/scene_render_target_set.hpp"

#include <type_traits>

namespace {

struct FakeColorRenderer {
    vr::render::RenderTargetColorOutputConfig color_output{};
    bool color_set = false;
    bool color_reset = false;

    void SetOutputTargetConfig(const vr::render::RenderTargetColorOutputConfig& color_output_config_) noexcept {
        color_output = color_output_config_;
        color_set = true;
    }

    void ResetOutputTargetConfig() noexcept {
        color_output = {};
        color_reset = true;
    }
};

struct FakeDepthRenderer : FakeColorRenderer {
    vr::render::RenderTargetDepthOutputConfig depth_output{};
    bool depth_set = false;
    bool depth_reset = false;

    void SetDepthTargetConfig(const vr::render::RenderTargetDepthOutputConfig& depth_output_config_) noexcept {
        depth_output = depth_output_config_;
        depth_set = true;
    }

    void ResetDepthTargetConfig() noexcept {
        depth_output = {};
        depth_reset = true;
    }
};

struct FakeSceneRecorderRenderer : FakeDepthRenderer {
    std::uint32_t prepare_count = 0U;
    std::uint32_t record_count = 0U;
    std::uint32_t recreate_count = 0U;

    void PrepareFrame(const vr::render::RuntimePrepareContext&) noexcept {
        prepare_count += 1U;
    }

    void Record(const vr::render::FrameRecordContext&) noexcept {
        record_count += 1U;
    }

    void OnSwapchainRecreated(std::uint32_t,
                              VkExtent2D,
                              VkFormat,
                              std::uint64_t,
                              std::uint64_t) noexcept {
        recreate_count += 1U;
    }
};

struct FakePreSceneRecorderRenderer final {
    std::uint32_t prepare_count = 0U;
    std::uint32_t record_count = 0U;
    std::uint32_t recreate_count = 0U;

    void PrepareFrame(const vr::render::RuntimePrepareContext&) noexcept {
        prepare_count += 1U;
    }

    void Record(const vr::render::FrameRecordContext&) noexcept {
        record_count += 1U;
    }

    void OnSwapchainRecreated(std::uint32_t,
                              VkExtent2D,
                              VkFormat,
                              std::uint64_t,
                              std::uint64_t) noexcept {
        recreate_count += 1U;
    }
};

struct FakeLitSceneRecorderRenderer final : FakeSceneRecorderRenderer {
    vr::render::LightFrameCoordinator<vr::ecs::Dim3>* light_frame = nullptr;
    vr::render::LightShadowLinkCoordinator3D* light_shadow_link = nullptr;
    vr::render::ShadowAtlasBindingCoordinator* shadow_atlas_binding = nullptr;
    vr::render::ShadowFrameCoordinator<vr::ecs::Dim3>* shadow_frame = nullptr;
    vr::shadow::ShadowAtlasHost* shadow_atlas = nullptr;

    void SetLightFrameCoordinator(
        vr::render::LightFrameCoordinator<vr::ecs::Dim3>* light_frame_coordinator_) noexcept {
        light_frame = light_frame_coordinator_;
    }

    void SetLightShadowLinkCoordinator(
        vr::render::LightShadowLinkCoordinator3D* coordinator_) noexcept {
        light_shadow_link = coordinator_;
    }

    void SetShadowAtlasBindingCoordinator(
        vr::render::ShadowAtlasBindingCoordinator* coordinator_) noexcept {
        shadow_atlas_binding = coordinator_;
    }

    void SetShadowFrameCoordinator(
        vr::render::ShadowFrameCoordinator<vr::ecs::Dim3>* shadow_frame_coordinator_) noexcept {
        shadow_frame = shadow_frame_coordinator_;
    }

    void SetShadowAtlasHost(vr::shadow::ShadowAtlasHost* shadow_atlas_host_) noexcept {
        shadow_atlas = shadow_atlas_host_;
    }
};

struct FakeSceneConsumer final {
    vr::render::RenderTargetHandle source_target{};
    vr::render::RenderTargetStateKind expected_state = vr::render::RenderTargetStateKind::undefined;
    bool source_set = false;
    bool source_cleared = false;
    bool output_reset = false;

    void SetSceneSourceTarget(vr::render::RenderTargetHandle source_target_,
                              vr::render::RenderTargetStateKind expected_state_) noexcept {
        source_target = source_target_;
        expected_state = expected_state_;
        source_set = true;
        source_cleared = false;
    }

    void ClearSceneSourceTarget() noexcept {
        source_target = {};
        expected_state = vr::render::RenderTargetStateKind::undefined;
        source_cleared = true;
    }

    void ResetOutputTargetConfig() noexcept {
        output_reset = true;
    }
};

VR_TEST_CASE(RenderTargetTypes_handle_and_range_are_trivial, "unit;core;render_target") {
    VR_CHECK(std::is_standard_layout_v<vr::render::RenderTargetHandle>);
    VR_CHECK(std::is_standard_layout_v<vr::render::RenderTargetSubresourceRange>);
}

VR_TEST_CASE(RenderTargetTypes_invalid_handle_defaults_to_invalid, "unit;core;render_target") {
    const vr::render::RenderTargetHandle handle{};
    VR_CHECK(handle.index == vr::render::invalid_render_target_index);
    VR_CHECK(handle.generation == 0U);
    VR_CHECK(!vr::render::IsValidRenderTargetHandle(handle));
}

VR_TEST_CASE(RenderTargetTypes_desc_defaults_match_v1_expectations, "unit;core;render_target") {
    vr::render::RenderTargetDesc desc{};
    VR_CHECK(desc.dimension == vr::render::RenderTargetDimension::image_2d);
    VR_CHECK(desc.lifetime == vr::render::RenderTargetLifetime::persistent);
    VR_CHECK(desc.scale_mode == vr::render::RenderTargetScaleMode::absolute);
    VR_CHECK(desc.width == 1U);
    VR_CHECK(desc.height == 1U);
    VR_CHECK(desc.depth == 1U);
    VR_CHECK(desc.samples == VK_SAMPLE_COUNT_1_BIT);
    VR_CHECK(desc.memory_policy == vr::render::RenderTargetMemoryPolicy::auto_select);
}

VR_TEST_CASE(RenderTargetTypes_pipeline_signature_and_attachment_defaults_are_stable, "unit;core;render_target") {
    vr::render::RenderTargetPipelineSignature signature{};
    VR_CHECK(signature.color_attachment_count == 0U);
    VR_CHECK(signature.depth_format == VK_FORMAT_UNDEFINED);
    VR_CHECK(signature.stencil_format == VK_FORMAT_UNDEFINED);
    VR_CHECK(signature.samples == VK_SAMPLE_COUNT_1_BIT);

    vr::render::AttachmentRef attachment{};
    VR_CHECK(attachment.load_op == VK_ATTACHMENT_LOAD_OP_LOAD);
    VR_CHECK(attachment.store_op == VK_ATTACHMENT_STORE_OP_STORE);
    VR_CHECK(attachment.expected_state == vr::render::RenderTargetStateKind::color_attachment);
    VR_CHECK(!attachment.use_resolve);

    vr::render::RenderTargetDepthOutputConfig depth_output{};
    VR_CHECK(depth_output.final_state == vr::render::RenderTargetStateKind::depth_attachment);
    VR_CHECK(depth_output.load_op == VK_ATTACHMENT_LOAD_OP_LOAD);
    VR_CHECK(depth_output.store_op == VK_ATTACHMENT_STORE_OP_STORE);
    VR_CHECK(depth_output.clear_depth_stencil.depth == 1.0F);
}

VR_TEST_CASE(RenderTargetTypes_state_mapping_matches_v1_contract, "unit;core;render_target") {
    const auto color_state = vr::render::RenderTargetHost::DescribeState(
        vr::render::RenderTargetStateKind::color_attachment,
        VK_IMAGE_ASPECT_COLOR_BIT);
    VR_CHECK(color_state.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VR_CHECK((color_state.stage_mask & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) != 0U);
    VR_CHECK((color_state.access_mask & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) != 0U);

    const auto sampled_color_state = vr::render::RenderTargetHost::DescribeState(
        vr::render::RenderTargetStateKind::shader_read,
        VK_IMAGE_ASPECT_COLOR_BIT);
    VR_CHECK(sampled_color_state.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VR_CHECK((sampled_color_state.access_mask & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) != 0U);

    const auto sampled_depth_state = vr::render::RenderTargetHost::DescribeState(
        vr::render::RenderTargetStateKind::shader_read,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    VR_CHECK(sampled_depth_state.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    const auto present_state = vr::render::RenderTargetHost::DescribeState(
        vr::render::RenderTargetStateKind::present_src,
        VK_IMAGE_ASPECT_COLOR_BIT);
    VR_CHECK(present_state.layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

VR_TEST_CASE(SceneRenderTargetSet_defaults_match_render_target_v1_contract, "unit;core;render_target") {
    vr::render::SceneRenderTargetSetCreateInfo create_info{};
    VR_CHECK(create_info.enable_depth);
    VR_CHECK(create_info.scale_mode == vr::render::RenderTargetScaleMode::swapchain_relative);
    VR_CHECK(create_info.color_final_state == vr::render::RenderTargetStateKind::shader_read);
    VR_CHECK(create_info.depth_final_state == vr::render::RenderTargetStateKind::depth_attachment);
    VR_CHECK(create_info.samples == VK_SAMPLE_COUNT_1_BIT);

    vr::render::SceneRenderTargetSet target_set{};
    target_set.Initialize(create_info);
    VR_CHECK(!target_set.IsReady());
    VR_CHECK(!target_set.HasDepthTarget());
    VR_CHECK(target_set.ColorFormat() == VK_FORMAT_UNDEFINED);
    VR_CHECK(target_set.DepthFormat() == VK_FORMAT_UNDEFINED);
    VR_CHECK(target_set.CreateInfo().enable_depth);

    target_set.Reset();
    VR_CHECK(!target_set.IsReady());
}

VR_TEST_CASE(SceneRenderTargetSet_not_ready_configuration_resets_renderer_bindings,
             "unit;core;render_target") {
    vr::render::SceneRenderTargetSet target_set{};
    target_set.Initialize({});

    FakeDepthRenderer renderer{};
    const bool configured =
        target_set.ConfigureSceneRenderer(renderer, vr::render::SceneRenderPassRole::single);

    VR_CHECK(!configured);
    VR_CHECK(!renderer.color_set);
    VR_CHECK(!renderer.depth_set);
    VR_CHECK(renderer.color_reset);
    VR_CHECK(renderer.depth_reset);
}

VR_TEST_CASE(SceneRenderTargetSet_bind_scene_renderer_preserves_role,
             "unit;core;render_target") {
    FakeColorRenderer renderer{};
    const auto binding =
        vr::render::BindSceneRenderer(renderer, vr::render::SceneRenderPassRole::middle);
    VR_CHECK(binding.renderer == &renderer);
    VR_CHECK(binding.pass_role == vr::render::SceneRenderPassRole::middle);
}

VR_TEST_CASE(SceneRenderTargetSet_not_ready_scene_consumer_resets_source_binding,
             "unit;core;render_target") {
    vr::render::SceneRenderTargetSet target_set{};
    target_set.Initialize({});

    FakeSceneConsumer consumer{};
    const bool configured = target_set.ConfigureSceneConsumer(consumer);

    VR_CHECK(!configured);
    VR_CHECK(!consumer.source_set);
    VR_CHECK(consumer.source_cleared);
    VR_CHECK(consumer.output_reset);
}

VR_TEST_CASE(SceneRecorder3D_overlay_output_defaults_match_present_overlay_contract,
             "unit;core;render_target") {
    const vr::render::RenderTargetColorOutputConfig overlay_output =
        vr::render::SceneRecorder3D::MakePresentOverlayOutputConfig();
    VR_CHECK(overlay_output.final_state == vr::render::RenderTargetStateKind::present_src);
    VR_CHECK(overlay_output.use_explicit_load_op);
    VR_CHECK(overlay_output.load_op == VK_ATTACHMENT_LOAD_OP_LOAD);
    VR_CHECK(overlay_output.store_op == VK_ATTACHMENT_STORE_OP_STORE);
}

VR_TEST_CASE(SceneRecorder3D_registration_upserts_renderer_counts,
             "unit;core;render_target") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize({});

    FakePreSceneRecorderRenderer pre_scene_renderer{};
    FakeSceneRecorderRenderer scene_renderer{};
    FakeSceneRecorderRenderer overlay_renderer{};

    recorder.RegisterPreSceneRenderer(pre_scene_renderer);
    recorder.RegisterSceneRenderer(scene_renderer, vr::render::SceneRenderPassRole::first);
    recorder.RegisterSceneRenderer(scene_renderer, vr::render::SceneRenderPassRole::last);
    recorder.RegisterOverlayRenderer(overlay_renderer);
    recorder.RegisterOverlayRenderer(overlay_renderer,
                                     vr::render::SceneRecorder3D::MakePresentOverlayOutputConfig());

    const vr::render::SceneRecorder3DStats stats = recorder.Stats();
    VR_CHECK(stats.pre_scene_renderer_count == 1U);
    VR_CHECK(stats.scene_renderer_count == 1U);
    VR_CHECK(stats.overlay_renderer_count == 1U);
    VR_CHECK(recorder.IsInitialized());
    VR_CHECK(!recorder.HasRuntimeBinding());
}

VR_TEST_CASE(SceneRecorder3D_shadow_registration_and_lighting_binding_are_propagated,
             "unit;core;render_target") {
    vr::render::SceneRecorder3D recorder{};
    vr::render::SceneRecorder3DCreateInfo create_info{};
    create_info.reserve_pre_scene_renderer_count = 2U;
    recorder.Initialize(create_info);

    FakeLitSceneRecorderRenderer lit_renderer{};
    vr::render::LightFrameCoordinator<vr::ecs::Dim3> light_frame_coordinator{};
    vr::shadow::ShadowRenderer3D shadow_renderer{};

    recorder.BindLightFrameCoordinator(&light_frame_coordinator);
    recorder.RegisterSceneRenderer(lit_renderer, vr::render::SceneRenderPassRole::single);
    recorder.RegisterShadowRenderer(shadow_renderer);

    const vr::render::SceneRecorder3DStats registered_stats = recorder.Stats();
    VR_CHECK(registered_stats.pre_scene_renderer_count == 1U);
    VR_CHECK(registered_stats.scene_renderer_count == 1U);
    VR_CHECK(lit_renderer.light_frame == &light_frame_coordinator);
    VR_CHECK(lit_renderer.light_shadow_link != nullptr);
    VR_CHECK(lit_renderer.shadow_atlas_binding != nullptr);
    VR_CHECK(lit_renderer.shadow_frame == &shadow_renderer.FrameCoordinatorMutable());
    VR_CHECK(lit_renderer.shadow_atlas == &shadow_renderer.AtlasHostMutable());

    recorder.BindLightFrameCoordinator(nullptr);
    recorder.ClearShadowRuntimeBinding();
    VR_CHECK(lit_renderer.light_frame == nullptr);
    VR_CHECK(lit_renderer.light_shadow_link != nullptr);
    VR_CHECK(lit_renderer.shadow_atlas_binding != nullptr);
    VR_CHECK(lit_renderer.shadow_frame == nullptr);
    VR_CHECK(lit_renderer.shadow_atlas == nullptr);
}

} // namespace
