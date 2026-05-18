#include "support/test_framework.hpp"
#include "vr/render_graph/alias_allocator.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <string_view>

namespace {

[[nodiscard]] const vr::render_graph::TransientAllocationRecord* FindAllocationRecord(
    const vr::render_graph::TransientAllocationPlan& plan_,
    const std::string_view debug_name_) noexcept {
    for (const auto& record_ : plan_.records) {
        if (record_.debug_name == debug_name_) {
            return &record_;
        }
    }
    return nullptr;
}

struct BufferFootprintOverride final {
    bool host_visible = false;
    bool persistently_mapped = false;
};

[[nodiscard]] bool ResolveBufferFootprintOverride(
    const vr::render_graph::CompiledResource& resource_,
    vr::render_graph::ResourceFootprint& footprint_,
    const void* user_data_,
    std::string& error_message_) {
    if (resource_.kind != vr::render_graph::ResourceKind::buffer) {
        error_message_ = "buffer-only footprint override received a non-buffer resource";
        return false;
    }

    const auto* override_ = static_cast<const BufferFootprintOverride*>(user_data_);
    footprint_ = {};
    footprint_.size_bytes = resource_.buffer.size_bytes;
    footprint_.alignment_bytes = 1U;
    footprint_.memory_type_bits = 0x1U;
    footprint_.usage_flags = resource_.buffer.usage;
    footprint_.host_visible = override_ != nullptr && override_->host_visible;
    footprint_.persistently_mapped = override_ != nullptr && override_->persistently_mapped;
    return true;
}

VR_TEST_CASE(RenderGraphAliasAllocator_empty_graph_returns_empty_plan,
             "unit;core;render_graph;alias_allocator") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto compiled = builder.Compile();
    const auto& plan = compiled.TransientAllocations();

    VR_CHECK(plan.Empty());
    VR_CHECK(plan.records.empty());
    VR_CHECK(plan.pages.empty());
    VR_CHECK(plan.alias_candidates.empty());
    VR_CHECK(plan.alias_barriers.empty());
}

VR_TEST_CASE(RenderGraphAliasAllocator_imported_and_persistent_are_ignored,
             "unit;core;render_graph;alias_allocator") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto imported_present = builder.CreateTexture(
        "present_target",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::unknown,
            .extent = {.width = 64U, .height = 64U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_present_flag,
        },
        vr::render_graph::ResourceLifetime::imported);
    const auto persistent_constants = builder.CreateBuffer(
        "persistent_constants",
        vr::render_graph::BufferDesc{
            .size_bytes = 1024U,
            .usage = vr::render_graph::buffer_usage_uniform_flag,
        },
        vr::render_graph::ResourceLifetime::persistent);
    const auto transient_scratch = builder.CreateBuffer(
        "transient_scratch",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });

    const auto upload = builder.AddPass("upload_constants");
    const auto shade = builder.AddPass("shade_scene");
    const auto present = builder.AddPass("present", true);

    const auto constants_written = builder.Write(
        upload,
        persistent_constants,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::transfer_write});
    const auto scratch_written = builder.Write(
        shade,
        transient_scratch,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        shade,
        constants_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::uniform_read});
    (void)builder.Read(
        present,
        scratch_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});
    (void)builder.Write(
        present,
        imported_present,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::present});

    const auto compiled = builder.Compile();
    const auto& plan = compiled.TransientAllocations();

    const auto* imported_record = FindAllocationRecord(plan, "present_target");
    const auto* persistent_record = FindAllocationRecord(plan, "persistent_constants");
    const auto* transient_record = FindAllocationRecord(plan, "transient_scratch");
    VR_REQUIRE(imported_record != nullptr);
    VR_REQUIRE(persistent_record != nullptr);
    VR_REQUIRE(transient_record != nullptr);

    VR_CHECK(!imported_record->eligible);
    VR_CHECK(!persistent_record->eligible);
    VR_CHECK(transient_record->eligible);
    VR_CHECK(plan.pages.size() == 1U);
    VR_CHECK(plan.timeline.transient_resource_count == 1U);
    VR_CHECK(plan.timeline.eligible_resource_count == 1U);
}

VR_TEST_CASE(RenderGraphAliasAllocator_device_local_buffers_alias_by_default,
             "unit;core;render_graph;alias_allocator") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateBuffer(
        "temp_a",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto temp_b = builder.CreateBuffer(
        "temp_b",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c");
    const auto pass_d = builder.AddPass("pass_d", true);

    const auto temp_a_written = builder.Write(
        pass_a,
        temp_a,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_b,
        temp_a_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});
    const auto temp_b_written = builder.Write(
        pass_c,
        temp_b,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_d,
        temp_b_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});

    const auto compiled = builder.Compile();
    const auto& plan = compiled.TransientAllocations();
    const auto* record_a = FindAllocationRecord(plan, "temp_a");
    const auto* record_b = FindAllocationRecord(plan, "temp_b");

    VR_REQUIRE(record_a != nullptr);
    VR_REQUIRE(record_b != nullptr);
    VR_CHECK(record_a->eligible);
    VR_CHECK(record_b->eligible);
    VR_REQUIRE(plan.pages.size() == 1U);
    VR_CHECK(record_a->page_index == plan.pages[0].page_index);
    VR_CHECK(record_b->page_index == plan.pages[0].page_index);
    VR_CHECK(plan.timeline.saved_bytes == 4096U);
    VR_CHECK(plan.timeline.aliased_resource_count == 2U);
    VR_CHECK(plan.alias_barriers.size() == 1U);
    VR_CHECK(plan.alias_barriers[0].required);
}

