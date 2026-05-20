#include "support/test_framework.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <string>

namespace {

struct AttachmentDecisionFixture final {
    vr::render_graph::RenderGraphBuilder builder{};
    vr::render_graph::ResourceHandle color{};
    vr::render_graph::ResourceHandle depth{};
    vr::render_graph::ResourceHandle present_target{};
};

[[nodiscard]] vr::render_graph::TextureDesc MakeColorTargetDesc() {
    return vr::render_graph::TextureDesc{
        .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
        .extent = {.width = 320U, .height = 180U, .depth = 1U},
        .usage = vr::render_graph::texture_usage_color_attachment_flag |
                 vr::render_graph::texture_usage_sampled_flag,
    };
}

[[nodiscard]] vr::render_graph::TextureDesc MakeDepthTargetDesc() {
    return vr::render_graph::TextureDesc{
        .format = vr::render_graph::TextureFormat::d32_sfloat,
        .extent = {.width = 320U, .height = 180U, .depth = 1U},
        .usage =
            vr::render_graph::texture_usage_depth_stencil_attachment_flag,
    };
}

[[nodiscard]] AttachmentDecisionFixture MakeFixture() {
    AttachmentDecisionFixture fixture{};
    fixture.color = fixture.builder.CreateTexture("scene_color",
                                                  MakeColorTargetDesc());
    fixture.depth = fixture.builder.CreateTexture("scene_depth",
                                                  MakeDepthTargetDesc());
    fixture.present_target = fixture.builder.CreateTexture(
        "present_target",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::unknown,
            .extent = {.width = 320U, .height = 180U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_present_flag,
        },
        vr::render_graph::ResourceLifetime::imported);
    return fixture;
}

VR_TEST_CASE(RenderGraphLoadStoreOptimizer_infers_transient_first_use_loads_and_last_use_stores,
             "unit;core;render_graph;native_pass;load_store") {
    auto fixture = MakeFixture();
    const auto gbuffer = fixture.builder.AddPass("gbuffer", true);

    (void)fixture.builder.Write(
        gbuffer,
        fixture.color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Write(
        gbuffer,
        fixture.depth,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });
    fixture.builder.SetRasterPassDesc(
        gbuffer,
        vr::render_graph::RasterPassDesc{
            .color_attachments = {
                vr::render_graph::RasterColorAttachmentDesc{
                    .target = fixture.color,
                    .load_op = vr::render_graph::AttachmentLoadOp::load,
                    .store_op = vr::render_graph::AttachmentStoreOp::store,
                },
            },
            .has_depth_attachment = true,
            .depth_attachment = vr::render_graph::RasterDepthAttachmentDesc{
                .target = fixture.depth,
                .load_op = vr::render_graph::AttachmentLoadOp::load,
                .store_op = vr::render_graph::AttachmentStoreOp::store,
                .stencil_load_op = vr::render_graph::AttachmentLoadOp::dont_care,
                .stencil_store_op = vr::render_graph::AttachmentStoreOp::dont_care,
                .read_only = false,
            },
        });

    const auto compiled = fixture.builder.Compile();
    const auto& native_plan = compiled.NativePasses();

    VR_REQUIRE(native_plan.groups.size() == 1U);
    const auto& group = native_plan.groups[0];
    VR_REQUIRE(group.attachments.color_attachments.size() == 1U);

    const auto& color_attachment = group.attachments.color_attachments[0];
    VR_CHECK(color_attachment.requested_load_op ==
             vr::render_graph::AttachmentLoadOp::load);
    VR_CHECK(color_attachment.effective_load_op ==
             vr::render_graph::AttachmentLoadOp::dont_care);
    VR_CHECK(color_attachment.load_reason ==
             vr::render_graph::NativePassAttachmentLoadOpReason::first_use_no_preserve);
    VR_CHECK(color_attachment.load_inferred);
    VR_CHECK(color_attachment.requested_store_op ==
             vr::render_graph::AttachmentStoreOp::store);
    VR_CHECK(color_attachment.effective_store_op ==
             vr::render_graph::AttachmentStoreOp::dont_care);
    VR_CHECK(color_attachment.store_reason ==
             vr::render_graph::NativePassAttachmentStoreOpReason::elided_transient_last_use);
    VR_CHECK(color_attachment.store_elided);

    VR_REQUIRE(group.attachments.has_depth_attachment);
    const auto& depth_attachment = group.attachments.depth_attachment;
    VR_CHECK(depth_attachment.requested_load_op ==
             vr::render_graph::AttachmentLoadOp::load);
    VR_CHECK(depth_attachment.effective_load_op ==
             vr::render_graph::AttachmentLoadOp::dont_care);
    VR_CHECK(depth_attachment.load_reason ==
             vr::render_graph::NativePassAttachmentLoadOpReason::first_use_no_preserve);
    VR_CHECK(depth_attachment.load_inferred);
    VR_CHECK(depth_attachment.requested_store_op ==
             vr::render_graph::AttachmentStoreOp::store);
    VR_CHECK(depth_attachment.effective_store_op ==
             vr::render_graph::AttachmentStoreOp::dont_care);
    VR_CHECK(depth_attachment.store_reason ==
             vr::render_graph::NativePassAttachmentStoreOpReason::elided_transient_last_use);
    VR_CHECK(depth_attachment.store_elided);

    VR_CHECK(native_plan.summary.logical_raster_pass_count == 1U);
    VR_CHECK(native_plan.summary.native_pass_group_count == 1U);
    VR_CHECK(native_plan.summary.fused_raster_pass_count == 0U);
    VR_CHECK(native_plan.summary.load_inference_count == 2U);
    VR_CHECK(native_plan.summary.store_elision_count == 2U);
}

VR_TEST_CASE(RenderGraphLoadStoreOptimizer_preserves_authored_clear_and_external_lifetime_store,
             "unit;core;render_graph;native_pass;load_store") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_color = builder.CreateTexture(
        "persistent_scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 256U, .height = 144U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag,
        },
        vr::render_graph::ResourceLifetime::persistent);
    const auto tone_map = builder.AddPass("tone_map", true);

    (void)builder.Write(
        tone_map,
        scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    builder.SetRasterPassDesc(
        tone_map,
        vr::render_graph::RasterPassDesc{
            .color_attachments = {
                vr::render_graph::RasterColorAttachmentDesc{
                    .target = scene_color,
                    .load_op = vr::render_graph::AttachmentLoadOp::clear,
                    .store_op = vr::render_graph::AttachmentStoreOp::store,
                },
            },
        });

    const auto compiled = builder.Compile();
    const auto& attachment =
        compiled.NativePasses().groups[0].attachments.color_attachments[0];

    VR_CHECK(attachment.effective_load_op ==
             vr::render_graph::AttachmentLoadOp::clear);
    VR_CHECK(attachment.load_reason ==
             vr::render_graph::NativePassAttachmentLoadOpReason::authored_clear);
    VR_CHECK(!attachment.load_inferred);
    VR_CHECK(attachment.effective_store_op ==
             vr::render_graph::AttachmentStoreOp::store);
    VR_CHECK(attachment.store_reason ==
             vr::render_graph::NativePassAttachmentStoreOpReason::preserve_external_lifetime);
    VR_CHECK(!attachment.store_elided);
}

