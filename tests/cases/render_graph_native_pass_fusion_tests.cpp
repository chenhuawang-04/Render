#include "support/test_framework.hpp"
#include "vr/render_graph/native_pass_plan.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <string>

namespace {

struct RasterChainFixture final {
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

[[nodiscard]] RasterChainFixture MakeRasterChainFixture() {
    RasterChainFixture fixture{};
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
    const vr::render_graph::AttachmentLoadOp depth_load_,
    const vr::render_graph::AttachmentStoreOp color_store_ =
        vr::render_graph::AttachmentStoreOp::store,
    const vr::render_graph::AttachmentStoreOp depth_store_ =
        vr::render_graph::AttachmentStoreOp::store) {
    return vr::render_graph::RasterPassDesc{
        .color_attachments = {
            vr::render_graph::RasterColorAttachmentDesc{
                .target = color_,
                .load_op = color_load_,
                .store_op = color_store_,
            },
        },
        .has_depth_attachment = true,
        .depth_attachment = vr::render_graph::RasterDepthAttachmentDesc{
            .target = depth_,
            .load_op = depth_load_,
            .store_op = depth_store_,
            .stencil_load_op = vr::render_graph::AttachmentLoadOp::dont_care,
            .stencil_store_op = vr::render_graph::AttachmentStoreOp::dont_care,
            .read_only = false,
        },
    };
}

[[nodiscard]] const vr::render_graph::NativePassBoundaryDecision*
FindBoundary(const vr::render_graph::NativePassPlan& plan_,
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

VR_TEST_CASE(RenderGraphNativePassPlan_groups_attachment_compatible_adjacent_raster_passes,
             "unit;core;render_graph;native_pass") {
    auto fixture = MakeRasterChainFixture();

    const auto opaque_scene = fixture.builder.AddPass("opaque_scene");
    const auto transparent_scene = fixture.builder.AddPass("transparent_scene");
    const auto present = fixture.builder.AddPass("present_to_swapchain", true);

    const auto color_v1 = fixture.builder.Write(
        opaque_scene,
        fixture.scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    const auto depth_v1 = fixture.builder.Write(
        opaque_scene,
        fixture.scene_depth,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });

    (void)fixture.builder.Read(
        transparent_scene,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_read,
        });
    const auto color_v2 = fixture.builder.Write(
        transparent_scene,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Read(
        transparent_scene,
        depth_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_read,
        });
    (void)fixture.builder.Write(
        transparent_scene,
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
        opaque_scene,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::clear,
                           vr::render_graph::AttachmentLoadOp::clear));
    fixture.builder.SetRasterPassDesc(
        transparent_scene,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));

    const auto compiled = fixture.builder.Compile();
    const auto& native_plan = compiled.NativePasses();

    VR_REQUIRE(compiled.Passes().size() == 3U);
    VR_REQUIRE(native_plan.groups.size() == 1U);
    VR_CHECK(native_plan.groups[0].first_pass_order == 0U);
    VR_CHECK(native_plan.groups[0].last_pass_order == 1U);
    VR_REQUIRE(native_plan.groups[0].logical_passes.size() == 2U);
    VR_CHECK(native_plan.groups[0].logical_passes[0U].index == 0U);
    VR_CHECK(native_plan.groups[0].logical_passes[1U].index == 1U);
    VR_CHECK(native_plan.GroupIndexForPassOrder(0U) == 0U);
    VR_CHECK(native_plan.GroupIndexForPassOrder(1U) == 0U);
    VR_CHECK(native_plan.GroupIndexForPassOrder(2U) ==
             vr::render_graph::invalid_render_graph_index);

    const auto* fused_boundary = FindBoundary(native_plan, 0U, 1U);
    VR_REQUIRE(fused_boundary != nullptr);
    VR_CHECK(fused_boundary->fused);
    VR_CHECK(fused_boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::none);

    const auto* present_boundary = FindBoundary(native_plan, 1U, 2U);
    VR_REQUIRE(present_boundary != nullptr);
    VR_CHECK(!present_boundary->fused);
    VR_CHECK(present_boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::next_pass_not_raster);