VR_TEST_CASE(RenderGraphAliasAllocator_host_visible_buffers_do_not_alias,
             "unit;core;render_graph;alias_allocator") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateBuffer(
        "temp_a",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
            .host_visible = true,
        });
    const auto temp_b = builder.CreateBuffer(
        "temp_b",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
            .host_visible = true,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c");
    const auto pass_d = builder.AddPass("pass_d", true);

    const auto temp_a_written = builder.Write(
        pass_a,
        temp_a,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_b,
        temp_a_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});
    const auto temp_b_written = builder.Write(
        pass_c,
        temp_b,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_d,
        temp_b_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});

    const auto compiled = builder.Compile();
    const auto& plan = compiled.TransientAllocations();

    VR_CHECK(plan.pages.empty());
    VR_CHECK(plan.timeline.eligible_resource_count == 0U);
    VR_REQUIRE(plan.alias_candidates.size() == 1U);
    VR_CHECK(!plan.alias_candidates[0].aliasable);
    VR_CHECK(plan.alias_candidates[0].non_alias_reason.find("host-visible") != std::string::npos);
}

VR_TEST_CASE(RenderGraphAliasAllocator_provider_host_visible_buffers_do_not_alias,
             "unit;core;render_graph;alias_allocator") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateBuffer(
        "temp_a",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto temp_b = builder.CreateBuffer(
        "temp_b",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c");
    const auto pass_d = builder.AddPass("pass_d", true);

    const auto temp_a_written = builder.Write(
        pass_a,
        temp_a,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_b,
        temp_a_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});
    const auto temp_b_written = builder.Write(
        pass_c,
        temp_b,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_d,
        temp_b_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});

    const auto compiled = builder.Compile();
    const BufferFootprintOverride override_{
        .host_visible = true,
    };
    const auto plan = vr::render_graph::BuildTransientAllocationPlan(
        compiled,
        vr::render_graph::TransientFootprintProvider{
            .user_data = &override_,
            .resolve_fn = ResolveBufferFootprintOverride,
        });

    VR_CHECK(plan.pages.empty());
    VR_CHECK(plan.timeline.eligible_resource_count == 0U);
    VR_REQUIRE(plan.alias_candidates.size() == 1U);
    VR_CHECK(!plan.alias_candidates[0].aliasable);
    VR_CHECK(plan.alias_candidates[0].non_alias_reason.find("host-visible") != std::string::npos);
}

VR_TEST_CASE(RenderGraphAliasAllocator_provider_persistently_mapped_buffers_do_not_alias,
             "unit;core;render_graph;alias_allocator") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateBuffer(
        "temp_a",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto temp_b = builder.CreateBuffer(
        "temp_b",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c");
    const auto pass_d = builder.AddPass("pass_d", true);

    const auto temp_a_written = builder.Write(
        pass_a,
        temp_a,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_b,
        temp_a_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});
    const auto temp_b_written = builder.Write(
        pass_c,
        temp_b,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_d,
        temp_b_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});

    const auto compiled = builder.Compile();
    const BufferFootprintOverride override_{
        .persistently_mapped = true,
    };
    const auto plan = vr::render_graph::BuildTransientAllocationPlan(
        compiled,
        vr::render_graph::TransientFootprintProvider{
            .user_data = &override_,
            .resolve_fn = ResolveBufferFootprintOverride,
        });

    VR_CHECK(plan.pages.empty());
    VR_CHECK(plan.timeline.eligible_resource_count == 0U);
    VR_REQUIRE(plan.alias_candidates.size() == 1U);
    VR_CHECK(!plan.alias_candidates[0].aliasable);
    VR_CHECK(plan.alias_candidates[0].non_alias_reason.find("persistently mapped") != std::string::npos);
}

VR_TEST_CASE(RenderGraphAliasAllocator_textures_require_opt_in,
             "unit;core;render_graph;alias_allocator") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateTexture(
        "temp_a",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
            .extent = {.width = 64U, .height = 64U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        });
    const auto temp_b = builder.CreateTexture(
        "temp_b",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
            .extent = {.width = 64U, .height = 64U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c");
    const auto pass_d = builder.AddPass("pass_d", true);

    const auto temp_a_written = builder.Write(
        pass_a,
        temp_a,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::color_attachment_write});
    (void)builder.Read(
        pass_b,
        temp_a_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_sample_read});
    const auto temp_b_written = builder.Write(
        pass_c,
        temp_b,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::color_attachment_write});
    (void)builder.Read(
        pass_d,
        temp_b_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_sample_read});

    const auto compiled = builder.Compile();
    const auto& plan = compiled.TransientAllocations();

    VR_CHECK(plan.pages.empty());
    VR_CHECK(plan.timeline.eligible_resource_count == 0U);
    VR_REQUIRE(plan.alias_candidates.size() == 1U);
    VR_CHECK(!plan.alias_candidates[0].aliasable);
    VR_CHECK(plan.alias_candidates[0].non_alias_reason.find("explicit opt-in") != std::string::npos);
}

