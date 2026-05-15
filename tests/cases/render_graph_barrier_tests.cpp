#include "support/test_framework.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

namespace {

VR_TEST_CASE(RenderGraphBarrierPlanner_emits_image_barriers_for_scene_overlay_present_chain,
             "unit;core;render_graph;barrier") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 320U, .height = 180U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        });
    const auto present_target = builder.CreateTexture(
        "present_target",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::unknown,
            .extent = {.width = 320U, .height = 180U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_present_flag,
        },
        vr::render_graph::ResourceLifetime::imported);

    const auto scene = builder.AddPass("main_scene_pass");
    const auto overlay = builder.AddPass("overlay_pass");
    const auto present = builder.AddPass("present_to_swapchain", true);

    const auto scene_color_v1 = builder.Write(
        scene,
        scene_color,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::color_attachment_write});
    (void)builder.Read(
        overlay,
        scene_color_v1,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_sample_read});
    const auto scene_color_v2 = builder.Write(
        overlay,
        scene_color,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::color_attachment_write});
    (void)builder.Read(
        present,
        scene_color_v2,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_sample_read});
    (void)builder.Write(
        present,
        present_target,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::present});

    const auto compiled = builder.Compile();
    const auto& plan = compiled.PlannedBarriers();

    VR_REQUIRE(plan.barrier_batches.size() == 2U);
    VR_CHECK(plan.barrier_batches[0].pass.index == overlay.index);
    VR_REQUIRE(plan.barrier_batches[0].barriers.size() == 2U);
    VR_CHECK(plan.barrier_batches[0].barriers[0].before == vr::render_graph::AccessKind::color_attachment_write);
    VR_CHECK(plan.barrier_batches[0].barriers[0].after == vr::render_graph::AccessKind::shader_sample_read);
    VR_CHECK(plan.barrier_batches[0].barriers[1].before == vr::render_graph::AccessKind::shader_sample_read);
    VR_CHECK(plan.barrier_batches[0].barriers[1].after == vr::render_graph::AccessKind::color_attachment_write);
    VR_CHECK(plan.barrier_batches[1].pass.index == present.index);
    VR_REQUIRE(plan.barrier_batches[1].barriers.size() == 1U);
    VR_CHECK(plan.barrier_batches[1].barriers[0].before == vr::render_graph::AccessKind::color_attachment_write);
    VR_CHECK(plan.barrier_batches[1].barriers[0].after == vr::render_graph::AccessKind::shader_sample_read);
}

VR_TEST_CASE(RenderGraphBarrierPlanner_emits_queue_transfer_for_transfer_to_graphics_buffer_read,
             "unit;core;render_graph;barrier") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_constants = builder.CreateBuffer(
        "scene_constants",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_uniform_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });

    const auto upload = builder.AddPass("upload_constants", false, vr::render_graph::QueueClass::transfer);
    const auto shade = builder.AddPass("shade_scene", true, vr::render_graph::QueueClass::graphics);

    const auto uploaded = builder.Write(
        upload,
        scene_constants,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::transfer_write,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 512U},
        });
    (void)builder.Read(
        shade,
        uploaded,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::uniform_read,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 512U},
        });

    const auto compiled = builder.Compile();
    const auto& plan = compiled.PlannedBarriers();

    VR_REQUIRE(plan.barrier_batches.size() == 1U);
    VR_REQUIRE(plan.barrier_batches[0].barriers.size() == 1U);
    const auto& barrier = plan.barrier_batches[0].barriers[0];

    VR_REQUIRE(plan.queue_batches.size() == 2U);
    VR_CHECK(plan.queue_batches[0].queue == vr::render_graph::QueueClass::transfer);
    VR_CHECK(plan.queue_batches[1].queue == vr::render_graph::QueueClass::graphics);
    VR_CHECK(barrier.before == vr::render_graph::AccessKind::transfer_write);
    VR_CHECK(barrier.after == vr::render_graph::AccessKind::uniform_read);
    VR_CHECK(barrier.queue_transfer);
    VR_CHECK(barrier.src_queue == vr::render_graph::QueueClass::transfer);
    VR_CHECK(barrier.dst_queue == vr::render_graph::QueueClass::graphics);
    VR_CHECK(barrier.buffer_range.size_bytes == 512U);
}

