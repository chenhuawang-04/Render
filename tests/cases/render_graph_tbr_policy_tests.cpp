#include "support/test_framework.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <string>

namespace {

struct NativePassPolicyFixture final {
    vr::render_graph::RenderGraphBuilder builder{};
    vr::render_graph::ResourceHandle scene_color{};
    vr::render_graph::ResourceHandle scene_depth{};
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

[[nodiscard]] NativePassPolicyFixture MakeFixture() {
    NativePassPolicyFixture fixture{};
    fixture.scene_color = fixture.builder.CreateTexture(
        "scene_color",
        MakeColorTargetDesc());
    fixture.scene_depth = fixture.builder.CreateTexture(
        "scene_depth",
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

[[nodiscard]] vr::render_graph::RasterPassDesc MakeRasterPassDesc(
    const vr::render_graph::ResourceHandle color_,
    const vr::render_graph::ResourceHandle depth_,
    const vr::render_graph::AttachmentLoadOp color_load_,
    const vr::render_graph::AttachmentLoadOp depth_load_) {
    return vr::render_graph::RasterPassDesc{
        .color_attachments = {
            vr::render_graph::RasterColorAttachmentDesc{
                .target = color_,
                .load_op = color_load_,
                .store_op = vr::render_graph::AttachmentStoreOp::store,
            },
        },
        .has_depth_attachment = true,
        .depth_attachment = vr::render_graph::RasterDepthAttachmentDesc{
            .target = depth_,
            .load_op = depth_load_,
            .store_op = vr::render_graph::AttachmentStoreOp::store,
            .stencil_load_op = vr::render_graph::AttachmentLoadOp::dont_care,
            .stencil_store_op = vr::render_graph::AttachmentStoreOp::dont_care,
            .read_only = false,
        },
    };
}

[[nodiscard]] const vr::render_graph::NativePassBoundaryDecision* FindBoundary(
    const vr::render_graph::NativePassPlan& plan_,
    const std::uint32_t previous_order_,
    const std::uint32_t next_order_) {
    for (const auto& boundary_ : plan_.boundaries) {
        if (boundary_.previous_pass_order == previous_order_ &&
            boundary_.next_pass_order == next_order_) {
            return &boundary_;
        }
    }
    return nullptr;
}

[[nodiscard]] vr::render_graph::CompiledRenderGraph BuildLocalReadCandidateGraph(
    vr::render_graph::RenderGraphBuilder& builder_,
    const vr::render_graph::ResourceHandle scene_color_,
    const vr::render_graph::ResourceHandle scene_depth_,
    const vr::render_graph::ResourceHandle present_target_) {
    const auto gbuffer = builder_.AddPass("gbuffer");
    const auto composite = builder_.AddPass("composite");
    const auto present = builder_.AddPass("present_to_swapchain", true);

    const auto color_v1 = builder_.Write(
        gbuffer,
        scene_color_,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    const auto depth_v1 = builder_.Write(
        gbuffer,
        scene_depth_,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });

    (void)builder_.Read(
        composite,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
        });
    const auto color_v2 = builder_.Write(
        composite,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)builder_.Read(
        composite,
        depth_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_read,
        });
    (void)builder_.Write(
        composite,
        depth_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });

    (void)builder_.Read(
        present,
        color_v2,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
        });
    (void)builder_.Write(
        present,
        present_target_,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::present,
        });

    builder_.SetRasterPassDesc(
        gbuffer,
        MakeRasterPassDesc(scene_color_,
                           scene_depth_,
                           vr::render_graph::AttachmentLoadOp::clear,
                           vr::render_graph::AttachmentLoadOp::clear));
    builder_.SetRasterPassDesc(
        composite,
        MakeRasterPassDesc(scene_color_,
                           scene_depth_,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));
    return builder_.Compile();
}

VR_TEST_CASE(RenderGraphTbrPolicy_marks_local_read_candidate_disabled_without_explicit_opt_in,
             "unit;core;render_graph;native_pass;tbr") {
    auto fixture = MakeFixture();
    fixture.builder.SetNativePassPlannerConfig(
        vr::render_graph::NativePassPlannerConfig{
            .request_dynamic_rendering_local_read = false,
            .dynamic_rendering_local_read_supported = true,
            .dynamic_rendering_local_read_enabled = false,
        });

    const auto compiled = BuildLocalReadCandidateGraph(
        fixture.builder,
        fixture.scene_color,
        fixture.scene_depth,
        fixture.present_target);
    const auto& native_plan = compiled.NativePasses();
    const auto* boundary = FindBoundary(native_plan, 0U, 1U);

    VR_REQUIRE(boundary != nullptr);
    VR_CHECK(!boundary->fused);
    VR_CHECK(boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::sampled_group_resource_read);
    VR_CHECK(boundary->local_read.candidate);
    VR_CHECK(boundary->local_read.status ==
             vr::render_graph::NativePassLocalReadStatus::disabled);
    VR_CHECK(boundary->local_read.reason ==
             vr::render_graph::NativePassLocalReadReason::opt_in_not_requested);
    VR_CHECK(native_plan.summary.local_read_candidate_count == 1U);
    VR_CHECK(native_plan.local_read.status ==
             vr::render_graph::NativePassLocalReadStatus::disabled);
    VR_CHECK(native_plan.local_read.reason ==
             vr::render_graph::NativePassLocalReadReason::opt_in_not_requested);

    const std::string debug = compiled.BuildDebugString();
    const std::string json = compiled.BuildJson();
    VR_CHECK(debug.find("native_pass_local_read requested=0 supported=1") !=
             std::string::npos);
    VR_CHECK(debug.find("local_read_candidate=1") != std::string::npos);
    VR_CHECK(json.find("\"localReadCandidateCount\": 1") != std::string::npos);
    VR_CHECK(json.find("\"reason\": \"opt_in_not_requested\"") !=
             std::string::npos);
}