    const std::string debug_string = compiled.BuildDebugString();
    const std::string legacy_debug_string =
        vr::render_graph::BuildNativePassPlanDebugString(compiled);
    const std::string json = compiled.BuildJson();
    VR_CHECK(debug_string.find("native_pass_groups=1") != std::string::npos);
    VR_CHECK(debug_string.find("transparent_scene fused=1") !=
             std::string::npos);
    VR_CHECK(debug_string.find("native_pass_attachment_group[0]") !=
             std::string::npos);
    VR_CHECK(legacy_debug_string.find("logical_raster_passes=") ==
             std::string::npos);
    VR_CHECK(legacy_debug_string.find("native_pass_groups=") ==
             std::string::npos);
    VR_CHECK(legacy_debug_string.find("native_pass_store_elisions=") ==
             std::string::npos);
    VR_CHECK(legacy_debug_string.find("native_pass_boundaries=") ==
             std::string::npos);
    VR_CHECK(legacy_debug_string.find("native_pass_local_read requested=") ==
             std::string::npos);
    VR_CHECK(legacy_debug_string.find("native_pass_attachment_group[0]") !=
             std::string::npos);
    VR_CHECK(json.find("\"nativePassPlan\"") != std::string::npos);
    VR_CHECK(json.find("\"reason\": \"none\"") != std::string::npos);
}

VR_TEST_CASE(RenderGraphNativePassPlan_blocks_shader_sample_reads_of_group_outputs,
             "unit;core;render_graph;native_pass") {
    auto fixture = MakeRasterChainFixture();
    const auto gbuffer = fixture.builder.AddPass("gbuffer");
    const auto composite = fixture.builder.AddPass("composite");
    const auto present = fixture.builder.AddPass("present_to_swapchain", true);

    const auto color_v1 = fixture.builder.Write(
        gbuffer,
        fixture.scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Write(
        gbuffer,
        fixture.scene_depth,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });

    (void)fixture.builder.Read(
        composite,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
        });
    const auto color_v2 = fixture.builder.Write(
        composite,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
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
        gbuffer,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::clear,
                           vr::render_graph::AttachmentLoadOp::clear));
    fixture.builder.SetRasterPassDesc(
        composite,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));

    const auto compiled = fixture.builder.Compile();
    const auto& native_plan = compiled.NativePasses();

    VR_REQUIRE(native_plan.groups.size() == 2U);
    const auto* boundary = FindBoundary(native_plan, 0U, 1U);
    VR_REQUIRE(boundary != nullptr);
    VR_CHECK(!boundary->fused);
    VR_CHECK(boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::sampled_group_resource_read);
    VR_CHECK(boundary->detail.find("scene_color") != std::string::npos);
}

VR_TEST_CASE(RenderGraphNativePassPlan_blocks_queue_mismatched_raster_passes,
             "unit;core;render_graph;native_pass") {
    auto fixture = MakeRasterChainFixture();
    const auto graphics_pass =
        fixture.builder.AddPass("graphics_raster", false,
                                vr::render_graph::QueueClass::graphics);
    const auto compute_pass =
        fixture.builder.AddPass("compute_like_raster", false,
                                vr::render_graph::QueueClass::compute);
    const auto present = fixture.builder.AddPass("present_to_swapchain", true);

    const auto color_v1 = fixture.builder.Write(
        graphics_pass,
        fixture.scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Write(
        graphics_pass,
        fixture.scene_depth,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });
    (void)fixture.builder.Read(
        compute_pass,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_read,
        });
    const auto color_v2 = fixture.builder.Write(
        compute_pass,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
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
        graphics_pass,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::clear,
                           vr::render_graph::AttachmentLoadOp::clear));
    fixture.builder.SetRasterPassDesc(
        compute_pass,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));

    const auto compiled = fixture.builder.Compile();
    const auto* boundary = FindBoundary(compiled.NativePasses(), 0U, 1U);
    VR_REQUIRE(boundary != nullptr);
    VR_CHECK(!boundary->fused);
    VR_CHECK(boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::queue_mismatch);
}

VR_TEST_CASE(RenderGraphNativePassPlan_blocks_attachment_target_mismatches,
             "unit;core;render_graph;native_pass") {
    auto fixture = MakeRasterChainFixture();
    const auto alternate_color = fixture.builder.CreateTexture(
        "alternate_color",
        MakeColorTargetDesc());
    const auto scene_pass = fixture.builder.AddPass("scene_pass");
    const auto post_pass = fixture.builder.AddPass("post_pass");
    const auto present = fixture.builder.AddPass("present_to_swapchain", true);

    const auto color_v1 = fixture.builder.Write(
        scene_pass,
        fixture.scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Write(
        scene_pass,
        fixture.scene_depth,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });
    const auto alternate_v1 = fixture.builder.Write(
        post_pass,
        alternate_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Read(
        present,
        alternate_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
        });
    (void)fixture.builder.Read(
        post_pass,
        color_v1,
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
        scene_pass,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::clear,
                           vr::render_graph::AttachmentLoadOp::clear));
    fixture.builder.SetRasterPassDesc(
        post_pass,
        MakeRasterPassDesc(alternate_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));

    const auto compiled = fixture.builder.Compile();
    const auto* boundary = FindBoundary(compiled.NativePasses(), 0U, 1U);
    VR_REQUIRE(boundary != nullptr);
    VR_CHECK(!boundary->fused);
    VR_CHECK(boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::color_attachment_target_mismatch);
}