VR_TEST_CASE(RenderGraphAliasAllocator_non_overlapping_buffers_share_page,
             "unit;core;render_graph;alias_allocator") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateBuffer(
        "temp_a",
        vr::render_graph::BufferDesc{
            .size_bytes = 8192U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto temp_b = builder.CreateBuffer(
        "temp_b",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c");
    const auto pass_d = builder.AddPass("pass_d", true);

    const auto temp_a_written = builder.Write(
        pass_a,
        temp_a,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_b,
        temp_a_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});
    const auto temp_b_written = builder.Write(
        pass_c,
        temp_b,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_d,
        temp_b_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});

    const auto compiled = builder.Compile();
    const auto& plan = compiled.TransientAllocations();

    VR_REQUIRE(plan.pages.size() == 1U);
    VR_CHECK(plan.pages[0].size_bytes == 8192U);
    VR_CHECK(plan.timeline.logical_total_bytes == 12288U);
    VR_CHECK(plan.timeline.physical_total_bytes == 8192U);
    VR_CHECK(plan.timeline.saved_bytes == 4096U);
}

VR_TEST_CASE(RenderGraphAliasAllocator_overlapping_buffers_use_distinct_pages,
             "unit;core;render_graph;alias_allocator") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateBuffer(
        "temp_a",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto temp_b = builder.CreateBuffer(
        "temp_b",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b");
    const auto pass_c = builder.AddPass("pass_c", true);
    const auto pass_d = builder.AddPass("pass_d", true);

    const auto temp_a_written = builder.Write(
        pass_a,
        temp_a,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_c,
        temp_a_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});
    const auto temp_b_written = builder.Write(
        pass_b,
        temp_b,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)builder.Read(
        pass_d,
        temp_b_written,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});

    const auto compiled = builder.Compile();
    const auto& plan = compiled.TransientAllocations();

    VR_REQUIRE(plan.pages.size() == 2U);
    VR_CHECK(plan.timeline.saved_bytes == 0U);
    VR_CHECK(plan.alias_barriers.empty());
}

VR_TEST_CASE(RenderGraphAliasAllocator_plan_is_deterministic,
             "unit;core;render_graph;alias_allocator") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateBuffer(
        "temp_a",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto temp_b = builder.CreateBuffer(
        "temp_b",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto temp_c = builder.CreateBuffer(
        "temp_c",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c");
    const auto pass_d = builder.AddPass("pass_d", true);
    const auto pass_e = builder.AddPass("pass_e");
    const auto pass_f = builder.AddPass("pass_f", true);

    const auto temp_a_written = builder.Write(pass_a, temp_a);
    (void)builder.Read(pass_b, temp_a_written);
    const auto temp_b_written = builder.Write(pass_c, temp_b);
    (void)builder.Read(pass_d, temp_b_written);
    const auto temp_c_written = builder.Write(pass_e, temp_c);
    (void)builder.Read(pass_f, temp_c_written);

    const auto first = builder.Compile().TransientAllocations();
    const auto second = builder.Compile().TransientAllocations();

    VR_CHECK(first.pages.size() == second.pages.size());
    VR_CHECK(first.alias_barriers.size() == second.alias_barriers.size());
    VR_CHECK(first.timeline.saved_bytes == second.timeline.saved_bytes);
    VR_REQUIRE(first.records.size() == second.records.size());
    for (std::size_t index = 0; index < first.records.size(); ++index) {
        VR_CHECK(first.records[index].resource.index == second.records[index].resource.index);
        VR_CHECK(first.records[index].page_index == second.records[index].page_index);
        VR_CHECK(first.records[index].aliased == second.records[index].aliased);
    }
}

VR_TEST_CASE(RenderGraphAliasAllocator_compiled_graph_json_exports_transient_plan,
             "unit;core;render_graph;alias_allocator;debug") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateBuffer(
        "temp_a",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto temp_b = builder.CreateBuffer(
        "temp_b",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c");
    const auto pass_d = builder.AddPass("pass_d", true);
    const auto temp_a_written = builder.Write(pass_a, temp_a);
    (void)builder.Read(pass_b, temp_a_written);
    const auto temp_b_written = builder.Write(pass_c, temp_b);
    (void)builder.Read(pass_d, temp_b_written);

    const auto compiled = builder.Compile();
    const auto json = compiled.BuildJson();

    VR_CHECK(json.find("\"transientAllocationPlan\"") != std::string::npos);
    VR_CHECK(json.find("\"savedBytes\": 4096") != std::string::npos);
    VR_CHECK(json.find("\"pageCount\": 1") != std::string::npos);
}

} // namespace