VR_TEST_CASE(RenderGraphTbrPolicy_marks_local_read_candidate_unsupported_when_capability_is_absent,
             "unit;core;render_graph;native_pass;tbr") {
    auto fixture = MakeFixture();
    fixture.builder.SetNativePassPlannerConfig(
        vr::render_graph::NativePassPlannerConfig{
            .request_dynamic_rendering_local_read = true,
            .dynamic_rendering_local_read_supported = false,
            .dynamic_rendering_local_read_enabled = false,
        });

    const auto compiled = BuildLocalReadCandidateGraph(
        fixture.builder,
        fixture.scene_color,
        fixture.scene_depth,
        fixture.present_target);
    const auto& native_plan = compiled.NativePasses();
    const auto* boundary = FindBoundary(native_plan, 0U, 1U);

    VR_REQUIRE(boundary != nullptr);
    VR_CHECK(boundary->local_read.candidate);
    VR_CHECK(boundary->local_read.status ==
             vr::render_graph::NativePassLocalReadStatus::unsupported);
    VR_CHECK(boundary->local_read.reason ==
             vr::render_graph::NativePassLocalReadReason::capability_unavailable);
    VR_CHECK(native_plan.local_read.supported == false);
    VR_CHECK(native_plan.local_read.reason ==
             vr::render_graph::NativePassLocalReadReason::capability_unavailable);
}

VR_TEST_CASE(RenderGraphTbrPolicy_keeps_candidate_disabled_until_live_executor_enablement_exists,
             "unit;core;render_graph;native_pass;tbr") {
    auto fixture = MakeFixture();
    fixture.builder.SetNativePassPlannerConfig(
        vr::render_graph::NativePassPlannerConfig{
            .request_dynamic_rendering_local_read = true,
            .dynamic_rendering_local_read_supported = true,
            .dynamic_rendering_local_read_enabled = true,
        });

    const auto compiled = BuildLocalReadCandidateGraph(
        fixture.builder,
        fixture.scene_color,
        fixture.scene_depth,
        fixture.present_target);
    const auto& native_plan = compiled.NativePasses();
    const auto* boundary = FindBoundary(native_plan, 0U, 1U);

    VR_REQUIRE(boundary != nullptr);
    VR_CHECK(boundary->local_read.candidate);
    VR_CHECK(boundary->local_read.status ==
             vr::render_graph::NativePassLocalReadStatus::disabled);
    VR_CHECK(boundary->local_read.reason ==
             vr::render_graph::NativePassLocalReadReason::not_implemented_live);
    VR_CHECK(native_plan.local_read.requested);
    VR_CHECK(native_plan.local_read.supported);
    VR_CHECK(native_plan.local_read.device_enabled);
    VR_CHECK(native_plan.local_read.reason ==
             vr::render_graph::NativePassLocalReadReason::not_implemented_live);
}

VR_TEST_CASE(RenderGraphTbrPolicy_preserves_standard_fused_groups_when_local_read_capability_is_unavailable,
             "unit;core;render_graph;native_pass;tbr") {
    auto fixture = MakeFixture();
    fixture.builder.SetNativePassPlannerConfig(
        vr::render_graph::NativePassPlannerConfig{
            .request_dynamic_rendering_local_read = true,
            .dynamic_rendering_local_read_supported = false,
            .dynamic_rendering_local_read_enabled = false,
        });

    const auto opaque = fixture.builder.AddPass("opaque_scene");
    const auto transparent = fixture.builder.AddPass("transparent_scene");
    const auto composite = fixture.builder.AddPass("composite");
    const auto present = fixture.builder.AddPass("present_to_swapchain", true);

    const auto color_v1 = fixture.builder.Write(
        opaque,
        fixture.scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    const auto depth_v1 = fixture.builder.Write(
        opaque,
        fixture.scene_depth,
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
    const auto depth_v2 = fixture.builder.Write(
        transparent,
        depth_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });

    (void)fixture.builder.Read(
        composite,
        color_v2,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
        });
    const auto color_v3 = fixture.builder.Write(
        composite,
        color_v2,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Read(
        composite,
        depth_v2,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_read,
        });
    (void)fixture.builder.Write(
        composite,
        depth_v2,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });

    (void)fixture.builder.Read(
        present,
        color_v3,
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
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::clear,
                           vr::render_graph::AttachmentLoadOp::clear));
    fixture.builder.SetRasterPassDesc(
        transparent,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));
    fixture.builder.SetRasterPassDesc(
        composite,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));

    const auto compiled = fixture.builder.Compile();
    const auto& native_plan = compiled.NativePasses();
    const auto* fused_boundary = FindBoundary(native_plan, 0U, 1U);
    const auto* local_read_boundary = FindBoundary(native_plan, 1U, 2U);

    VR_REQUIRE(fused_boundary != nullptr);
    VR_REQUIRE(local_read_boundary != nullptr);
    VR_CHECK(fused_boundary->fused);
    VR_CHECK(fused_boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::none);
    VR_CHECK(!local_read_boundary->fused);
    VR_CHECK(local_read_boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::sampled_group_resource_read);
    VR_CHECK(local_read_boundary->local_read.candidate);
    VR_CHECK(local_read_boundary->local_read.status ==
             vr::render_graph::NativePassLocalReadStatus::unsupported);
    VR_REQUIRE(native_plan.groups.size() == 2U);
    VR_CHECK(native_plan.groups[0].first_pass_order == 0U);
    VR_CHECK(native_plan.groups[0].last_pass_order == 1U);
    VR_CHECK(native_plan.groups[1].first_pass_order == 2U);
}

} // namespace