VR_TEST_CASE(RenderGraphBarrierPlanner_marks_uav_ordering_for_storage_hazards,
             "unit;core;render_graph;barrier") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto particle_buffer = builder.CreateBuffer(
        "particle_buffer",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });

    const auto update = builder.AddPass("update_particles", false, vr::render_graph::QueueClass::compute);
    const auto draw = builder.AddPass("draw_particles", true, vr::render_graph::QueueClass::compute);

    const auto updated = builder.Write(
        update,
        particle_buffer,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_write,
            .buffer_range = {.offset_bytes = 256U, .size_bytes = 1024U},
        });
    (void)builder.Read(
        draw,
        updated,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_read,
            .buffer_range = {.offset_bytes = 256U, .size_bytes = 1024U},
        });

    const auto compiled = builder.Compile();
    VR_REQUIRE(compiled.PlannedBarriers().barrier_batches.size() == 1U);
    VR_REQUIRE(compiled.PlannedBarriers().barrier_batches[0].barriers.size() == 1U);
    const auto& barrier = compiled.PlannedBarriers().barrier_batches[0].barriers[0];

    VR_CHECK(barrier.before == vr::render_graph::AccessKind::shader_storage_write);
    VR_CHECK(barrier.after == vr::render_graph::AccessKind::shader_storage_read);
    VR_CHECK(barrier.uav_ordering);
    VR_CHECK(!barrier.queue_transfer);
}

VR_TEST_CASE(RenderGraphBarrierPlanner_skips_barriers_for_non_overlapping_subresources,
             "unit;core;render_graph;barrier") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
            .extent = {.width = 128U, .height = 128U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
            .mip_level_count = 2U,
        });

    const auto write_pass = builder.AddPass("write_mip0");
    const auto read_pass = builder.AddPass("read_mip1", true);

    const auto written = builder.Write(
        write_pass,
        scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
            .subresource_range = {.base_mip_level = 0U, .level_count = 1U, .base_array_layer = 0U, .layer_count = 1U},
        });
    (void)builder.Read(
        read_pass,
        written,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
            .subresource_range = {.base_mip_level = 1U, .level_count = 1U, .base_array_layer = 0U, .layer_count = 1U},
        });

    const auto compiled = builder.Compile();
    VR_CHECK(compiled.PlannedBarriers().barrier_batches.empty());
}

VR_TEST_CASE(RenderGraphBarrierPlanner_reports_alias_candidates_for_non_overlapping_transients,
             "unit;core;render_graph;barrier") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateTexture(
        "temp_a",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
            .extent = {.width = 128U, .height = 128U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag,
        });
    const auto temp_b = builder.CreateTexture(
        "temp_b",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
            .extent = {.width = 128U, .height = 128U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c");
    const auto pass_d = builder.AddPass("pass_d", true);

    (void)builder.Write(
        pass_a,
        temp_a,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::color_attachment_write});
    (void)builder.Read(
        pass_b,
        temp_a,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_sample_read});
    (void)builder.Write(
        pass_c,
        temp_b,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::color_attachment_write});
    (void)builder.Read(
        pass_d,
        temp_b,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_sample_read});

    const auto compiled = builder.Compile();
    const auto& alias_candidates = compiled.PlannedBarriers().alias_candidates;
    const auto& alias_barriers = compiled.PlannedBarriers().alias_barriers;

    VR_REQUIRE(alias_candidates.size() == 1U);
    VR_CHECK(alias_candidates[0].same_compatibility_class);
    VR_CHECK(!alias_candidates[0].overlapping_liveness);
    VR_CHECK(alias_candidates[0].aliasable);
    VR_REQUIRE(alias_barriers.size() == 1U);
    VR_CHECK(alias_barriers[0].required);
    VR_CHECK(!alias_barriers[0].realized);
}

} // namespace