VR_TEST_CASE(RenderGraphNativePassPlan_blocks_interior_load_clear_or_discard_semantics,
             "unit;core;render_graph;native_pass") {
    const auto run_case =
        [&](const vr::render_graph::AttachmentLoadOp interior_load_op_,
            const std::string_view expected_load_name_) {
            auto fixture = MakeRasterChainFixture();
            const auto opaque = fixture.builder.AddPass("opaque_scene");
            const auto transparent = fixture.builder.AddPass("transparent_scene");
            const auto present =
                fixture.builder.AddPass("present_to_swapchain", true);

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
                MakeRasterPassDesc(fixture.scene_color,
                                   fixture.scene_depth,
                                   vr::render_graph::AttachmentLoadOp::clear,
                                   vr::render_graph::AttachmentLoadOp::clear));
            fixture.builder.SetRasterPassDesc(
                transparent,
                MakeRasterPassDesc(fixture.scene_color,
                                   fixture.scene_depth,
                                   interior_load_op_,
                                   vr::render_graph::AttachmentLoadOp::load));

            const auto compiled = fixture.builder.Compile();
            const auto& native_plan = compiled.NativePasses();
            const auto* boundary = FindBoundary(native_plan, 0U, 1U);

            VR_REQUIRE(boundary != nullptr);
            VR_REQUIRE(native_plan.groups.size() == 2U);
            VR_CHECK(!boundary->fused);
            VR_CHECK(boundary->block_reason ==
                     vr::render_graph::NativePassFusionBlockReason::
                         interior_color_load_op_requires_split);
            VR_CHECK(boundary->detail.find("transparent_scene") !=
                     std::string::npos);
            VR_CHECK(boundary->detail.find(expected_load_name_) !=
                     std::string::npos);
        };

    run_case(vr::render_graph::AttachmentLoadOp::clear, "clear");
    run_case(vr::render_graph::AttachmentLoadOp::dont_care, "dont_care");
}

VR_TEST_CASE(RenderGraphNativePassPlan_blocks_fusion_when_middle_pass_store_would_be_lost,
             "unit;core;render_graph;native_pass") {
    auto fixture = MakeRasterChainFixture();
    const auto opaque = fixture.builder.AddPass("opaque_scene");
    const auto transparent = fixture.builder.AddPass("transparent_scene");
    const auto lighting = fixture.builder.AddPass("lighting_scene");
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
        lighting,
        color_v2,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_read,
        });
    const auto color_v3 = fixture.builder.Write(
        lighting,
        color_v2,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Read(
        lighting,
        depth_v2,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_read,
        });
    (void)fixture.builder.Write(
        lighting,
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
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentStoreOp::dont_care));
    fixture.builder.SetRasterPassDesc(
        lighting,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));

    const auto compiled = fixture.builder.Compile();
    const auto& native_plan = compiled.NativePasses();
    const auto* first_boundary = FindBoundary(native_plan, 0U, 1U);
    const auto* second_boundary = FindBoundary(native_plan, 1U, 2U);

    VR_REQUIRE(first_boundary != nullptr);
    VR_REQUIRE(second_boundary != nullptr);
    VR_REQUIRE(native_plan.groups.size() == 2U);
    VR_CHECK(first_boundary->fused);
    VR_CHECK(first_boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::none);
    VR_CHECK(!second_boundary->fused);
    VR_CHECK(second_boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::
                 interior_color_store_op_requires_split);
    VR_CHECK(second_boundary->detail.find("transparent_scene") !=
             std::string::npos);
    VR_CHECK(second_boundary->detail.find("dont_care") !=
             std::string::npos);
    VR_CHECK(native_plan.groups[0].first_pass_order == 0U);
    VR_CHECK(native_plan.groups[0].last_pass_order == 1U);
    VR_CHECK(native_plan.groups[1].first_pass_order == 2U);
    VR_CHECK(native_plan.groups[1].last_pass_order == 2U);
}