VR_TEST_CASE(RenderGraphLoadStoreOptimizer_exports_effective_attachment_decisions_to_json_and_debug,
             "unit;core;render_graph;native_pass;load_store") {
    auto fixture = MakeFixture();
    const auto opaque = fixture.builder.AddPass("opaque_scene");
    const auto transparent = fixture.builder.AddPass("transparent_scene");
    const auto present = fixture.builder.AddPass("present_to_swapchain", true);

    const auto color_v1 = fixture.builder.Write(
        opaque,
        fixture.color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    const auto depth_v1 = fixture.builder.Write(
        opaque,
        fixture.depth,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });
    (void)fixture.builder.Read(
        transparent,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_read,
        });
    const auto color_v2 = fixture.builder.Write(
        transparent,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Read(
        transparent,
        depth_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_read,
        });
    (void)fixture.builder.Write(
        transparent,
        depth_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });
    (void)fixture.builder.Read(
        present,
        color_v2,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
        });
    (void)fixture.builder.Write(
        present,
        fixture.present_target,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::present,
        });

    fixture.builder.SetRasterPassDesc(
        opaque,
        vr::render_graph::RasterPassDesc{
            .color_attachments = {
                vr::render_graph::RasterColorAttachmentDesc{
                    .target = fixture.color,
                    .load_op = vr::render_graph::AttachmentLoadOp::load,
                    .store_op = vr::render_graph::AttachmentStoreOp::store,
                },
            },
            .has_depth_attachment = true,
            .depth_attachment = vr::render_graph::RasterDepthAttachmentDesc{
                .target = fixture.depth,
                .load_op = vr::render_graph::AttachmentLoadOp::load,
                .store_op = vr::render_graph::AttachmentStoreOp::store,
                .stencil_load_op = vr::render_graph::AttachmentLoadOp::dont_care,
                .stencil_store_op = vr::render_graph::AttachmentStoreOp::dont_care,
                .read_only = false,
            },
        });
    fixture.builder.SetRasterPassDesc(
        transparent,
        vr::render_graph::RasterPassDesc{
            .color_attachments = {
                vr::render_graph::RasterColorAttachmentDesc{
                    .target = fixture.color,
                    .load_op = vr::render_graph::AttachmentLoadOp::load,
                    .store_op = vr::render_graph::AttachmentStoreOp::store,
                },
            },
            .has_depth_attachment = true,
            .depth_attachment = vr::render_graph::RasterDepthAttachmentDesc{
                .target = fixture.depth,
                .load_op = vr::render_graph::AttachmentLoadOp::load,
                .store_op = vr::render_graph::AttachmentStoreOp::store,
                .stencil_load_op = vr::render_graph::AttachmentLoadOp::dont_care,
                .stencil_store_op = vr::render_graph::AttachmentStoreOp::dont_care,
                .read_only = false,
            },
        });

    const auto compiled = fixture.builder.Compile();
    const std::string debug_string = compiled.BuildDebugString();
    const std::string json = compiled.BuildJson();

    VR_CHECK(compiled.NativePasses().summary.fused_raster_pass_count == 1U);
    VR_CHECK(compiled.NativePasses().summary.load_inference_count == 2U);
    VR_CHECK(compiled.NativePasses().summary.store_elision_count == 1U);
    VR_CHECK(debug_string.find("native_pass_store_elisions=1") != std::string::npos);
    VR_CHECK(debug_string.find("native_pass_load_inferences=2") != std::string::npos);
    VR_CHECK(debug_string.find("requested=load/store, effective=dont_care/store") !=
             std::string::npos);
    VR_CHECK(debug_string.find("depth_store_elided=1") != std::string::npos);
    VR_CHECK(json.find("\"nativePassPlan\"") != std::string::npos);
    VR_CHECK(json.find("\"storeElisionCount\": 1") != std::string::npos);
    VR_CHECK(json.find("\"loadInferenceCount\": 2") != std::string::npos);
    VR_CHECK(json.find("\"effectiveLoadOp\": \"dont_care\"") != std::string::npos);
    VR_CHECK(json.find("\"effectiveStoreOp\": \"dont_care\"") != std::string::npos);
    VR_CHECK(json.find("\"loadReason\": \"first_use_no_preserve\"") !=
             std::string::npos);
    VR_CHECK(json.find("\"storeReason\": \"elided_transient_last_use\"") !=
             std::string::npos);
}

} // namespace
