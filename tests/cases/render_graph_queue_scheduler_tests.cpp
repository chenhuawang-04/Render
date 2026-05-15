#include "support/test_framework.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

namespace {

VR_TEST_CASE(RenderGraphQueueScheduler_groups_contiguous_passes_by_queue,
             "unit;core;render_graph;queue") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto upload_buffer = builder.CreateBuffer(
        "upload_buffer",
        vr::render_graph::BufferDesc{
            .size_bytes = 1024U,
            .usage = vr::render_graph::buffer_usage_transfer_dst_flag |
                     vr::render_graph::buffer_usage_storage_flag,
        });
    const auto work_buffer = builder.CreateBuffer(
        "work_buffer",
        vr::render_graph::BufferDesc{
            .size_bytes = 1024U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto present_target = builder.CreateTexture(
        "present_target",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::unknown,
            .extent = {.width = 64U, .height = 64U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_present_flag,
        },
        vr::render_graph::ResourceLifetime::imported);

    const auto upload = builder.AddPass("upload", false, vr::render_graph::QueueClass::transfer);
    const auto compute = builder.AddPass("compute", false, vr::render_graph::QueueClass::compute);
    const auto present = builder.AddPass("present", true, vr::render_graph::QueueClass::graphics);

    const auto uploaded = builder.Write(
        upload,
        upload_buffer,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::transfer_write});
    const auto computed = builder.Write(
        compute,
        work_buffer,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        compute,
        uploaded,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});
    (void)builder.Read(
        present,
        computed,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});
    (void)builder.Write(
        present,
        present_target,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::present});

    const auto compiled = builder.Compile();
    const auto& queue_batches = compiled.PlannedBarriers().queue_batches;

    const auto& queue_dependencies = compiled.PlannedBarriers().queue_dependencies;

    VR_REQUIRE(queue_batches.size() == 3U);
    VR_CHECK(queue_batches[0].queue == vr::render_graph::QueueClass::transfer);
    VR_CHECK(queue_batches[0].passes.size() == 1U);
    VR_CHECK(queue_batches[1].queue == vr::render_graph::QueueClass::compute);
    VR_CHECK(queue_batches[1].passes.size() == 1U);
    VR_CHECK(queue_batches[2].queue == vr::render_graph::QueueClass::graphics);
    VR_CHECK(queue_batches[2].passes.size() == 1U);
    VR_REQUIRE(queue_dependencies.size() == 2U);
    VR_CHECK(queue_dependencies[0].source_queue == vr::render_graph::QueueClass::transfer);
    VR_CHECK(queue_dependencies[0].target_queue == vr::render_graph::QueueClass::compute);
    VR_CHECK(queue_dependencies[0].source_batch_index == 0U);
    VR_CHECK(queue_dependencies[0].target_batch_index == 1U);
    VR_CHECK(queue_dependencies[0].queue_transfer);
    VR_CHECK(queue_dependencies[1].source_queue == vr::render_graph::QueueClass::compute);
    VR_CHECK(queue_dependencies[1].target_queue == vr::render_graph::QueueClass::graphics);
    VR_CHECK(queue_dependencies[1].source_batch_index == 1U);
    VR_CHECK(queue_dependencies[1].target_batch_index == 2U);
    VR_CHECK(queue_dependencies[1].queue_transfer);
    VR_REQUIRE(queue_batches[0].signal_dependency_indices.size() == 1U);
    VR_CHECK(queue_batches[0].signal_dependency_indices[0] == 0U);
    VR_REQUIRE(queue_batches[1].wait_dependency_indices.size() == 1U);
    VR_CHECK(queue_batches[1].wait_dependency_indices[0] == 0U);
    VR_REQUIRE(queue_batches[1].signal_dependency_indices.size() == 1U);
    VR_CHECK(queue_batches[1].signal_dependency_indices[0] == 1U);
    VR_REQUIRE(queue_batches[2].wait_dependency_indices.size() == 1U);
    VR_CHECK(queue_batches[2].wait_dependency_indices[0] == 1U);
}

VR_TEST_CASE(RenderGraphQueueScheduler_keeps_same_queue_passes_in_one_submit_batch,
             "unit;core;render_graph;queue") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto history = builder.CreateTexture(
        "history",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
            .extent = {.width = 64U, .height = 64U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        });
    const auto present_target = builder.CreateTexture(
        "present_target",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::unknown,
            .extent = {.width = 64U, .height = 64U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_present_flag,
        },
        vr::render_graph::ResourceLifetime::imported);

    const auto scene = builder.AddPass("scene", false, vr::render_graph::QueueClass::graphics);
    const auto overlay = builder.AddPass("overlay", false, vr::render_graph::QueueClass::graphics);
    const auto present = builder.AddPass("present", true, vr::render_graph::QueueClass::graphics);

    const auto scene_v1 = builder.Write(
        scene,
        history,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::color_attachment_write});
    (void)builder.Read(
        overlay,
        scene_v1,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_sample_read});
    const auto scene_v2 = builder.Write(
        overlay,
        history,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::color_attachment_write});
    (void)builder.Read(
        present,
        scene_v2,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_sample_read});
    (void)builder.Write(
        present,
        present_target,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::present});

    const auto compiled = builder.Compile();
    const auto& queue_batches = compiled.PlannedBarriers().queue_batches;

    VR_REQUIRE(queue_batches.size() == 1U);
    VR_CHECK(queue_batches[0].queue == vr::render_graph::QueueClass::graphics);
    VR_CHECK(queue_batches[0].passes.size() == 3U);
    VR_CHECK(compiled.PlannedBarriers().queue_dependencies.empty());
}

VR_TEST_CASE(RenderGraphQueueScheduler_splits_host_access_into_separate_submit_boundaries,
             "unit;core;render_graph;queue") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto staging = builder.CreateBuffer(
        "staging",
        vr::render_graph::BufferDesc{
            .size_bytes = 1024U,
            .usage = vr::render_graph::buffer_usage_storage_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
            .host_visible = true,
        });

    const auto gpu_write = builder.AddPass("gpu_write", false, vr::render_graph::QueueClass::graphics);
    const auto cpu_read = builder.AddPass("cpu_read", true, vr::render_graph::QueueClass::graphics);
    const auto gpu_consume = builder.AddPass("gpu_consume", true, vr::render_graph::QueueClass::graphics);

    const auto staged = builder.Write(
        gpu_write,
        staging,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_write,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    (void)builder.Read(
        cpu_read,
        staged,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::host_read,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    (void)builder.Read(
        gpu_consume,
        staged,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_read,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });

    const auto compiled = builder.Compile();
    const auto& queue_batches = compiled.PlannedBarriers().queue_batches;
    const auto& queue_dependencies = compiled.PlannedBarriers().queue_dependencies;

    VR_REQUIRE(queue_batches.size() == 3U);
    VR_CHECK(queue_batches[1].contains_host_boundary);
    VR_REQUIRE(queue_dependencies.size() == 2U);
    VR_CHECK(queue_dependencies[0].host_boundary);
    VR_CHECK(queue_dependencies[1].host_boundary);
    VR_CHECK(!queue_dependencies[0].queue_transfer);
    VR_CHECK(!queue_dependencies[1].queue_transfer);
    VR_CHECK(queue_dependencies[0].source_batch_index == 0U);
    VR_CHECK(queue_dependencies[0].target_batch_index == 1U);
    VR_CHECK(queue_dependencies[1].source_batch_index == 1U);
    VR_CHECK(queue_dependencies[1].target_batch_index == 2U);
}

} // namespace