VR_TEST_CASE(RenderGraphNativePassPlan_blocks_host_boundary_raster_passes,
             "unit;core;render_graph;native_pass") {
    auto fixture = MakeRasterChainFixture();
    const auto host_buffer = fixture.builder.CreateBuffer(
        "readback_buffer",
        vr::render_graph::BufferDesc{
            .size_bytes = 512U,
            .usage = vr::render_graph::buffer_usage_transfer_dst_flag,
        },
        vr::render_graph::ResourceLifetime::persistent);
    const auto shadow = fixture.builder.AddPass("shadow");
    const auto readback = fixture.builder.AddPass("readback_raster");
    const auto present = fixture.builder.AddPass("present_to_swapchain", true);

    const auto color_v1 = fixture.builder.Write(
        shadow,
        fixture.scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Write(
        shadow,
        fixture.scene_depth,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });
    (void)fixture.builder.Read(
        readback,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_read,
        });
    const auto color_v2 = fixture.builder.Write(
        readback,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Read(
        readback,
        host_buffer,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::host_read,
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
        shadow,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::clear,
                           vr::render_graph::AttachmentLoadOp::clear));
    fixture.builder.SetRasterPassDesc(
        readback,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));

    const auto compiled = fixture.builder.Compile();
    const auto* boundary = FindBoundary(compiled.NativePasses(), 0U, 1U);
    VR_REQUIRE(boundary != nullptr);
    VR_CHECK(!boundary->fused);
    VR_CHECK(boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::host_boundary);
}

VR_TEST_CASE(RenderGraphNativePassPlan_blocks_side_effect_raster_passes,
             "unit;core;render_graph;native_pass") {
    auto fixture = MakeRasterChainFixture();
    const auto prepass = fixture.builder.AddPass("prepass");
    const auto present_like = fixture.builder.AddPass("present_like_raster", true);

    const auto color_v1 = fixture.builder.Write(
        prepass,
        fixture.scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Write(
        prepass,
        fixture.scene_depth,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });
    (void)fixture.builder.Read(
        present_like,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_read,
        });
    (void)fixture.builder.Write(
        present_like,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });

    fixture.builder.SetRasterPassDesc(
        prepass,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::clear,
                           vr::render_graph::AttachmentLoadOp::clear));
    fixture.builder.SetRasterPassDesc(
        present_like,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));

    const auto compiled = fixture.builder.Compile();
    const auto* boundary = FindBoundary(compiled.NativePasses(), 0U, 1U);
    VR_REQUIRE(boundary != nullptr);
    VR_CHECK(!boundary->fused);
    VR_CHECK(boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::side_effect);
}

VR_TEST_CASE(RenderGraphNativePassPlan_blocks_force_split_hints,
             "unit;core;render_graph;native_pass") {
    auto fixture = MakeRasterChainFixture();
    const auto pass_a = fixture.builder.AddPass("pass_a");
    const auto pass_b = fixture.builder.AddPass("pass_b");
    const auto present = fixture.builder.AddPass("present_to_swapchain", true);

    const auto color_v1 = fixture.builder.Write(
        pass_a,
        fixture.scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)fixture.builder.Write(
        pass_a,
        fixture.scene_depth,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });
    (void)fixture.builder.Read(
        pass_b,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_read,
        });
    const auto color_v2 = fixture.builder.Write(
        pass_b,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
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
        pass_a,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::clear,
                           vr::render_graph::AttachmentLoadOp::clear));
    fixture.builder.SetRasterPassDesc(
        pass_b,
        MakeRasterPassDesc(fixture.scene_color,
                           fixture.scene_depth,
                           vr::render_graph::AttachmentLoadOp::load,
                           vr::render_graph::AttachmentLoadOp::load));
    fixture.builder.SetPassCompileHints(
        pass_b,
        vr::render_graph::PassCompileHints{
            .force_native_pass_split = true,
        });

    const auto compiled = fixture.builder.Compile();
    const auto* boundary = FindBoundary(compiled.NativePasses(), 0U, 1U);
    VR_REQUIRE(boundary != nullptr);
    VR_CHECK(!boundary->fused);
    VR_CHECK(boundary->block_reason ==
             vr::render_graph::NativePassFusionBlockReason::force_split_hint);
}

} // namespace
