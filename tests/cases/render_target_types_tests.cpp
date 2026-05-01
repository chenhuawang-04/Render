#include "support/test_framework.hpp"
#include "vr/render/render_pass_preset.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/render_target_desc.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/render_target_types.hpp"
#include "vr/render/scene_render_target_set.hpp"

#include <type_traits>

namespace {

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

} // namespace
