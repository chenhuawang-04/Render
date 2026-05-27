#include "support/test_framework.hpp"
#include "vr/geometry/geometry_renderer_2d.hpp"
#include "vr/geometry/geometry_renderer_3d.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/render_target_composite_renderer.hpp"
#include "vr/render/scene_recorder_2d.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render_graph/compiled_render_graph_observability.hpp"
#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/frame_temporal_consumer.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/particle/particle_renderer_2d.hpp"
#include "vr/particle/particle_renderer_3d.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
#include "vr/shadow/shadow_renderer_3d.hpp"
#include "vr/surface/surface_renderer_2d.hpp"
#include "vr/surface/surface_renderer_3d.hpp"
#include "vr/text/text_renderer_2d.hpp"
#include "vr/text/text_renderer_3d.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace {

template<typename FnT>
[[nodiscard]] bool ThrowsAnyException(FnT&& function_) {
    try {
        function_();
    } catch (...) {
        return true;
    }
    return false;
}

void CheckSharedBindlessBindingPrefix(vr::test::TestContext& test_context_,
                                      const vr::render_graph::CompiledPass& pass_) {
    test_context_.Require(pass_.descriptor_bindings.size() >= 2U,
                          "pass_.descriptor_bindings.size() >= 2U",
                          {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[0U].set == 0U,
                        "pass_.descriptor_bindings[0U].set == 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[0U].binding == 0U,
                        "pass_.descriptor_bindings[0U].binding == 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[0U].source ==
                            vr::render_graph::DescriptorBindingSource::bindless_table,
                        "pass_.descriptor_bindings[0U].source == DescriptorBindingSource::bindless_table",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[0U].kind ==
                            vr::render_graph::DescriptorBindingKind::sampled_image_table,
                        "pass_.descriptor_bindings[0U].kind == DescriptorBindingKind::sampled_image_table",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[0U].source_id ==
                            vr::render::BindlessResourceSystem::SampledImageTableContractId().value,
                        "pass_.descriptor_bindings[0U].source_id == SampledImageTableContractId()",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[1U].set == 1U,
                        "pass_.descriptor_bindings[1U].set == 1U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[1U].binding == 0U,
                        "pass_.descriptor_bindings[1U].binding == 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[1U].source ==
                            vr::render_graph::DescriptorBindingSource::bindless_table,
                        "pass_.descriptor_bindings[1U].source == DescriptorBindingSource::bindless_table",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[1U].kind ==
                            vr::render_graph::DescriptorBindingKind::sampler_table,
                        "pass_.descriptor_bindings[1U].kind == DescriptorBindingKind::sampler_table",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[1U].source_id ==
                            vr::render::BindlessResourceSystem::SamplerTableContractId().value,
                        "pass_.descriptor_bindings[1U].source_id == SamplerTableContractId()",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
}

void CheckSharedBindlessBindings(vr::test::TestContext& test_context_,
                                 const vr::render_graph::CompiledPass& pass_) {
    test_context_.Require(pass_.descriptor_bindings.size() == 2U,
                          "pass_.descriptor_bindings.size() == 2U",
                          {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    CheckSharedBindlessBindingPrefix(test_context_, pass_);
}

void CheckNoDescriptorBindings(vr::test::TestContext& test_context_,
                               const vr::render_graph::CompiledPass& pass_) {
    test_context_.Check(pass_.descriptor_bindings.empty(),
                        "pass_.descriptor_bindings.empty()",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
}

void CheckSurface2DGraphBindings(vr::test::TestContext& test_context_,
                                 const vr::render_graph::CompiledPass& pass_) {
    test_context_.Require(pass_.descriptor_bindings.size() == 7U,
                          "pass_.descriptor_bindings.size() == 7U",
                          {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    CheckSharedBindlessBindingPrefix(test_context_, pass_);
    for (std::uint32_t binding_index = 2U; binding_index < 6U; ++binding_index) {
        const auto& binding = pass_.descriptor_bindings[binding_index];
        test_context_.Check(binding.set == 2U,
                            "binding.set == 2U",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.binding == binding_index - 2U,
                            "binding.binding == binding_index - 2U",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.source ==
                                vr::render_graph::DescriptorBindingSource::external_buffer,
                            "binding.source == DescriptorBindingSource::external_buffer",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.kind ==
                                vr::render_graph::DescriptorBindingKind::storage_buffer,
                            "binding.kind == DescriptorBindingKind::storage_buffer",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.source_id != 0U,
                            "binding.source_id != 0U",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    }
    const auto& uniform_binding = pass_.descriptor_bindings[6U];
    test_context_.Check(uniform_binding.set == 2U,
                        "uniform_binding.set == 2U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(uniform_binding.binding == 4U,
                        "uniform_binding.binding == 4U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(uniform_binding.source ==
                            vr::render_graph::DescriptorBindingSource::external_buffer,
                        "uniform_binding.source == DescriptorBindingSource::external_buffer",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(uniform_binding.kind ==
                            vr::render_graph::DescriptorBindingKind::uniform_buffer,
                        "uniform_binding.kind == DescriptorBindingKind::uniform_buffer",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(uniform_binding.source_id != 0U,
                        "uniform_binding.source_id != 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
}

void CheckSurface3DGraphBindings(vr::test::TestContext& test_context_,
                                 const vr::render_graph::CompiledPass& pass_) {
    test_context_.Require(pass_.descriptor_bindings.size() == 4U,
                          "pass_.descriptor_bindings.size() == 4U",
                          {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    CheckSharedBindlessBindingPrefix(test_context_, pass_);
    const auto& appearance_binding = pass_.descriptor_bindings[2U];
    test_context_.Check(appearance_binding.set == 2U,
                        "appearance_binding.set == 2U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(appearance_binding.binding == 8U,
                        "appearance_binding.binding == 8U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(appearance_binding.source ==
                            vr::render_graph::DescriptorBindingSource::external_buffer,
                        "appearance_binding.source == DescriptorBindingSource::external_buffer",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(appearance_binding.kind ==
                            vr::render_graph::DescriptorBindingKind::storage_buffer,
                        "appearance_binding.kind == DescriptorBindingKind::storage_buffer",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(appearance_binding.source_id != 0U,
                        "appearance_binding.source_id != 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    const auto& ibl_binding = pass_.descriptor_bindings[3U];
    test_context_.Check(ibl_binding.set == 3U,
                        "ibl_binding.set == 3U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(ibl_binding.binding == 0U,
                        "ibl_binding.binding == 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(ibl_binding.source ==
                            vr::render_graph::DescriptorBindingSource::external_buffer,
                        "ibl_binding.source == DescriptorBindingSource::external_buffer",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(ibl_binding.kind ==
                            vr::render_graph::DescriptorBindingKind::uniform_buffer,
                        "ibl_binding.kind == DescriptorBindingKind::uniform_buffer",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(ibl_binding.source_id != 0U,
                        "ibl_binding.source_id != 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
}

void CheckGeometry3DGraphBindings(vr::test::TestContext& test_context_,
                                  const vr::render_graph::CompiledPass& pass_) {
    test_context_.Require(pass_.descriptor_bindings.size() == 11U,
                          "pass_.descriptor_bindings.size() == 11U",
                          {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    CheckSharedBindlessBindingPrefix(test_context_, pass_);

    for (std::uint32_t binding_index = 0U; binding_index < 4U; ++binding_index) {
        const auto& binding = pass_.descriptor_bindings[2U + binding_index];
        test_context_.Check(binding.set == 2U,
                            "binding.set == 2U",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.binding == binding_index,
                            "binding.binding == binding_index",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.source ==
                                vr::render_graph::DescriptorBindingSource::external_buffer,
                            "binding.source == DescriptorBindingSource::external_buffer",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.kind ==
                                vr::render_graph::DescriptorBindingKind::storage_buffer,
                            "binding.kind == DescriptorBindingKind::storage_buffer",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.source_id != 0U,
                            "binding.source_id != 0U",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    }

    const auto& lighting_uniform_binding = pass_.descriptor_bindings[6U];
    test_context_.Check(lighting_uniform_binding.set == 2U,
                        "lighting_uniform_binding.set == 2U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(lighting_uniform_binding.binding == 4U,
                        "lighting_uniform_binding.binding == 4U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(lighting_uniform_binding.kind ==
                            vr::render_graph::DescriptorBindingKind::uniform_buffer,
                        "lighting_uniform_binding.kind == DescriptorBindingKind::uniform_buffer",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(lighting_uniform_binding.source_id != 0U,
                        "lighting_uniform_binding.source_id != 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});

    for (std::uint32_t binding_index = 5U; binding_index <= 7U; ++binding_index) {
        const auto& binding = pass_.descriptor_bindings[2U + binding_index];
        test_context_.Check(binding.set == 2U,
                            "binding.set == 2U",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.binding == binding_index,
                            "binding.binding == binding_index",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.source ==
                                vr::render_graph::DescriptorBindingSource::external_buffer,
                            "binding.source == DescriptorBindingSource::external_buffer",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.kind ==
                                vr::render_graph::DescriptorBindingKind::storage_buffer,
                            "binding.kind == DescriptorBindingKind::storage_buffer",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
        test_context_.Check(binding.source_id != 0U,
                            "binding.source_id != 0U",
                            {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    }

    const auto& ibl_binding = pass_.descriptor_bindings[10U];
    test_context_.Check(ibl_binding.set == 3U,
                        "ibl_binding.set == 3U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(ibl_binding.binding == 0U,
                        "ibl_binding.binding == 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(ibl_binding.source ==
                            vr::render_graph::DescriptorBindingSource::external_buffer,
                        "ibl_binding.source == DescriptorBindingSource::external_buffer",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(ibl_binding.kind ==
                            vr::render_graph::DescriptorBindingKind::uniform_buffer,
                        "ibl_binding.kind == DescriptorBindingKind::uniform_buffer",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(ibl_binding.source_id != 0U,
                        "ibl_binding.source_id != 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
}

VR_TEST_CASE(RenderGraphBuilder_tracks_linear_resource_versioning,
             "unit;core;render_graph") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 1920U, .height = 1080U, .depth = 1U},
        });

    const auto prepare = builder.AddPass("prepare_visibility");
    const auto shade = builder.AddPass("shade_scene");
    const auto present = builder.AddPass("present_scene", true);

    const auto version0 = builder.Read(prepare, scene_color);
    const auto version1 = builder.Write(shade, scene_color);
    const auto present_input = builder.Read(present, scene_color);

    const auto compiled = builder.Compile();

    VR_CHECK(version0.version == 0U);
    VR_CHECK(version1.version == 1U);
    VR_CHECK(present_input.version == 1U);
    VR_REQUIRE(compiled.ExecutionOrder().size() == 3U);
    VR_CHECK(compiled.ExecutionOrder()[0].index == prepare.index);
    VR_CHECK(compiled.ExecutionOrder()[1].index == shade.index);
    VR_CHECK(compiled.ExecutionOrder()[2].index == present.index);
    VR_REQUIRE(compiled.LivenessRanges().size() == 2U);
    VR_CHECK(compiled.LivenessRanges()[0].version.version == 0U);
    VR_CHECK(compiled.LivenessRanges()[0].first_pass_order == 0U);
    VR_CHECK(compiled.LivenessRanges()[0].last_pass_order == 1U);
    VR_CHECK(compiled.LivenessRanges()[1].version.version == 1U);
    VR_CHECK(compiled.LivenessRanges()[1].first_pass_order == 1U);
    VR_CHECK(compiled.LivenessRanges()[1].last_pass_order == 2U);
    VR_CHECK(compiled.Passes()[0].reads.size() == 1U);
    VR_CHECK(compiled.Passes()[0].reads[0].access == vr::render_graph::AccessKind::none);
    VR_CHECK(compiled.Passes()[1].reads.empty());
    VR_CHECK(compiled.Passes()[1].writes.size() == 1U);
    VR_CHECK(compiled.Passes()[1].writes[0].access == vr::render_graph::AccessKind::none);
}

VR_TEST_CASE(RenderGraphBuilder_culls_unrooted_work,
             "unit;core;render_graph") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scratch = builder.CreateBuffer(
        "scratch_buffer",
        vr::render_graph::BufferDesc{.size_bytes = 4096U});
    const auto orphan = builder.AddPass("orphan_pass");

    (void)builder.Write(orphan, scratch);

    const auto compiled = builder.Compile();
    VR_CHECK(compiled.Empty());
    VR_CHECK(compiled.Passes().empty());
    VR_CHECK(compiled.LivenessRanges().empty());
}

VR_TEST_CASE(RenderGraphBuilder_rejects_non_latest_version_writes,
             "unit;core;render_graph") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto history = builder.CreateTexture(
        "history",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
        });
    const auto writer0 = builder.AddPass("writer0");
    const auto writer1 = builder.AddPass("writer1", true);

    const auto version1 = builder.Write(writer0, history);
    VR_CHECK(version1.version == 1U);
    VR_CHECK(ThrowsAnyException([&]() {
        (void)builder.Write(writer1,
                            vr::render_graph::ResourceVersionHandle{
                                .resource_index = history.index,
                                .version = 0U,
                            });
    }));
}

VR_TEST_CASE(RenderGraphBuilder_preserves_explicit_access_descriptors_and_queue_preferences,
             "unit;core;render_graph") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_constants = builder.CreateBuffer(
        "scene_constants",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_uniform_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 640U, .height = 360U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        });
    const auto present_target = builder.CreateTexture(
        "present_target",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::unknown,
            .extent = {.width = 640U, .height = 360U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_present_flag,
        },
        vr::render_graph::ResourceLifetime::imported);

    const auto upload = builder.AddPass("upload_constants", false, vr::render_graph::QueueClass::transfer);
    const auto shade = builder.AddPass("main_scene_pass", false, vr::render_graph::QueueClass::graphics);
    const auto present = builder.AddPass("present_to_swapchain", true, vr::render_graph::QueueClass::graphics);

    const auto uploaded_constants = builder.Write(
        upload,
        scene_constants,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::transfer_write,
            .buffer_range = {.offset_bytes = 128U, .size_bytes = 512U},
        });
    (void)builder.Read(
        shade,
        uploaded_constants,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::uniform_read,
            .buffer_range = {.offset_bytes = 128U, .size_bytes = 512U},
        });
    const auto shaded_color = builder.Write(
        shade,
        scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
            .subresource_range = {.base_mip_level = 0U, .level_count = 1U, .base_array_layer = 0U, .layer_count = 1U},
        });
    (void)builder.Read(
        present,
        shaded_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
            .subresource_range = {.base_mip_level = 0U, .level_count = 1U, .base_array_layer = 0U, .layer_count = 1U},
        });
    (void)builder.Write(
        present,
        present_target,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::present,
        });

    const auto compiled = builder.Compile();

    VR_REQUIRE(compiled.Passes().size() == 3U);
    VR_CHECK(compiled.Passes()[0].queue == vr::render_graph::QueueClass::transfer);
    VR_CHECK(compiled.Passes()[1].queue == vr::render_graph::QueueClass::graphics);
    VR_CHECK(compiled.Passes()[2].queue == vr::render_graph::QueueClass::graphics);
    VR_REQUIRE(compiled.Passes()[0].writes.size() == 1U);
    VR_CHECK(compiled.Passes()[0].writes[0].access == vr::render_graph::AccessKind::transfer_write);
    VR_CHECK(compiled.Passes()[0].writes[0].buffer_range.offset_bytes == 128U);
    VR_CHECK(compiled.Passes()[0].writes[0].buffer_range.size_bytes == 512U);
    VR_REQUIRE(compiled.Passes()[1].reads.size() == 1U);
    VR_CHECK(compiled.Passes()[1].reads[0].access == vr::render_graph::AccessKind::uniform_read);
    VR_CHECK(compiled.Passes()[1].reads[0].buffer_range.offset_bytes == 128U);
    VR_CHECK(compiled.Passes()[1].reads[0].buffer_range.size_bytes == 512U);
    VR_REQUIRE(compiled.Passes()[1].writes.size() == 1U);
    VR_CHECK(compiled.Passes()[1].writes[0].access == vr::render_graph::AccessKind::color_attachment_write);
    VR_CHECK(compiled.Passes()[1].writes[0].subresource_range.level_count == 1U);
    VR_REQUIRE(compiled.Passes()[2].reads.size() == 1U);
    VR_CHECK(compiled.Passes()[2].reads[0].access == vr::render_graph::AccessKind::shader_sample_read);
    VR_REQUIRE(compiled.Passes()[2].writes.size() == 1U);
    VR_CHECK(compiled.Passes()[2].writes[0].access == vr::render_graph::AccessKind::present);
}

VR_TEST_CASE(RenderGraphRuntimeService_is_registered_in_runtime_profile,
             "unit;core;render_graph;runtime") {
    using Runtime = vr::runtime::Runtime<>;
    using RenderGraphRuntimeService = vr::runtime::services::RenderGraphRuntimeService;

    static_assert(Runtime::RuntimeServicesType::Contains<RenderGraphRuntimeService>());

    Runtime runtime{};
    auto& services = runtime.Services();
    VR_CHECK(services.TryGet<RenderGraphRuntimeService>() != nullptr);
}

VR_TEST_CASE(FrameSnapshot3D_copies_packet_metadata_and_stable_view_state,
             "unit;core;render_graph;snapshot") {
    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 8.0F, .origin_y = 4.0F, .width = 640.0F, .height = 360.0F};
    camera.runtime.culling_mask = 0x1234U;
    camera.runtime.revision = 7U;

    vr::ecs::Transform<vr::ecs::Dim3> camera_transform{};
    camera_transform.runtime.world_revision = 11U;

    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        &camera_transform,
        vr::render::RenderViewKind::world,
        3U);
    view.layer_mask = 0x00FF00FFU;
    view.debug_flags = vr::render::render_view_debug_wireframe_flag;
    view.background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    view.background_override.state.revision = 42U;
    view.background_override.gpu.index = 9U;
    view.background_override.gpu.generation = 2U;
    vr::render::RefreshRenderViewSignature(view);

    auto packet = vr::render::MakeSingleViewScenePacket(view, 99U);
    packet.debug_flags = view.debug_flags;
    packet.extra.environment = view.background_override.state;
    packet.extra.environment_gpu = view.background_override.gpu;
    packet.extra.ibl_environment_id = 5U;
    vr::render::RefreshRenderScenePacketSignature(packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(
        packet,
        17U,
        vr::render_graph::Extent3D{.width = 1920U, .height = 1080U, .depth = 1U});
    const auto* active_view = snapshot.ActiveView();

    VR_REQUIRE(active_view != nullptr);
    VR_CHECK(snapshot.frame_index == 17U);
    VR_CHECK(snapshot.reference_extent.width == 1920U);
    VR_CHECK(snapshot.reference_extent.height == 1080U);
    VR_CHECK(snapshot.kind == vr::render::RenderScenePacketKind::world);
    VR_CHECK(snapshot.selection.active_view_index == 0U);
    VR_CHECK(snapshot.selection.scene_view_index == 0U);
    VR_CHECK(snapshot.selection.overlay_view_index == vr::render::invalid_scene_view_index);
    VR_CHECK(snapshot.submission_id.value == 99U);
    VR_CHECK(snapshot.ViewCount() == 1U);
    VR_CHECK(snapshot.debug_flags == vr::render::render_view_debug_wireframe_flag);
    VR_CHECK(snapshot.extra.environment.revision == 42U);
    VR_CHECK(snapshot.extra.environment_gpu.index == 9U);
    VR_CHECK(snapshot.extra.ibl_environment_id == 5U);
    VR_CHECK(active_view->view_index == 3U);
    VR_CHECK(active_view->has_camera == 1U);
    VR_CHECK(active_view->has_camera_transform == 1U);
    VR_CHECK(active_view->camera.runtime.culling_mask == 0x1234U);
    VR_CHECK(active_view->camera.runtime.revision == 7U);
    VR_CHECK(active_view->camera_transform_world_revision == 11U);
    VR_CHECK(active_view->background_override.gpu.index == 9U);
}

VR_TEST_CASE(FrameSnapshot2D_resolves_scene_and_overlay_view_selection,
             "unit;core;render_graph;snapshot") {
    vr::ecs::Camera<vr::ecs::Dim2> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 800.0F, .height = 600.0F};
    world_camera.runtime.culling_mask = 0x1U;
    world_camera.runtime.revision = 5U;

    vr::ecs::Camera<vr::ecs::Dim2> ui_camera = world_camera;
    ui_camera.runtime.revision = 6U;

    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto ui_view = vr::render::MakeRenderViewFromCamera(
        ui_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);

    const std::array views{world_view, ui_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        1U,
        123U,
        vr::render::RenderScenePacketKind::mixed);
    const auto snapshot = vr::render_graph::MakeFrameSnapshot(
        packet,
        88U,
        vr::render_graph::Extent3D{.width = 800U, .height = 600U, .depth = 1U});

    VR_CHECK(snapshot.selection.active_view_index == 1U);
    VR_CHECK(snapshot.selection.scene_view_index == 0U);
    VR_CHECK(snapshot.selection.overlay_view_index == 1U);
    VR_REQUIRE(snapshot.SceneView() != nullptr);
    VR_REQUIRE(snapshot.OverlayView() != nullptr);
    VR_CHECK(snapshot.SceneView()->kind == vr::render::RenderViewKind::world);
    VR_CHECK(snapshot.OverlayView()->kind == vr::render::RenderViewKind::ui);
    VR_CHECK(snapshot.reference_extent.width == 800U);
    VR_CHECK(snapshot.reference_extent.height == 600U);
}

VR_TEST_CASE(FrameSnapshot_submission_schema_matches_packet_schema_contract,
             "unit;core;render_graph;snapshot;contract") {
    std::array<vr::render::RenderView3D, 2U> views{};
    views[0U].kind = vr::render::RenderViewKind::world;
    views[0U].flags = vr::render::render_view_lighting_enabled_flag;
    views[0U].layer_mask = 0x5U;
    vr::render::RefreshRenderViewSignature(views[0U]);
    views[1U].kind = vr::render::RenderViewKind::ui;
    views[1U].flags = vr::render::render_view_overlay_enabled_flag;
    views[1U].layer_mask = 0x8U;
    vr::render::RefreshRenderViewSignature(views[1U]);

    auto packet = vr::render::MakeScenePacketFromViewRange(views.data(),
                                                           static_cast<std::uint32_t>(views.size()),
                                                           1U,
                                                           vr::render::SceneSubmissionId{515U},
                                                           vr::render::RenderScenePacketKind::mixed);
    packet.flags = vr::render::render_scene_packet_allow_overlay_flag;
    packet.debug_flags = vr::render::render_view_debug_bounds_flag;
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.Payload().environment.mode = vr::scene::SkyEnvironmentMode::gradient;
    packet.Payload().environment.sky_texture_id = vr::asset::TextureId{811U};
    packet.Payload().environment.revision = 23U;
    packet.Payload().environment_gpu = vr::scene::SkyEnvironmentGpuHandle{.index = 6U, .generation = 4U};
    packet.Payload().ibl_environment_id = vr::render::IblEnvironmentId{42U};
    vr::render::RefreshRenderScenePacketSignature(packet);

    const auto packet_schema = vr::render::MakeSceneSubmissionSchema(packet);
    const auto snapshot = vr::render_graph::MakeFrameSnapshot(
        packet,
        19U,
        vr::render_graph::Extent3D{.width = 1024U, .height = 576U, .depth = 1U});
    const auto snapshot_schema = vr::render_graph::MakeSceneSubmissionSchema(snapshot);

    VR_CHECK(snapshot.Metadata().submission_id == vr::render::SceneSubmissionId{515U});
    VR_CHECK(snapshot_schema.metadata.kind == packet_schema.metadata.kind);
    VR_CHECK(snapshot_schema.metadata.flags == packet_schema.metadata.flags);
    VR_CHECK(snapshot_schema.metadata.render_layer_mask == packet_schema.metadata.render_layer_mask);
    VR_CHECK(snapshot_schema.metadata.debug_flags == packet_schema.metadata.debug_flags);
    VR_CHECK(snapshot_schema.metadata.postprocess_policy == packet_schema.metadata.postprocess_policy);
    VR_CHECK(snapshot_schema.metadata.submission_id == packet_schema.metadata.submission_id);
    VR_CHECK(snapshot_schema.selection.active_view_index == packet_schema.selection.active_view_index);
    VR_CHECK(snapshot_schema.selection.scene_view_index == packet_schema.selection.scene_view_index);
    VR_CHECK(snapshot_schema.selection.overlay_view_index == packet_schema.selection.overlay_view_index);
    VR_CHECK(snapshot_schema.payload.environment.mode == packet_schema.payload.environment.mode);
    VR_CHECK(snapshot_schema.payload.environment.sky_texture_id == packet_schema.payload.environment.sky_texture_id);
    VR_CHECK(snapshot_schema.payload.environment.revision == packet_schema.payload.environment.revision);
    VR_CHECK(snapshot_schema.payload.environment_gpu.index == packet_schema.payload.environment_gpu.index);
    VR_CHECK(snapshot_schema.payload.ibl_environment_id == packet_schema.payload.ibl_environment_id);
}

VR_TEST_CASE(FrameSnapshot_schema_helpers_apply_runtime_facing_envelope_without_manual_field_copy,
             "unit;core;render_graph;snapshot;contract") {
    vr::render_graph::FrameSnapshot3D snapshot{};
    snapshot.views.push_back(vr::render_graph::FrameViewSnapshot3D{});

    const vr::render::SceneSubmissionSchema<vr::ecs::Dim3> schema{
        .metadata = {
            .kind = vr::render::RenderScenePacketKind::custom,
            .flags = vr::render::render_scene_packet_allow_shadow_flag,
            .render_layer_mask = 0x44U,
            .debug_flags = vr::render::render_view_debug_wireframe_flag,
            .postprocess_policy = vr::render::RenderPostProcessPolicy::disabled,
            .submission_id = vr::render::SceneSubmissionId{901U},
        },
        .selection = {
            .active_view_index = 0U,
            .scene_view_index = 0U,
            .overlay_view_index = vr::render::invalid_scene_view_index,
        },
        .payload = {
            .environment = {
                .mode = vr::scene::SkyEnvironmentMode::cubemap,
                .sky_texture_id = vr::asset::TextureId{515U},
                .revision = 31U,
            },
            .environment_gpu = vr::scene::SkyEnvironmentGpuHandle{.index = 3U, .generation = 7U},
            .ibl_environment_id = vr::render::IblEnvironmentId{55U},
        },
    };

    vr::render_graph::ApplySceneSubmissionSchema(snapshot, schema);
    vr::render_graph::RefreshFrameSnapshotSignature(snapshot);

    VR_CHECK(snapshot.Metadata().kind == vr::render::RenderScenePacketKind::custom);
    VR_CHECK(snapshot.Metadata().submission_id == vr::render::SceneSubmissionId{901U});
    VR_CHECK(snapshot.selection.active_view_index == 0U);
    VR_CHECK(snapshot.Payload().environment.sky_texture_id == vr::asset::TextureId{515U});
    VR_CHECK(snapshot.Payload().environment_gpu.generation == 7U);
    VR_CHECK(snapshot.Payload().ibl_environment_id == vr::render::IblEnvironmentId{55U});

    const auto resolved = vr::render_graph::MakeSceneSubmissionSchema(snapshot);
    VR_CHECK(resolved.metadata.render_layer_mask == 0x44U);
    VR_CHECK(resolved.metadata.debug_flags == vr::render::render_view_debug_wireframe_flag);
    VR_CHECK(resolved.payload.environment.mode == vr::scene::SkyEnvironmentMode::cubemap);
    VR_CHECK(resolved.payload.environment.revision == 31U);
    VR_CHECK(resolved.payload.ibl_environment_id == vr::render::IblEnvironmentId{55U});
}

VR_TEST_CASE(FrameSnapshot_schema_formalizes_compiled_scene_handoff_with_view_roundtrip,
             "unit;core;render_graph;snapshot;contract") {
    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 960.0F, .height = 540.0F};
    world_camera.runtime.culling_mask = 0x1357U;
    world_camera.runtime.revision = 44U;

    vr::ecs::Transform<vr::ecs::Dim3> world_transform{};
    world_transform.runtime.world_revision = 71U;

    std::array<vr::render::RenderView3D, 2U> views{};
    views[0U] = vr::render::MakeRenderViewFromCamera(world_camera,
                                                     &world_transform,
                                                     vr::render::RenderViewKind::world,
                                                     0U);
    views[0U].debug_flags = vr::render::render_view_debug_bounds_flag;
    views[0U].background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    views[0U].background_override.state.mode = vr::scene::SkyEnvironmentMode::gradient;
    views[0U].background_override.state.revision = 27U;
    views[0U].background_override.gpu = {.index = 5U, .generation = 2U};
    vr::render::RefreshRenderViewSignature(views[0U]);

    views[1U].kind = vr::render::RenderViewKind::ui;
    views[1U].view_index = 1U;
    views[1U].flags = vr::render::render_view_overlay_enabled_flag;
    views[1U].layer_mask = 0x40U;
    views[1U].viewport = {.x = 0.0F, .y = 0.0F, .width = 960.0F, .height = 540.0F, .min_depth = 0.0F, .max_depth = 1.0F};
    views[1U].scissor = {.x = 0, .y = 0, .width = 960U, .height = 540U};
    vr::render::RefreshRenderViewSignature(views[1U]);

    auto packet = vr::render::MakeScenePacketFromViewRange(views.data(),
                                                           static_cast<std::uint32_t>(views.size()),
                                                           0U,
                                                           vr::render::SceneSubmissionId{1201U},
                                                           vr::render::RenderScenePacketKind::mixed);
    packet.flags = vr::render::render_scene_packet_allow_overlay_flag;
    packet.debug_flags = vr::render::render_view_debug_wireframe_flag;
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.Payload().environment.mode = vr::scene::SkyEnvironmentMode::cubemap;
    packet.Payload().environment.sky_texture_id = vr::asset::TextureId{177U};
    packet.Payload().environment.revision = 66U;
    packet.Payload().environment_gpu = {.index = 4U, .generation = 9U};
    packet.Payload().ibl_environment_id = vr::render::IblEnvironmentId{21U};
    vr::render::RefreshRenderScenePacketSignature(packet);

    const auto schema = vr::render_graph::MakeFrameSnapshotSchema(
        packet,
        37U,
        vr::render_graph::Extent3D{.width = 960U, .height = 540U, .depth = 1U});

    static_assert(std::is_same_v<decltype(schema.submission),
                                 vr::render::SceneSubmissionSchema<vr::ecs::Dim3>>);
    static_assert(std::is_same_v<typename vr::render_graph::FrameSnapshotSchema3D::ViewType,
                                 vr::render_graph::FrameViewSchema3D>);

    VR_CHECK(schema.frame_index == 37U);
    VR_CHECK(schema.reference_extent.width == 960U);
    VR_CHECK(schema.Metadata().submission_id == vr::render::SceneSubmissionId{1201U});
    VR_CHECK(schema.Metadata().debug_flags == vr::render::render_view_debug_wireframe_flag);
    VR_CHECK(schema.Payload().environment.sky_texture_id == vr::asset::TextureId{177U});
    VR_REQUIRE(schema.ActiveView() != nullptr);
    VR_REQUIRE(schema.OverlayView() != nullptr);
    VR_CHECK(schema.ActiveView()->kind == vr::render::RenderViewKind::world);
    VR_CHECK(schema.ActiveView()->camera.runtime.revision == 44U);
    VR_CHECK(schema.ActiveView()->camera_transform_world_revision == 71U);
    VR_CHECK(schema.ActiveView()->background_override.state.revision == 27U);
    VR_CHECK(schema.OverlayView()->kind == vr::render::RenderViewKind::ui);
    VR_CHECK(schema.OverlayView()->view_index == 1U);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(schema);
    const auto roundtrip = vr::render_graph::MakeFrameSnapshotSchema(snapshot);

    VR_CHECK(snapshot.frame_index == 37U);
    VR_CHECK(snapshot.Metadata().submission_id == vr::render::SceneSubmissionId{1201U});
    VR_REQUIRE(snapshot.ActiveView() != nullptr);
    VR_CHECK(snapshot.ActiveView()->camera.runtime.revision == 44U);
    VR_CHECK(snapshot.ActiveView()->background_override.state.revision == 27U);
    VR_CHECK(roundtrip.reference_extent.width == schema.reference_extent.width);
    VR_CHECK(roundtrip.Metadata().kind == schema.Metadata().kind);
    VR_CHECK(roundtrip.Selection().overlay_view_index ==
             schema.Selection().overlay_view_index);
    VR_CHECK(roundtrip.Payload().environment_gpu.generation ==
             schema.Payload().environment_gpu.generation);
    VR_CHECK(roundtrip.views.size() == schema.views.size());
    VR_CHECK(vr::render_graph::MakeFrameViewSnapshot(roundtrip.views[0U]).signature ==
             snapshot.views[0U].signature);
}

VR_TEST_CASE(FrameSnapshot_schema_helpers_apply_full_compiled_scene_without_manual_view_copy,
             "unit;core;render_graph;snapshot;contract") {
    vr::render_graph::FrameSnapshot2D snapshot{};

    const vr::render_graph::FrameSnapshotSchema2D schema{
        .frame_index = 73U,
        .reference_extent = {.width = 320U, .height = 200U, .depth = 1U},
        .submission =
            {
                .metadata = {
                    .kind = vr::render::RenderScenePacketKind::ui,
                    .flags = vr::render::render_scene_packet_allow_overlay_flag,
                    .render_layer_mask = 0x99U,
                    .debug_flags = vr::render::render_view_debug_bounds_flag,
                    .postprocess_policy = vr::render::RenderPostProcessPolicy::disabled,
                    .submission_id = vr::render::SceneSubmissionId{303U},
                },
                .selection = {
                    .active_view_index = 0U,
                    .scene_view_index = vr::render::invalid_scene_view_index,
                    .overlay_view_index = 0U,
                },
                .payload =
                    {
                        .background = {
                            .mode = vr::scene::Background2DMode::sprite,
                            .image_id = vr::surface::SurfaceImageId{19U},
                            .revision = 11U,
                        },
                    },
            },
        .views =
            {
                vr::render_graph::FrameViewSchema2D{
                    .kind = vr::render::RenderViewKind::ui,
                    .has_camera = 0U,
                    .has_camera_transform = 0U,
                    .view_index = 5U,
                    .flags = vr::render::render_view_overlay_enabled_flag,
                    .culling_mask = 0xFFFF'FFFFU,
                    .layer_mask = 0x77U,
                    .debug_flags = vr::render::render_view_debug_bounds_flag,
                    .postprocess_policy = vr::render::RenderPostProcessPolicy::disabled,
                    .viewport = {.x = 0.0F, .y = 0.0F, .width = 320.0F, .height = 200.0F, .min_depth = 0.0F, .max_depth = 1.0F},
                    .scissor = {.x = 0, .y = 0, .width = 320U, .height = 200U},
                    .background_override =
                        {
                            .mode = vr::render::BackgroundOverrideMode::override_state,
                            .state =
                                {
                                    .mode = vr::scene::Background2DMode::solid_color,
                                    .revision = 8U,
                                },
                        },
                    .camera = {},
                    .camera_transform_world_revision = 0U,
                },
            },
    };

    vr::render_graph::ApplyFrameSnapshotSchema(snapshot, schema);
    vr::render_graph::RefreshFrameSnapshotSignature(snapshot);

    VR_CHECK(snapshot.frame_index == 73U);
    VR_CHECK(snapshot.reference_extent.width == 320U);
    VR_CHECK(snapshot.Metadata().submission_id == vr::render::SceneSubmissionId{303U});
    VR_CHECK(snapshot.Payload().background.image_id == vr::surface::SurfaceImageId{19U});
    VR_REQUIRE(snapshot.ActiveView() != nullptr);
    VR_CHECK(snapshot.ActiveView()->view_index == 5U);
    VR_CHECK(snapshot.ActiveView()->background_override.state.revision == 8U);

    const auto resolved = vr::render_graph::MakeFrameSnapshotSchema(snapshot);
    VR_CHECK(resolved.submission.metadata.render_layer_mask == 0x99U);
    VR_CHECK(resolved.submission.selection.overlay_view_index == 0U);
    VR_REQUIRE(resolved.views.size() == 1U);
    VR_CHECK(resolved.views[0U].view_index == 5U);
    VR_CHECK(vr::render_graph::MakeFrameViewSnapshot(resolved.views[0U]).signature ==
             snapshot.views[0U].signature);
}

VR_TEST_CASE(FrameSnapshot_temporal_history_contract_roundtrips_without_baking_runtime_handle_identity,
             "unit;core;render_graph;snapshot;temporal") {
    vr::render_graph::FrameSnapshot3D snapshot{};
    snapshot.frame_index = 81U;
    snapshot.reference_extent = {.width = 640U, .height = 360U, .depth = 1U};
    snapshot.views.push_back(vr::render_graph::FrameViewSnapshot3D{
        .kind = vr::render::RenderViewKind::world,
        .view_index = 0U,
    });
    snapshot.selection = {
        .active_view_index = 0U,
        .scene_view_index = 0U,
        .overlay_view_index = vr::render::invalid_scene_view_index,
    };
    snapshot.temporal.color.desc = {
        .dimension = vr::render_graph::TextureDimension::image_2d,
        .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
        .extent = {.width = 640U, .height = 360U, .depth = 1U},
        .usage = vr::render_graph::texture_usage_color_attachment_flag |
                 vr::render_graph::texture_usage_sampled_flag |
                 vr::render_graph::texture_usage_transfer_src_flag |
                 vr::render_graph::texture_usage_transfer_dst_flag,
    };
    snapshot.temporal.color.previous = {
        .handle = {.index = 11U, .generation = 3U},
        .resource_revision = 5U,
    };
    snapshot.temporal.color.current = {
        .handle = {.index = 12U, .generation = 4U},
        .resource_revision = 7U,
    };
    snapshot.temporal.color.previous_submission_id = {901U};
    snapshot.temporal.color.previous_frame_index = 80U;
    snapshot.temporal.color.previous_available = true;
    snapshot.temporal.color.current_writable = true;
    snapshot.temporal.depth.desc = {
        .dimension = vr::render_graph::TextureDimension::image_2d,
        .format = vr::render_graph::TextureFormat::d32_sfloat,
        .extent = {.width = 640U, .height = 360U, .depth = 1U},
        .usage = vr::render_graph::texture_usage_sampled_flag |
                 vr::render_graph::texture_usage_transfer_src_flag |
                 vr::render_graph::texture_usage_transfer_dst_flag,
    };
    snapshot.temporal.depth.previous = {
        .handle = {.index = 21U, .generation = 6U},
        .resource_revision = 9U,
    };
    snapshot.temporal.depth.current = {
        .handle = {.index = 22U, .generation = 7U},
        .resource_revision = 10U,
    };
    snapshot.temporal.depth.previous_submission_id = {902U};
    snapshot.temporal.depth.previous_frame_index = 79U;
    snapshot.temporal.depth.previous_available = true;
    snapshot.temporal.depth.current_writable = true;
    snapshot.temporal.motion.desc = {
        .dimension = vr::render_graph::TextureDimension::image_2d,
        .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
        .extent = {.width = 640U, .height = 360U, .depth = 1U},
        .usage = vr::render_graph::texture_usage_color_attachment_flag |
                 vr::render_graph::texture_usage_sampled_flag |
                 vr::render_graph::texture_usage_transfer_src_flag |
                 vr::render_graph::texture_usage_transfer_dst_flag,
    };
    snapshot.temporal.motion.previous = {
        .handle = {.index = 31U, .generation = 8U},
        .resource_revision = 11U,
    };
    snapshot.temporal.motion.current = {
        .handle = {.index = 32U, .generation = 9U},
        .resource_revision = 12U,
    };
    snapshot.temporal.motion.previous_submission_id = {903U};
    snapshot.temporal.motion.previous_frame_index = 78U;
    snapshot.temporal.motion.previous_available = true;
    snapshot.temporal.motion.current_writable = true;
    snapshot.temporal.reprojection.current_clip_to_previous_clip = {
        .m = {1.0F, 0.0F, 0.0F, 0.0F,
              0.0F, 1.0F, 0.0F, 0.0F,
              0.0F, 0.0F, 1.0F, 0.0F,
              0.1F, 0.2F, 0.0F, 1.0F},
    };
    snapshot.temporal.reprojection.previous_submission_id = {904U};
    snapshot.temporal.reprojection.previous_frame_index = 77U;
    snapshot.temporal.reprojection.current_available = true;
    snapshot.temporal.reprojection.previous_available = true;
    snapshot.temporal.jitter.current_uv_x = 0.125F / 640.0F;
    snapshot.temporal.jitter.current_uv_y = -0.25F / 360.0F;
    snapshot.temporal.jitter.previous_uv_x = -0.375F / 640.0F;
    snapshot.temporal.jitter.previous_uv_y = 0.25F / 360.0F;
    snapshot.temporal.jitter.previous_submission_id = {905U};
    snapshot.temporal.jitter.previous_frame_index = 76U;
    snapshot.temporal.jitter.current_available = true;
    snapshot.temporal.jitter.previous_available = true;
    vr::render_graph::RefreshFrameSnapshotSignature(snapshot);

    const auto roundtrip =
        vr::render_graph::MakeFrameSnapshot(
            vr::render_graph::MakeFrameSnapshotSchema(snapshot));
    VR_CHECK(roundtrip.temporal.color.previous.handle.index ==
             snapshot.temporal.color.previous.handle.index);
    VR_CHECK(roundtrip.temporal.color.previous.handle.generation ==
             snapshot.temporal.color.previous.handle.generation);
    VR_CHECK(roundtrip.temporal.color.previous.resource_revision ==
             snapshot.temporal.color.previous.resource_revision);
    VR_CHECK(roundtrip.temporal.color.current.handle.index ==
             snapshot.temporal.color.current.handle.index);
    VR_CHECK(roundtrip.temporal.color.current.handle.generation ==
             snapshot.temporal.color.current.handle.generation);
    VR_CHECK(roundtrip.temporal.color.current.resource_revision ==
             snapshot.temporal.color.current.resource_revision);
    VR_CHECK(roundtrip.temporal.color.previous_submission_id ==
             snapshot.temporal.color.previous_submission_id);
    VR_CHECK(roundtrip.temporal.color.previous_frame_index ==
             snapshot.temporal.color.previous_frame_index);
    VR_CHECK(roundtrip.temporal.color.previous_available);
    VR_CHECK(roundtrip.temporal.color.current_writable);
    VR_CHECK(roundtrip.temporal.depth.previous.handle.index ==
             snapshot.temporal.depth.previous.handle.index);
    VR_CHECK(roundtrip.temporal.depth.previous.handle.generation ==
             snapshot.temporal.depth.previous.handle.generation);
    VR_CHECK(roundtrip.temporal.depth.previous.resource_revision ==
             snapshot.temporal.depth.previous.resource_revision);
    VR_CHECK(roundtrip.temporal.depth.current.handle.index ==
             snapshot.temporal.depth.current.handle.index);
    VR_CHECK(roundtrip.temporal.depth.current.handle.generation ==
             snapshot.temporal.depth.current.handle.generation);
    VR_CHECK(roundtrip.temporal.depth.current.resource_revision ==
             snapshot.temporal.depth.current.resource_revision);
    VR_CHECK(roundtrip.temporal.depth.previous_submission_id ==
             snapshot.temporal.depth.previous_submission_id);
    VR_CHECK(roundtrip.temporal.depth.previous_frame_index ==
             snapshot.temporal.depth.previous_frame_index);
    VR_CHECK(roundtrip.temporal.depth.previous_available);
    VR_CHECK(roundtrip.temporal.depth.current_writable);
    VR_CHECK(roundtrip.temporal.motion.previous_available);
    VR_CHECK(roundtrip.temporal.motion.current_writable);
    VR_CHECK(roundtrip.temporal.motion.previous_submission_id ==
             snapshot.temporal.motion.previous_submission_id);
    VR_CHECK(roundtrip.temporal.motion.previous_frame_index ==
             snapshot.temporal.motion.previous_frame_index);
    VR_CHECK(roundtrip.temporal.reprojection.current_available);
    VR_CHECK(roundtrip.temporal.reprojection.previous_available);
    VR_CHECK(roundtrip.temporal.reprojection.previous_submission_id ==
             snapshot.temporal.reprojection.previous_submission_id);
    VR_CHECK(roundtrip.temporal.reprojection.previous_frame_index ==
             snapshot.temporal.reprojection.previous_frame_index);
    VR_CHECK(roundtrip.temporal.reprojection.current_clip_to_previous_clip.m[12] ==
             snapshot.temporal.reprojection.current_clip_to_previous_clip.m[12]);
    VR_CHECK(roundtrip.temporal.jitter.current_available);
    VR_CHECK(roundtrip.temporal.jitter.previous_available);
    VR_CHECK(roundtrip.temporal.jitter.previous_submission_id ==
             snapshot.temporal.jitter.previous_submission_id);
    VR_CHECK(roundtrip.temporal.jitter.previous_frame_index ==
             snapshot.temporal.jitter.previous_frame_index);
    VR_CHECK(roundtrip.temporal.jitter.current_uv_x ==
             snapshot.temporal.jitter.current_uv_x);
    VR_CHECK(roundtrip.temporal.jitter.previous_uv_y ==
             snapshot.temporal.jitter.previous_uv_y);

    auto different_runtime_identity = snapshot;
    different_runtime_identity.temporal.color.previous = {
        .handle = {.index = 44U, .generation = 9U},
        .resource_revision = 13U,
    };
    different_runtime_identity.temporal.color.current = {
        .handle = {.index = 55U, .generation = 2U},
        .resource_revision = 17U,
    };
    different_runtime_identity.temporal.depth.previous = {
        .handle = {.index = 66U, .generation = 1U},
        .resource_revision = 19U,
    };
    different_runtime_identity.temporal.depth.current = {
        .handle = {.index = 77U, .generation = 3U},
        .resource_revision = 20U,
    };
    different_runtime_identity.temporal.motion.previous = {
        .handle = {.index = 88U, .generation = 4U},
        .resource_revision = 21U,
    };
    different_runtime_identity.temporal.motion.current = {
        .handle = {.index = 99U, .generation = 5U},
        .resource_revision = 22U,
    };
    different_runtime_identity.temporal.color.previous_submission_id = {77U};
    different_runtime_identity.temporal.color.previous_frame_index = 12U;
    different_runtime_identity.temporal.reprojection.current_clip_to_previous_clip.m[12] =
        3.5F;
    different_runtime_identity.temporal.jitter.current_uv_x = -0.25F / 640.0F;
    different_runtime_identity.temporal.jitter.current_uv_y = 0.125F / 360.0F;
    different_runtime_identity.temporal.jitter.previous_uv_x = 0.375F / 640.0F;
    different_runtime_identity.temporal.jitter.previous_uv_y = -0.125F / 360.0F;
    different_runtime_identity.temporal.jitter.previous_submission_id = {17U};
    different_runtime_identity.temporal.jitter.previous_frame_index = 4U;
    vr::render_graph::RefreshFrameSnapshotSignature(different_runtime_identity);
    VR_CHECK(different_runtime_identity.signature == snapshot.signature);

    auto warmup_snapshot = snapshot;
    warmup_snapshot.temporal.color.previous_available = false;
    warmup_snapshot.temporal.color.previous = {};
    warmup_snapshot.temporal.color.previous_submission_id = {};
    warmup_snapshot.temporal.color.previous_frame_index = 0U;
    warmup_snapshot.temporal.depth.previous_available = false;
    warmup_snapshot.temporal.depth.previous = {};
    warmup_snapshot.temporal.depth.previous_submission_id = {};
    warmup_snapshot.temporal.depth.previous_frame_index = 0U;
    warmup_snapshot.temporal.depth.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::first_frame;
    warmup_snapshot.temporal.motion.previous_available = false;
    warmup_snapshot.temporal.motion.previous = {};
    warmup_snapshot.temporal.motion.previous_submission_id = {};
    warmup_snapshot.temporal.motion.previous_frame_index = 0U;
    warmup_snapshot.temporal.motion.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::first_frame;
    warmup_snapshot.temporal.reprojection.previous_available = false;
    warmup_snapshot.temporal.reprojection.previous_submission_id = {};
    warmup_snapshot.temporal.reprojection.previous_frame_index = 0U;
    warmup_snapshot.temporal.reprojection.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::first_frame;
    warmup_snapshot.temporal.jitter.previous_available = false;
    warmup_snapshot.temporal.jitter.previous_submission_id = {};
    warmup_snapshot.temporal.jitter.previous_frame_index = 0U;
    warmup_snapshot.temporal.jitter.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::first_frame;
    vr::render_graph::RefreshFrameSnapshotSignature(warmup_snapshot);
    VR_CHECK(warmup_snapshot.signature != snapshot.signature);
}

VR_TEST_CASE(FrameTemporalConsumer_helpers_import_previous_surfaces_and_gate_readiness,
             "unit;core;render_graph;temporal;consumer") {
    vr::render_graph::RenderGraphBuilder builder{};
    vr::render_graph::FrameTemporalContract temporal{};
    temporal.color.desc = {
        .dimension = vr::render_graph::TextureDimension::image_2d,
        .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
        .extent = {.width = 128U, .height = 72U, .depth = 1U},
        .usage = vr::render_graph::texture_usage_sampled_flag |
                 vr::render_graph::texture_usage_transfer_src_flag |
                 vr::render_graph::texture_usage_transfer_dst_flag,
    };
    temporal.color.previous = {
        .handle = {.index = 9U, .generation = 2U},
        .resource_revision = 5U,
    };
    temporal.color.previous_available = true;
    temporal.color.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::none;
    temporal.depth.desc = {
        .dimension = vr::render_graph::TextureDimension::image_2d,
        .format = vr::render_graph::TextureFormat::d32_sfloat,
        .extent = {.width = 128U, .height = 72U, .depth = 1U},
        .usage = vr::render_graph::texture_usage_sampled_flag |
                 vr::render_graph::texture_usage_transfer_src_flag |
                 vr::render_graph::texture_usage_transfer_dst_flag,
    };
    temporal.depth.previous = {
        .handle = {.index = 10U, .generation = 4U},
        .resource_revision = 6U,
    };
    temporal.depth.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::first_frame;
    temporal.motion.desc = {
        .dimension = vr::render_graph::TextureDimension::image_2d,
        .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
        .extent = {.width = 128U, .height = 72U, .depth = 1U},
        .usage = vr::render_graph::texture_usage_color_attachment_flag |
                 vr::render_graph::texture_usage_sampled_flag,
    };
    temporal.motion.current = {
        .handle = {.index = 11U, .generation = 6U},
        .resource_revision = 8U,
    };
    temporal.motion.current_writable = true;
    temporal.motion.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::first_frame;
    temporal.reprojection.current_available = true;
    temporal.reprojection.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::first_frame;
    temporal.jitter.current_available = true;
    temporal.jitter.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::first_frame;

    bool registered = false;
    vr::render_graph::ResourceHandle registered_resource{};
    vr::render::RenderTargetHandle registered_target{};
    const auto imported_color =
        vr::render_graph::ImportPreviousTemporalSurface(
            builder,
            "temporal_previous_color",
            temporal.color,
            [&](const vr::render_graph::ResourceHandle logical_,
                const vr::render::RenderTargetHandle handle_) {
                registered = true;
                registered_resource = logical_;
                registered_target = handle_;
            });

    VR_CHECK(imported_color.available);
    VR_CHECK(registered);
    VR_CHECK(imported_color.resource.index == registered_resource.index);
    VR_CHECK(registered_target.index == temporal.color.previous.handle.index);

    bool registered_current = false;
    const auto imported_motion =
        vr::render_graph::ImportCurrentTemporalSurface(
            builder,
            "temporal_current_motion",
            temporal.motion,
            [&](const vr::render_graph::ResourceHandle logical_,
                const vr::render::RenderTargetHandle handle_) {
                registered_current = true;
                registered_resource = logical_;
                registered_target = handle_;
            });
    VR_CHECK(imported_motion.available);
    VR_CHECK(registered_current);
    VR_CHECK(registered_target.index == temporal.motion.current.handle.index);

    const auto pass = builder.AddPass("temporal_consumer");
    const auto previous_version =
        builder.Read(pass,
                     imported_color.resource,
                     vr::render_graph::AccessDesc{
                         .access =
                             vr::render_graph::AccessKind::shader_sample_read,
                     });
    VR_CHECK(vr::render_graph::IsValidResourceVersionHandle(previous_version));

    const auto blocked_readiness =
        vr::render_graph::EvaluateTemporalConsumerAvailability(
            temporal,
            vr::render_graph::TemporalConsumerRequirements{
                .requires_previous_color = true,
                .requires_previous_depth = true,
                .requires_previous_motion = false,
                .requires_current_motion = true,
                .requires_temporal_jitter = true,
            });
    VR_CHECK(!blocked_readiness.ready);
    VR_CHECK(blocked_readiness.fallback_to_current);
    VR_CHECK(blocked_readiness.previous_color_available);
    VR_CHECK(!blocked_readiness.previous_depth_available);
    VR_CHECK(!blocked_readiness.current_motion_available);
    VR_CHECK(!blocked_readiness.temporal_jitter_available);
    VR_CHECK(blocked_readiness.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);

    temporal.depth.previous_available = true;
    temporal.depth.previous_submission_id = {44U};
    temporal.depth.previous_frame_index = 7U;
    temporal.depth.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::none;
    temporal.reprojection.previous_available = true;
    temporal.reprojection.previous_submission_id = {45U};
    temporal.reprojection.previous_frame_index = 7U;
    temporal.reprojection.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::none;
    temporal.jitter.previous_submission_id = {46U};
    temporal.jitter.previous_frame_index = 7U;
    temporal.jitter.previous_available = false;
    temporal.jitter.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::first_frame;
    const auto still_blocked_by_motion =
        vr::render_graph::EvaluateTemporalConsumerAvailability(
            temporal,
            vr::render_graph::TemporalConsumerRequirements{
                .requires_previous_color = true,
                .requires_previous_depth = true,
                .requires_previous_motion = false,
                .requires_current_motion = true,
                .requires_temporal_jitter = true,
            });
    VR_CHECK(!still_blocked_by_motion.ready);
    VR_CHECK(still_blocked_by_motion.fallback_to_current);
    VR_CHECK(!still_blocked_by_motion.current_motion_available);
    VR_CHECK(still_blocked_by_motion.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);
    temporal.motion.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::none;
    const auto blocked_by_jitter =
        vr::render_graph::EvaluateTemporalConsumerAvailability(
            temporal,
            vr::render_graph::TemporalConsumerRequirements{
                .requires_previous_color = true,
                .requires_previous_depth = true,
                .requires_previous_motion = false,
                .requires_current_motion = true,
                .requires_temporal_jitter = true,
            });
    VR_CHECK(!blocked_by_jitter.ready);
    VR_CHECK(blocked_by_jitter.fallback_to_current);
    VR_CHECK(blocked_by_jitter.current_motion_available);
    VR_CHECK(!blocked_by_jitter.temporal_jitter_available);
    VR_CHECK(blocked_by_jitter.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);
    temporal.jitter.previous_available = true;
    temporal.jitter.invalidation_reason =
        vr::render_graph::FrameHistoryInvalidationReason::none;
    const auto ready =
        vr::render_graph::EvaluateTemporalConsumerAvailability(
            temporal,
            vr::render_graph::TemporalConsumerRequirements{
                .requires_previous_color = true,
                .requires_previous_depth = true,
                .requires_previous_motion = false,
                .requires_current_motion = true,
                .requires_temporal_jitter = true,
            });
    VR_CHECK(ready.ready);
    VR_CHECK(!ready.fallback_to_current);
    VR_CHECK(ready.previous_depth_available);
    VR_CHECK(ready.current_motion_available);
    VR_CHECK(ready.temporal_jitter_available);
    VR_CHECK(ready.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
}

VR_TEST_CASE(FrameViewSchema_strips_derived_signature_but_reconstructs_equivalent_compiled_view,
             "unit;core;render_graph;snapshot;contract") {
    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 10.0F, .origin_y = 20.0F, .width = 640.0F, .height = 360.0F};
    camera.runtime.culling_mask = 0xABC0U;
    camera.runtime.revision = 28U;

    vr::ecs::Transform<vr::ecs::Dim3> transform{};
    transform.runtime.world_revision = 19U;

    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        &transform,
        vr::render::RenderViewKind::world,
        3U);
    view.debug_flags = vr::render::render_view_debug_culling_flag;
    view.background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    view.background_override.state.mode = vr::scene::SkyEnvironmentMode::gradient;
    view.background_override.state.revision = 77U;
    view.background_override.gpu = {.index = 12U, .generation = 6U};
    vr::render::RefreshRenderViewSignature(view);

    const auto schema = vr::render_graph::MakeFrameViewSchema(view);
    const auto snapshot = vr::render_graph::MakeFrameViewSnapshot(schema);
    const auto direct_snapshot = vr::render_graph::MakeFrameViewSnapshot(view);
    const auto roundtrip_schema = vr::render_graph::MakeFrameViewSchema(snapshot);

    VR_CHECK(schema.view_index == 3U);
    VR_CHECK(schema.camera.runtime.revision == 28U);
    VR_CHECK(schema.camera_transform_world_revision == 19U);
    VR_CHECK(schema.background_override.state.revision == 77U);
    VR_CHECK(snapshot.signature == direct_snapshot.signature);
    VR_CHECK(roundtrip_schema.view_index == schema.view_index);
    VR_CHECK(roundtrip_schema.camera.runtime.revision == schema.camera.runtime.revision);
    VR_CHECK(roundtrip_schema.background_override.gpu.generation ==
             schema.background_override.gpu.generation);
}

VR_TEST_CASE(FrameSnapshot3D_signature_ignores_pointer_identity,
             "unit;core;render_graph;snapshot") {
    vr::ecs::Camera<vr::ecs::Dim3> camera_a{};
    camera_a.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 200.0F};
    camera_a.runtime.culling_mask = 0xAA55U;
    camera_a.runtime.revision = 21U;

    vr::ecs::Camera<vr::ecs::Dim3> camera_b = camera_a;

    vr::ecs::Transform<vr::ecs::Dim3> transform_a{};
    transform_a.runtime.world_revision = 44U;
    vr::ecs::Transform<vr::ecs::Dim3> transform_b = transform_a;

    auto view_a = vr::render::MakeRenderViewFromCamera(
        camera_a,
        &transform_a,
        vr::render::RenderViewKind::world,
        1U);
    auto view_b = vr::render::MakeRenderViewFromCamera(
        camera_b,
        &transform_b,
        vr::render::RenderViewKind::world,
        1U);

    view_a.background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    view_b.background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    view_a.background_override.state.revision = 8U;
    view_b.background_override.state.revision = 8U;
    view_a.background_override.gpu.index = 6U;
    view_b.background_override.gpu.index = 6U;
    view_a.background_override.gpu.generation = 1U;
    view_b.background_override.gpu.generation = 1U;
    vr::render::RefreshRenderViewSignature(view_a);
    vr::render::RefreshRenderViewSignature(view_b);

    VR_CHECK(view_a.signature != view_b.signature);

    const auto frame_view_a = vr::render_graph::MakeFrameViewSnapshot(view_a);
    const auto frame_view_b = vr::render_graph::MakeFrameViewSnapshot(view_b);
    VR_CHECK(frame_view_a.signature == frame_view_b.signature);

    auto packet_a = vr::render::MakeSingleViewScenePacket(view_a, 100U);
    auto packet_b = vr::render::MakeSingleViewScenePacket(view_b, 200U);
    packet_a.extra.environment = view_a.background_override.state;
    packet_b.extra.environment = view_b.background_override.state;
    packet_a.extra.environment_gpu = view_a.background_override.gpu;
    packet_b.extra.environment_gpu = view_b.background_override.gpu;
    packet_a.extra.ibl_environment_id = 3U;
    packet_b.extra.ibl_environment_id = 3U;

    const auto snapshot_a = vr::render_graph::MakeFrameSnapshot(packet_a, 1U);
    const auto snapshot_b = vr::render_graph::MakeFrameSnapshot(packet_b, 2U);
    VR_CHECK(snapshot_a.signature == snapshot_b.signature);
}

VR_TEST_CASE(RenderGraphRuntimeService_resets_frame_local_state_on_begin_frame,
             "unit;core;render_graph;runtime") {
    vr::runtime::services::RenderGraphRuntimeService service{};
    const auto scratch = service.Builder().CreateBuffer(
        "scratch",
        vr::render_graph::BufferDesc{.size_bytes = 64U});
    const auto writer = service.Builder().AddPass("writer", true);
    (void)service.Builder().Write(writer, scratch);
    service.SetCompiledGraph(service.Builder().Compile());

    vr::render_graph::FrameSnapshot3D snapshot{};
    snapshot.views.push_back(vr::render_graph::FrameViewSnapshot3D{});
    vr::render_graph::RefreshFrameSnapshotSignature(snapshot);
    service.SetFrameSnapshot<vr::ecs::Dim3>(snapshot);

    VR_REQUIRE(service.TryGetCompiledGraph() != nullptr);
    VR_REQUIRE(service.TryGetFrameSnapshot<vr::ecs::Dim3>() != nullptr);

    service.BeginFrame(7U);

    VR_CHECK(service.FrameIndex() == 7U);
    VR_CHECK(service.Builder().ResourceCount() == 0U);
    VR_CHECK(service.Builder().PassCount() == 0U);
    VR_CHECK(service.TryGetCompiledGraph() == nullptr);
    VR_CHECK(service.PlannedVulkanBarriers().barrier_batches.empty());
    VR_CHECK(service.PlannedCommandReadyVulkanBarriers().command_batches.empty());
    VR_CHECK(service.PlannedCommandReadyVulkanBarriers().queue_transfer_batches.empty());
    VR_CHECK(!service.HasFrameSnapshot());
}

VR_TEST_CASE(RenderGraphRuntimeService_tracks_dimensioned_snapshots,
             "unit;core;render_graph;runtime") {
    vr::runtime::services::RenderGraphRuntimeService service{};

    vr::render_graph::FrameSnapshot2D snapshot2d{};
    snapshot2d.views.push_back(vr::render_graph::FrameViewSnapshot2D{});
    vr::render_graph::RefreshFrameSnapshotSignature(snapshot2d);
    service.SetFrameSnapshot<vr::ecs::Dim2>(snapshot2d);

    VR_REQUIRE(service.TryGetFrameSnapshot<vr::ecs::Dim2>() != nullptr);
    VR_CHECK(service.TryGetFrameSnapshot<vr::ecs::Dim3>() == nullptr);

    vr::render_graph::FrameSnapshot3D snapshot3d{};
    snapshot3d.views.push_back(vr::render_graph::FrameViewSnapshot3D{});
    vr::render_graph::RefreshFrameSnapshotSignature(snapshot3d);
    service.SetFrameSnapshot<vr::ecs::Dim3>(snapshot3d);

    VR_REQUIRE(service.TryGetFrameSnapshot<vr::ecs::Dim3>() != nullptr);
    VR_CHECK(service.TryGetFrameSnapshot<vr::ecs::Dim2>() == nullptr);
}

VR_TEST_CASE(RenderGraphRuntimeService_captures_snapshot_during_prepare_tick_frame,
             "unit;core;render_graph;runtime") {
    using Host = vr::runtime::detail::RuntimeHost<vr::platform::ActiveBackendTag, 2U>;

    struct FakeRecorder final {
        const vr::render::RenderScenePacket3D* frame_packet = nullptr;

        void PrepareFrame(const vr::render::SceneRecorder3DPrepareView&) noexcept {}

        void Record(const vr::render::FrameRecordContext&) noexcept {}

        [[nodiscard]] const vr::render::RenderScenePacket3D* FramePacket() const noexcept {
            return frame_packet;
        }
    };

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 128.0F, .height = 72.0F};
    camera.runtime.culling_mask = 0x55AAU;
    camera.runtime.revision = 12U;

    vr::ecs::Transform<vr::ecs::Dim3> transform{};
    transform.runtime.world_revision = 34U;

    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        &transform,
        vr::render::RenderViewKind::world,
        2U);
    view.background_override.mode = vr::render::BackgroundOverrideMode::override_state;
    view.background_override.state.revision = 9U;
    view.background_override.gpu.index = 4U;
    view.background_override.gpu.generation = 1U;

    auto packet = vr::render::MakeSingleViewScenePacket(view, 333U);
    packet.extra.environment = view.background_override.state;
    packet.extra.environment_gpu = view.background_override.gpu;
    packet.extra.ibl_environment_id = 7U;

    Host host{};
    FakeRecorder recorder{};
    recorder.frame_packet = &packet;

    host.PrepareTickFrame(recorder, 5U);

    auto& service = host.Services().Get<vr::runtime::services::RenderGraphRuntimeService>();
    const auto* snapshot = service.TryGetFrameSnapshot<vr::ecs::Dim3>();
    VR_REQUIRE(snapshot != nullptr);
    VR_CHECK(snapshot->frame_index == 5U);
    VR_CHECK(snapshot->reference_extent.width == 128U);
    VR_CHECK(snapshot->reference_extent.height == 72U);
    VR_CHECK(snapshot->submission_id.value == 333U);
    VR_CHECK(snapshot->ViewCount() == 1U);
    VR_CHECK(snapshot->extra.ibl_environment_id == 7U);
    VR_REQUIRE(snapshot->ActiveView() != nullptr);
    VR_CHECK(snapshot->ActiveView()->camera.runtime.revision == 12U);
    VR_CHECK(snapshot->ActiveView()->camera_transform_world_revision == 34U);
}

VR_TEST_CASE(RenderGraphBuilder_builds_minimal_scene_overlay_present_chain,
             "unit;core;render_graph;runtime") {
    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 640.0F, .height = 360.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x10U;

    vr::ecs::Camera<vr::ecs::Dim3> overlay_camera = world_camera;
    overlay_camera.runtime.revision = 2U;

    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto overlay_view = vr::render::MakeRenderViewFromCamera(
        overlay_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);

    const std::array views{world_view, overlay_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        0U,
        444U,
        vr::render::RenderScenePacketKind::mixed);
    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 9U);

    vr::render_graph::RenderGraphBuilder builder{};
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_CHECK(build_result.has_scene_pass);
    VR_CHECK(build_result.has_overlay_pass);
    VR_CHECK(build_result.has_depth);
    VR_REQUIRE(compiled.ExecutionOrder().size() == 4U);
    VR_REQUIRE(compiled.Passes().size() == 4U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[1].debug_name == "overlay_pass");
    VR_CHECK(compiled.Passes()[2].debug_name == "present_to_swapchain");
    VR_CHECK(compiled.Passes()[3].debug_name == "present_transition");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_CHECK(compiled.Passes()[1].executable);
    VR_CHECK(compiled.Passes()[2].executable);
    VR_CHECK(!compiled.Passes()[3].executable);
    VR_REQUIRE(compiled.Passes()[0].raster_pass.has_value());
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_CHECK(!compiled.Passes()[2].raster_pass.has_value());
    VR_CHECK(!compiled.Passes()[3].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[2].execute));
    VR_REQUIRE(compiled.Passes()[0].writes.size() == 2U);
    VR_CHECK(compiled.Passes()[0].writes[0].access == vr::render_graph::AccessKind::color_attachment_write);
    VR_CHECK(compiled.Passes()[0].writes[1].access == vr::render_graph::AccessKind::depth_stencil_write);
    VR_REQUIRE(compiled.Passes()[1].reads.size() == 1U);
    VR_CHECK(compiled.Passes()[1].reads[0].access == vr::render_graph::AccessKind::shader_sample_read);
    VR_REQUIRE(compiled.Passes()[2].reads.size() == 1U);
    VR_CHECK(compiled.Passes()[2].reads[0].access == vr::render_graph::AccessKind::transfer_read);
    VR_REQUIRE(compiled.Passes()[2].writes.size() == 1U);
    VR_CHECK(compiled.Passes()[2].writes[0].access == vr::render_graph::AccessKind::transfer_write);
    VR_REQUIRE(compiled.Passes()[3].reads.size() == 1U);
    VR_CHECK(compiled.Passes()[3].reads[0].access == vr::render_graph::AccessKind::present);
}

VR_TEST_CASE(SceneRecorder2D_build_render_graph_routes_geometry_scene_to_scene_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder2D recorder{};
    recorder.Initialize();
    vr::geometry::GeometryRenderer2D geometry_renderer{};
    geometry_renderer.Initialize();
    recorder.RegisterSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim2> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 4401U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 25U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    recorder.BuildRenderGraph(builder, snapshot, build_result, color_chain);
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 3U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_REQUIRE(compiled.Passes()[0].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[0].execute));
    CheckNoDescriptorBindings(test_context_, compiled.Passes()[0]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder2D_build_render_graph_routes_background_to_scene_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder2D recorder{};
    recorder.Initialize();

    vr::ecs::Camera<vr::ecs::Dim2> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 4403U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.extra.background.mode = vr::scene::Background2DMode::solid_color;
    packet.extra.background.color0 = {.x = 0.15F, .y = 0.25F, .z = 0.35F, .w = 1.0F};
    packet.extra.background.opacity = 1.0F;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 27U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    recorder.BuildRenderGraph(builder, snapshot, build_result, color_chain);
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 3U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_REQUIRE(compiled.Passes()[0].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[0].execute));
    CheckNoDescriptorBindings(test_context_, compiled.Passes()[0]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder2D_build_render_graph_routes_overlay_text_to_overlay_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder2D recorder{};
    recorder.Initialize();
    vr::text::TextRenderer2D overlay_text{};
    overlay_text.Initialize();
    recorder.RegisterOverlayRenderer(overlay_text);

    vr::ecs::Camera<vr::ecs::Dim2> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto overlay_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);
    const std::array views{world_view, overlay_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        0U,
        4405U,
        vr::render::RenderScenePacketKind::mixed);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.flags = vr::render::render_scene_packet_allow_overlay_flag;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 29U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    recorder.BuildRenderGraph(builder, snapshot, build_result, color_chain);
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 4U);
    VR_CHECK(compiled.Passes()[1].debug_name == "overlay_pass");
    VR_CHECK(compiled.Passes()[1].executable);
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[1].execute));
    CheckSharedBindlessBindings(test_context_, compiled.Passes()[1]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder2D_build_render_graph_routes_overlay_surface_to_overlay_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder2D recorder{};
    recorder.Initialize();
    vr::surface::SurfaceRenderer2D overlay_surface{};
    overlay_surface.Initialize();
    recorder.RegisterOverlayRenderer(overlay_surface);

    vr::ecs::Camera<vr::ecs::Dim2> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto overlay_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);
    const std::array views{world_view, overlay_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        0U,
        4406U,
        vr::render::RenderScenePacketKind::mixed);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.flags = vr::render::render_scene_packet_allow_overlay_flag;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 30U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    recorder.BuildRenderGraph(builder, snapshot, build_result, color_chain);
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 4U);
    VR_CHECK(compiled.Passes()[1].debug_name == "overlay_pass");
    VR_CHECK(compiled.Passes()[1].executable);
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[1].execute));
    CheckSurface2DGraphBindings(test_context_, compiled.Passes()[1]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder2D_build_render_graph_routes_overlay_particle_to_overlay_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder2D recorder{};
    recorder.Initialize();
    vr::particle::ParticleRenderer2D overlay_particle{};
    overlay_particle.Initialize();
    recorder.RegisterOverlayRenderer(overlay_particle);

    vr::ecs::Camera<vr::ecs::Dim2> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto overlay_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);
    const std::array views{world_view, overlay_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        0U,
        4407U,
        vr::render::RenderScenePacketKind::mixed);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.flags = vr::render::render_scene_packet_allow_overlay_flag;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 31U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    recorder.BuildRenderGraph(builder, snapshot, build_result, color_chain);
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 4U);
    VR_CHECK(compiled.Passes()[1].debug_name == "overlay_pass");
    VR_CHECK(compiled.Passes()[1].executable);
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[1].execute));
    CheckSharedBindlessBindings(test_context_, compiled.Passes()[1]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder2D_build_render_graph_routes_scene_consumer_to_present,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder2D recorder{};
    recorder.Initialize();
    vr::render::RenderTargetCompositeRenderer composite_renderer{};
    composite_renderer.Initialize();
    recorder.RegisterSceneConsumer(composite_renderer);

    vr::ecs::Camera<vr::ecs::Dim2> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 4407U);
    packet.flags = vr::render::render_scene_packet_allow_postprocess_flag;
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::enabled;
    world_view.flags = vr::render::render_view_postprocess_enabled_flag;
    vr::render::RefreshRenderViewSignature(world_view);
    packet.views = &world_view;
    vr::render::RefreshRenderScenePacketSignature(packet);
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 31U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot2D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim2>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    auto find_pass_index = [&compiled](const std::string_view name_) -> std::size_t {
        for (std::size_t i = 0; i < compiled.Passes().size(); ++i) {
            if (compiled.Passes()[i].debug_name == name_) {
                return i;
            }
        }
        return compiled.Passes().size();
    };

    const std::size_t scene_consumer_index = find_pass_index("scene_consumer_pass");
    const std::size_t present_transition_index = find_pass_index("present_transition");
    VR_CHECK(build_result.built);
    VR_REQUIRE(scene_consumer_index < compiled.Passes().size());
    VR_REQUIRE(present_transition_index < compiled.Passes().size());
    VR_CHECK(scene_consumer_index < present_transition_index);
    VR_CHECK(compiled.Passes()[scene_consumer_index].executable);
    VR_REQUIRE(compiled.Passes()[scene_consumer_index].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[scene_consumer_index].execute));
    VR_REQUIRE(compiled.Passes()[scene_consumer_index].descriptor_bindings.size() == 2U);
    VR_CHECK(compiled.Passes()[scene_consumer_index].descriptor_bindings[0U].set == 0U);
    VR_CHECK(compiled.Passes()[scene_consumer_index].descriptor_bindings[0U].binding == 0U);
    VR_CHECK(compiled.Passes()[scene_consumer_index].descriptor_bindings[0U].source ==
             vr::render_graph::DescriptorBindingSource::bindless_table);
    VR_CHECK(compiled.Passes()[scene_consumer_index].descriptor_bindings[0U].kind ==
             vr::render_graph::DescriptorBindingKind::sampled_image_table);
    VR_CHECK(compiled.Passes()[scene_consumer_index].descriptor_bindings[0U].source_id ==
             vr::render::BindlessResourceSystem::SampledImageTableContractId().value);
    VR_CHECK(compiled.Passes()[scene_consumer_index].descriptor_bindings[1U].set == 1U);
    VR_CHECK(compiled.Passes()[scene_consumer_index].descriptor_bindings[1U].binding == 0U);
    VR_CHECK(compiled.Passes()[scene_consumer_index].descriptor_bindings[1U].source ==
             vr::render_graph::DescriptorBindingSource::bindless_table);
    VR_CHECK(compiled.Passes()[scene_consumer_index].descriptor_bindings[1U].kind ==
             vr::render_graph::DescriptorBindingKind::sampler_table);
    VR_CHECK(compiled.Passes()[scene_consumer_index].descriptor_bindings[1U].source_id ==
             vr::render::BindlessResourceSystem::SamplerTableContractId().value);
    VR_CHECK(find_pass_index("present_to_swapchain") == compiled.Passes().size());

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder2D_build_render_graph_routes_scene_consumer_before_overlay_with_copyback,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder2D recorder{};
    recorder.Initialize();
    vr::render::RenderTargetCompositeRenderer composite_renderer{};
    composite_renderer.Initialize();
    vr::text::TextRenderer2D overlay_text{};
    overlay_text.Initialize();
    recorder.RegisterSceneConsumer(composite_renderer);
    recorder.RegisterOverlayRenderer(overlay_text);

    vr::ecs::Camera<vr::ecs::Dim2> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto overlay_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim2>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);
    world_view.flags = vr::render::render_view_postprocess_enabled_flag;
    overlay_view.flags = vr::render::render_view_overlay_enabled_flag;
    vr::render::RefreshRenderViewSignature(world_view);
    vr::render::RefreshRenderViewSignature(overlay_view);
    const std::array views{world_view, overlay_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        0U,
        4409U,
        vr::render::RenderScenePacketKind::mixed);
    packet.flags = vr::render::render_scene_packet_allow_postprocess_flag |
                   vr::render::render_scene_packet_allow_overlay_flag;
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::enabled;
    vr::render::RefreshRenderScenePacketSignature(packet);
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 33U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot2D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim2>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    auto find_pass_index = [&compiled](const std::string_view name_) -> std::size_t {
        for (std::size_t i = 0; i < compiled.Passes().size(); ++i) {
            if (compiled.Passes()[i].debug_name == name_) {
                return i;
            }
        }
        return compiled.Passes().size();
    };

    const std::size_t consumer_index = find_pass_index("scene_consumer_pass");
    const std::size_t copyback_index = find_pass_index("scene_consumer_copyback");
    const std::size_t overlay_index = find_pass_index("overlay_pass");
    const std::size_t present_index = find_pass_index("present_to_swapchain");
    VR_CHECK(build_result.built);
    VR_REQUIRE(consumer_index < compiled.Passes().size());
    VR_REQUIRE(copyback_index < compiled.Passes().size());
    VR_REQUIRE(overlay_index < compiled.Passes().size());
    VR_REQUIRE(present_index < compiled.Passes().size());
    VR_CHECK(consumer_index < copyback_index);
    VR_CHECK(copyback_index < overlay_index);
    VR_CHECK(overlay_index < present_index);
    VR_CHECK(compiled.Passes()[consumer_index].executable);
    VR_REQUIRE(compiled.Passes()[consumer_index].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[consumer_index].execute));
    VR_CHECK(compiled.Passes()[copyback_index].executable);
    VR_CHECK(!compiled.Passes()[copyback_index].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[copyback_index].execute));

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_inserts_sky_prepass_before_scene,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 500U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.extra.environment.mode = vr::scene::SkyEnvironmentMode::gradient;
    packet.extra.environment.draw_order = vr::scene::SkyEnvironmentDrawOrder::before_opaque;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 11U);
    vr::render_graph::RenderGraphBuilder builder{};
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    auto color_chain = vr::render_graph::invalid_resource_version;
    recorder.BuildRenderGraph(builder, snapshot, build_result, color_chain);
    const auto compiled = builder.Compile();

    VR_REQUIRE(compiled.ExecutionOrder().size() == 4U);
    VR_CHECK(compiled.Passes()[0].debug_name == "sky_environment_pre_opaque");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_REQUIRE(compiled.Passes()[0].raster_pass.has_value());
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_CHECK(compiled.Passes()[1].debug_name == "main_scene_pass");
    VR_REQUIRE(compiled.Passes()[1].dependencies.size() == 1U);
    VR_CHECK(compiled.Passes()[1].dependencies[0].index == compiled.Passes()[0].handle.index);
    VR_REQUIRE(!compiled.Passes()[1].raster_pass->color_attachments.empty());
    VR_CHECK(compiled.Passes()[1].raster_pass->color_attachments[0].load_op ==
             vr::render_graph::AttachmentLoadOp::load);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_inserts_sky_post_opaque_before_overlay_and_present,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto overlay_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);
    const std::array views{world_view, overlay_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        0U,
        502U,
        vr::render::RenderScenePacketKind::mixed);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.extra.environment.mode = vr::scene::SkyEnvironmentMode::gradient;
    packet.extra.environment.draw_order = vr::scene::SkyEnvironmentDrawOrder::after_opaque_depth_tested;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 13U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() == 5U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[1].debug_name == "sky_environment_post_opaque");
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_REQUIRE(compiled.Passes()[1].reads.size() == 2U);
    VR_CHECK(compiled.Passes()[1].reads[0].access == vr::render_graph::AccessKind::color_attachment_read);
    VR_CHECK(compiled.Passes()[1].reads[1].access == vr::render_graph::AccessKind::depth_stencil_read);
    VR_REQUIRE(compiled.Passes()[1].writes.size() == 1U);
    VR_CHECK(compiled.Passes()[1].writes[0].access == vr::render_graph::AccessKind::color_attachment_write);
    VR_CHECK(compiled.Passes()[2].debug_name == "overlay_pass");
    VR_CHECK(compiled.Passes()[3].debug_name == "present_to_swapchain");
    VR_CHECK(compiled.Passes()[4].debug_name == "present_transition");

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_routes_single_geometry_opaque_slice_to_scene_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::geometry::GeometryRenderer3D geometry_renderer{};
    geometry_renderer.Initialize();
    recorder.RegisterOpaqueSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 777U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 10U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 3U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_REQUIRE(compiled.Passes()[0].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[0].execute));
    CheckGeometry3DGraphBindings(test_context_, compiled.Passes()[0]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_applies_temporal_jitter_view_projection_override_to_scene_renderers,
             "unit;core;render_graph;runtime;temporal;scene3d") {
    struct MockTemporalSceneRenderer final {
        void PrepareFrame(const vr::render::SceneRecorder3DPrepareView&) {}
        void RecordGraphSceneStage(vr::render_graph::GraphCommandContext&,
                                   vr::render::SceneRenderStage,
                                   vr::render_graph::ResourceHandle,
                                   vr::render_graph::ResourceHandle) {}
        void OnSwapchainRecreated(std::uint32_t,
                                  VkExtent2D,
                                  VkFormat,
                                  std::uint64_t,
                                  std::uint64_t) {}
        void SetFrameViewProjectionOverride(
            const vr::ecs::Matrix4x4& view_projection_) noexcept {
            override_active = true;
            override_matrix = view_projection_;
            set_count += 1U;
        }
        void ClearFrameViewProjectionOverride() noexcept {
            override_active = false;
            clear_count += 1U;
        }

        vr::ecs::Matrix4x4 override_matrix{};
        std::uint32_t set_count = 0U;
        std::uint32_t clear_count = 0U;
        bool override_active = false;
    };

    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    MockTemporalSceneRenderer renderer{};
    recorder.RegisterOpaqueSceneRenderer(renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    world_camera.runtime.view_matrix.m[0] = 1.0F;
    world_camera.runtime.view_matrix.m[5] = 1.0F;
    world_camera.runtime.view_matrix.m[10] = 1.0F;
    world_camera.runtime.view_matrix.m[15] = 1.0F;
    world_camera.runtime.projection_matrix = world_camera.runtime.view_matrix;
    world_camera.runtime.view_projection_matrix = world_camera.runtime.view_matrix;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 881U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    auto jittered_snapshot = vr::render_graph::MakeFrameSnapshot(
        packet,
        22U,
        vr::render_graph::Extent3D{
            .width = 320U,
            .height = 180U,
            .depth = 1U,
        });
    jittered_snapshot.temporal.jitter.current_available = true;
    jittered_snapshot.temporal.jitter.current_uv_x = 0.25F / 320.0F;
    jittered_snapshot.temporal.jitter.current_uv_y = -0.5F / 180.0F;
    vr::render_graph::RefreshFrameSnapshotSignature(jittered_snapshot);

    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        jittered_snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(
                builder_ref_,
                snapshot_ref_,
                build_result_ref_,
                color_chain_ref_);
        });

    VR_CHECK(build_result.built);
    VR_CHECK(renderer.override_active);
    VR_CHECK(renderer.set_count == 1U);
    VR_CHECK(renderer.override_matrix.m[0] == 1.0F);
    VR_CHECK(renderer.override_matrix.m[5] == 1.0F);
    VR_CHECK(renderer.override_matrix.m[10] == 1.0F);
    VR_CHECK(renderer.override_matrix.m[15] == 1.0F);
    VR_CHECK(renderer.override_matrix.m[12] == 2.0F * jittered_snapshot.temporal.jitter.current_uv_x);
    VR_CHECK(renderer.override_matrix.m[13] == 2.0F * jittered_snapshot.temporal.jitter.current_uv_y);

    auto non_jittered_snapshot = jittered_snapshot;
    non_jittered_snapshot.temporal.jitter = {};
    vr::render_graph::RefreshFrameSnapshotSignature(non_jittered_snapshot);
    vr::render_graph::RenderGraphBuilder non_jittered_builder{};
    color_chain = vr::render_graph::invalid_resource_version;
    (void)vr::render_graph::BuildMinimalFrameGraph(
        non_jittered_builder,
        non_jittered_snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(
                builder_ref_,
                snapshot_ref_,
                build_result_ref_,
                color_chain_ref_);
        });

    VR_CHECK(!renderer.override_active);
    VR_CHECK(renderer.clear_count >= 1U);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_routes_mixed_geometry_surface_text_opaque_slice_to_scene_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::geometry::GeometryRenderer3D geometry_renderer{};
    geometry_renderer.Initialize();
    vr::surface::SurfaceRenderer3D surface_renderer{};
    surface_renderer.Initialize();
    vr::text::TextRenderer3D text_renderer{};
    text_renderer.Initialize();
    recorder.RegisterOpaqueSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::single);
    recorder.RegisterOpaqueSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::single);
    recorder.RegisterOpaqueSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 779U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 15U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 3U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_REQUIRE(compiled.Passes()[0].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[0].execute));
    VR_REQUIRE(compiled.Passes()[0].descriptor_bindings.size() == 12U);
    CheckSharedBindlessBindingPrefix(test_context_, compiled.Passes()[0]);
    VR_CHECK(compiled.Passes()[0].descriptor_bindings[10U].set == 2U);
    VR_CHECK(compiled.Passes()[0].descriptor_bindings[10U].binding == 8U);
    VR_CHECK(compiled.Passes()[0].descriptor_bindings[11U].set == 3U);
    VR_CHECK(compiled.Passes()[0].descriptor_bindings[11U].binding == 0U);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_routes_mixed_surface_text_transparent_slice_to_transparent_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::surface::SurfaceRenderer3D surface_renderer{};
    surface_renderer.Initialize();
    vr::text::TextRenderer3D text_renderer{};
    text_renderer.Initialize();
    recorder.RegisterTransparentSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::single);
    recorder.RegisterTransparentSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 892U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 20U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 4U);
    VR_CHECK(compiled.Passes()[1].debug_name == "transparent_scene_pass");
    VR_CHECK(compiled.Passes()[1].executable);
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[1].execute));
    CheckSurface3DGraphBindings(test_context_, compiled.Passes()[1]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_routes_single_text_opaque_slice_to_scene_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::text::TextRenderer3D text_renderer{};
    text_renderer.Initialize();
    recorder.RegisterOpaqueSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 780U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 16U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 3U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_REQUIRE(compiled.Passes()[0].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[0].execute));

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_inserts_transparent_scene_pass_before_overlay_and_present,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::surface::SurfaceRenderer3D surface_renderer{};
    surface_renderer.Initialize();
    recorder.RegisterTransparentSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto overlay_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);
    const std::array views{world_view, overlay_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        0U,
        890U,
        vr::render::RenderScenePacketKind::mixed);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 18U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() == 5U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[1].debug_name == "transparent_scene_pass");
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_REQUIRE(compiled.Passes()[1].reads.size() >= 1U);
    VR_CHECK(compiled.Passes()[1].reads[0].access == vr::render_graph::AccessKind::color_attachment_read);
    VR_REQUIRE(compiled.Passes()[1].writes.size() == 1U);
    VR_CHECK(compiled.Passes()[1].writes[0].access == vr::render_graph::AccessKind::color_attachment_write);
    CheckSurface3DGraphBindings(test_context_, compiled.Passes()[1]);
    VR_CHECK(compiled.Passes()[2].debug_name == "overlay_pass");
    VR_CHECK(compiled.Passes()[3].debug_name == "present_to_swapchain");
    VR_CHECK(compiled.Passes()[4].debug_name == "present_transition");

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_fuses_main_and_transparent_scene_native_pass_group,
             "unit;core;render_graph;runtime;native_pass") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::surface::SurfaceRenderer3D surface_renderer{};
    surface_renderer.Initialize();
    recorder.RegisterTransparentSceneRenderer(surface_renderer,
                                             vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 896U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 24U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    auto find_pass_order = [&compiled](const std::string_view name_) -> std::uint32_t {
        for (std::uint32_t pass_order = 0U;
             pass_order < static_cast<std::uint32_t>(compiled.Passes().size());
             ++pass_order) {
            if (compiled.Passes()[pass_order].debug_name == name_) {
                return pass_order;
            }
        }
        return vr::render_graph::invalid_render_graph_index;
    };

    VR_CHECK(build_result.built);
    const std::uint32_t main_scene_order = find_pass_order("main_scene_pass");
    const std::uint32_t transparent_scene_order =
        find_pass_order("transparent_scene_pass");
    VR_REQUIRE(main_scene_order != vr::render_graph::invalid_render_graph_index);
    VR_REQUIRE(transparent_scene_order !=
               vr::render_graph::invalid_render_graph_index);

    const auto* native_group =
        compiled.NativePasses().FindGroupByPassOrder(main_scene_order);
    VR_REQUIRE(native_group != nullptr);
    VR_CHECK(native_group->first_pass_order == main_scene_order);
    VR_CHECK(native_group->last_pass_order == transparent_scene_order);
    VR_REQUIRE(native_group->attachments.has_depth_attachment);
    VR_CHECK(!native_group->attachments.depth_attachment.read_only);
    VR_CHECK(native_group->attachments.depth_attachment.effective_store_op ==
             vr::render_graph::AttachmentStoreOp::dont_care);
    VR_CHECK(native_group->attachments.depth_attachment.store_elided);
    VR_CHECK(compiled.NativePasses().summary.fused_raster_pass_count > 0U);
    VR_CHECK(compiled.NativePasses().summary.native_pass_group_count <
             compiled.NativePasses().summary.logical_raster_pass_count);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_inserts_bloom_chain_before_present_transition,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 894U);
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 22U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 6U);
    VR_CHECK(compiled.Passes()[1].debug_name == "bloom_prefilter");
    VR_CHECK(compiled.Passes()[2].debug_name == "bloom_blur_h");
    VR_CHECK(compiled.Passes()[3].debug_name == "bloom_blur_v");
    VR_CHECK(compiled.Passes()[4].debug_name == "bloom_combine");
    VR_CHECK(compiled.Passes().back().debug_name == "present_transition");
    VR_CHECK(compiled.TransientAllocations().timeline.saved_bytes > 0U);
    VR_CHECK(compiled.TransientAllocations().timeline.page_count > 0U);
    for (std::size_t pass_index = 1U; pass_index <= 4U; ++pass_index) {
        VR_REQUIRE(compiled.Passes()[pass_index].descriptor_bindings.size() == 2U);
        VR_CHECK(compiled.Passes()[pass_index].descriptor_bindings[0U].set == 0U);
        VR_CHECK(compiled.Passes()[pass_index].descriptor_bindings[0U].binding == 0U);
        VR_CHECK(compiled.Passes()[pass_index].descriptor_bindings[0U].source ==
                 vr::render_graph::DescriptorBindingSource::bindless_table);
        VR_CHECK(compiled.Passes()[pass_index].descriptor_bindings[0U].kind ==
                 vr::render_graph::DescriptorBindingKind::sampled_image_table);
        VR_CHECK(compiled.Passes()[pass_index].descriptor_bindings[0U].source_id ==
                 vr::render::BindlessResourceSystem::SampledImageTableContractId().value);
        VR_CHECK(compiled.Passes()[pass_index].descriptor_bindings[1U].set == 1U);
        VR_CHECK(compiled.Passes()[pass_index].descriptor_bindings[1U].binding == 0U);
        VR_CHECK(compiled.Passes()[pass_index].descriptor_bindings[1U].source ==
                 vr::render_graph::DescriptorBindingSource::bindless_table);
        VR_CHECK(compiled.Passes()[pass_index].descriptor_bindings[1U].kind ==
                 vr::render_graph::DescriptorBindingKind::sampler_table);
        VR_CHECK(compiled.Passes()[pass_index].descriptor_bindings[1U].source_id ==
                 vr::render::BindlessResourceSystem::SamplerTableContractId().value);
    }

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_routes_bloom_chain_before_overlay_then_present,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto overlay_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);
    const std::array views{world_view, overlay_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        0U,
        895U,
        vr::render::RenderScenePacketKind::mixed);
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 23U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    auto find_pass_index = [&compiled](const std::string_view name_) -> std::size_t {
        for (std::size_t i = 0; i < compiled.Passes().size(); ++i) {
            if (compiled.Passes()[i].debug_name == name_) {
                return i;
            }
        }
        return compiled.Passes().size();
    };

    VR_CHECK(build_result.built);
    const std::size_t bloom_prefilter_index = find_pass_index("bloom_prefilter");
    const std::size_t bloom_combine_index = find_pass_index("bloom_combine");
    const std::size_t overlay_index = find_pass_index("overlay_pass");
    const std::size_t present_index = find_pass_index("present_to_swapchain");
    const std::size_t transition_index = find_pass_index("present_transition");
    VR_REQUIRE(bloom_prefilter_index < compiled.Passes().size());
    VR_REQUIRE(bloom_combine_index < compiled.Passes().size());
    VR_REQUIRE(overlay_index < compiled.Passes().size());
    VR_REQUIRE(present_index < compiled.Passes().size());
    VR_REQUIRE(transition_index < compiled.Passes().size());
    VR_CHECK(bloom_prefilter_index < bloom_combine_index);
    VR_CHECK(bloom_combine_index < overlay_index);
    VR_CHECK(overlay_index < present_index);
    VR_CHECK(present_index < transition_index);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_routes_single_text_transparent_slice_to_transparent_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::text::TextRenderer3D text_renderer{};
    text_renderer.Initialize();
    recorder.RegisterTransparentSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 891U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 19U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 4U);
    VR_CHECK(compiled.Passes()[1].debug_name == "transparent_scene_pass");
    VR_CHECK(compiled.Passes()[1].executable);
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[1].execute));

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_routes_mixed_surface_text_particle_transparent_slice_to_transparent_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::surface::SurfaceRenderer3D surface_renderer{};
    surface_renderer.Initialize();
    vr::text::TextRenderer3D text_renderer{};
    text_renderer.Initialize();
    vr::particle::ParticleRenderer3D particle_renderer{};
    particle_renderer.Initialize();
    recorder.RegisterTransparentSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::single);
    recorder.RegisterTransparentSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::single);
    recorder.RegisterTransparentSceneRenderer(particle_renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 893U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 21U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 4U);
    VR_CHECK(compiled.Passes()[1].debug_name == "transparent_scene_pass");
    VR_CHECK(compiled.Passes()[1].executable);
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[1].execute));
    CheckSurface3DGraphBindings(test_context_, compiled.Passes()[1]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_routes_single_particle_transparent_slice_to_transparent_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::particle::ParticleRenderer3D particle_renderer{};
    particle_renderer.Initialize();
    recorder.RegisterTransparentSceneRenderer(particle_renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 894U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 22U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 4U);
    VR_CHECK(compiled.Passes()[1].debug_name == "transparent_scene_pass");
    VR_CHECK(compiled.Passes()[1].executable);
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[1].execute));
    CheckSharedBindlessBindings(test_context_, compiled.Passes()[1]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_routes_overlay_text_to_overlay_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::text::TextRenderer2D overlay_text{};
    overlay_text.Initialize();
    recorder.RegisterOverlayRenderer(overlay_text);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto overlay_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::ui,
        1U);
    const std::array views{world_view, overlay_view};
    auto packet = vr::render::MakeScenePacketFromViewRange(
        views.data(),
        static_cast<std::uint32_t>(views.size()),
        1U,
        895U,
        vr::render::RenderScenePacketKind::mixed);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    packet.flags = vr::render::render_scene_packet_allow_overlay_flag;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 23U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 4U);
    VR_CHECK(compiled.Passes()[1].debug_name == "overlay_pass");
    VR_CHECK(compiled.Passes()[1].executable);
    VR_REQUIRE(compiled.Passes()[1].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[1].execute));
    CheckSharedBindlessBindings(test_context_, compiled.Passes()[1]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_routes_single_surface_opaque_slice_to_scene_pass,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::surface::SurfaceRenderer3D surface_renderer{};
    surface_renderer.Initialize();
    recorder.RegisterOpaqueSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 778U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 14U);
    vr::render_graph::RenderGraphBuilder builder{};
    auto color_chain = vr::render_graph::invalid_resource_version;
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(
        builder,
        snapshot,
        [&recorder](vr::render_graph::RenderGraphBuilder& builder_ref_,
                    const vr::render_graph::FrameSnapshot3D& snapshot_ref_,
                    vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_ref_,
                    vr::render_graph::ResourceVersionHandle& color_chain_ref_) {
            recorder.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
        });
    const auto compiled = builder.Compile();

    VR_CHECK(build_result.built);
    VR_REQUIRE(compiled.Passes().size() >= 3U);
    VR_CHECK(compiled.Passes()[0].debug_name == "main_scene_pass");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_REQUIRE(compiled.Passes()[0].raster_pass.has_value());
    VR_CHECK(static_cast<bool>(compiled.Passes()[0].execute));
    CheckSurface3DGraphBindings(test_context_, compiled.Passes()[0]);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(SceneRecorder3D_build_render_graph_inserts_shadow_prepass_before_scene,
             "unit;core;render_graph;runtime") {
    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    vr::shadow::ShadowRenderer3D shadow_renderer{};
    shadow_renderer.Initialize();
    recorder.RegisterShadowRenderer(shadow_renderer);

    vr::ecs::Camera<vr::ecs::Dim3> world_camera{};
    world_camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    world_camera.runtime.revision = 1U;
    world_camera.runtime.culling_mask = 0x1U;
    auto world_view = vr::render::MakeRenderViewFromCamera(
        world_camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(world_view, 501U);
    packet.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    recorder.SetFramePacket(&packet);

    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 12U);
    vr::render_graph::RenderGraphBuilder builder{};
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    auto color_chain = vr::render_graph::invalid_resource_version;
    recorder.BuildRenderGraph(builder, snapshot, build_result, color_chain);
    const auto compiled = builder.Compile();

    VR_REQUIRE(compiled.ExecutionOrder().size() == 4U);
    VR_CHECK(compiled.Passes()[0].debug_name == "shadow_prepass");
    VR_CHECK(compiled.Passes()[0].executable);
    VR_CHECK(compiled.Passes()[1].debug_name == "main_scene_pass");
    VR_REQUIRE(compiled.Passes()[1].dependencies.size() == 1U);
    VR_CHECK(compiled.Passes()[1].dependencies[0].index == compiled.Passes()[0].handle.index);

    recorder.ClearFramePacket();
}

VR_TEST_CASE(RenderGraphBuilder_exports_dot_and_json,
             "unit;core;render_graph;debug") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 256U, .height = 256U, .depth = 1U},
        });
    const auto main_pass = builder.AddPass("main_scene_pass", false, vr::render_graph::QueueClass::graphics);
    const auto present_pass = builder.AddPass("present_to_swapchain", true, vr::render_graph::QueueClass::graphics);
    const auto color_version = builder.Write(
        main_pass,
        color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)builder.Read(
        present_pass,
        color_version,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
        });

    const auto compiled = builder.Compile();
    const std::string dot = compiled.BuildDotGraph();
    const std::string json = compiled.BuildJson();

    VR_CHECK(dot.find("digraph RenderGraph") != std::string::npos);
    VR_CHECK(dot.find("main_scene_pass") != std::string::npos);
    VR_CHECK(dot.find("present_to_swapchain") != std::string::npos);
    VR_CHECK(dot.find("color_attachment_write") != std::string::npos);
    VR_CHECK(json.find("\"passes\"") != std::string::npos);
    VR_CHECK(json.find("\"livenessRanges\"") != std::string::npos);
    VR_CHECK(json.find("scene_color#v1") != std::string::npos);
    VR_CHECK(json.find("\"queue\": \"graphics\"") != std::string::npos);
    VR_CHECK(json.find("\"access\": \"shader_sample_read\"") != std::string::npos);
}

VR_TEST_CASE(RenderGraphBuilder_builds_canonical_topology_view_for_pass_resource_and_queue_contract,
             "unit;core;render_graph;debug;topology") {
    static_assert(std::is_trivially_copyable_v<
                  vr::render_graph::RenderGraphQueueBatchTopologyId>);
    static_assert(std::is_trivially_copyable_v<
                  vr::render_graph::RenderGraphQueueDependencyTopologyId>);
    static_assert(std::is_trivially_copyable_v<
                  vr::render_graph::RenderGraphBarrierBatchTopologyId>);
    static_assert(std::is_trivially_copyable_v<
                  vr::render_graph::RenderGraphNativePassGroupTopologyId>);
    static_assert(std::is_trivially_copyable_v<
                  vr::render_graph::RenderGraphNativePassBoundaryTopologyId>);

    vr::render_graph::RenderGraphBuilder builder{};
    const auto payload = builder.CreateBuffer(
        "payload",
        vr::render_graph::BufferDesc{
            .size_bytes = 256U,
            .usage = vr::render_graph::buffer_usage_storage_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 256U, .height = 256U, .depth = 1U},
        });

    const auto upload = builder.AddPass("upload_payload",
                                        false,
                                        vr::render_graph::QueueClass::transfer);
    const auto simulate = builder.AddPass("simulate_payload",
                                          false,
                                          vr::render_graph::QueueClass::compute);
    const auto shade = builder.AddPass("shade_scene",
                                       true,
                                       vr::render_graph::QueueClass::graphics);

    const auto uploaded = builder.Write(
        upload,
        payload,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::transfer_write,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    (void)builder.Read(
        simulate,
        uploaded,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_read,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    const auto simulated = builder.Write(
        simulate,
        uploaded,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_write,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    (void)builder.Read(
        shade,
        simulated,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::uniform_read,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    (void)builder.Write(
        shade,
        scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
            .subresource_range = {
                .base_mip_level = 0U,
                .level_count = 1U,
                .base_array_layer = 0U,
                .layer_count = 1U,
            },
        });
    builder.SetRasterPassDesc(
        shade,
        vr::render_graph::RasterPassDesc{
            .color_attachments = {
                vr::render_graph::RasterColorAttachmentDesc{
                    .target = scene_color,
                    .load_op = vr::render_graph::AttachmentLoadOp::load,
                    .store_op = vr::render_graph::AttachmentStoreOp::store,
                },
            },
        });

    const auto compiled = builder.Compile();
    const auto topology_view =
        vr::render_graph::BuildCompiledRenderGraphTopologyView(compiled);
    const std::string topology_json =
        vr::render_graph::BuildCompiledRenderGraphTopologyJson(topology_view);
    const std::string compiled_json = compiled.BuildJson();

    if (!vr::render_graph::CompiledRenderGraphObservabilityAvailableInBuild()) {
        VR_CHECK(topology_json.empty());
        VR_CHECK(compiled_json.empty());
        return;
    }

    VR_CHECK(topology_view.summary.execution_order_count ==
             static_cast<std::uint32_t>(compiled.ExecutionOrder().size()));
    VR_CHECK(topology_view.summary.pass_count ==
             static_cast<std::uint32_t>(compiled.Passes().size()));
    VR_CHECK(topology_view.summary.executable_pass_count ==
             std::count_if(compiled.Passes().begin(),
                           compiled.Passes().end(),
                           [](const vr::render_graph::CompiledPass& pass_) {
                               return pass_.executable;
                           }));
    VR_CHECK(topology_view.summary.culled_pass_count ==
             topology_view.summary.pass_count -
                 topology_view.summary.executable_pass_count);
    VR_CHECK(topology_view.summary.resource_count ==
             static_cast<std::uint32_t>(compiled.Resources().size()));
    VR_CHECK(topology_view.summary.queue_batch_count ==
             static_cast<std::uint32_t>(
                 compiled.PlannedBarriers().queue_batches.size()));
    VR_CHECK(topology_view.summary.queue_dependency_count ==
             static_cast<std::uint32_t>(
                 compiled.PlannedBarriers().queue_dependencies.size()));
    VR_CHECK(topology_view.summary.barrier_batch_count ==
             static_cast<std::uint32_t>(
                 compiled.PlannedBarriers().barrier_batches.size()));
    VR_CHECK(topology_view.summary.native_pass_group_count ==
             static_cast<std::uint32_t>(compiled.NativePasses().groups.size()));
    VR_CHECK(topology_view.transient_memory.summary.logical_total_bytes ==
             compiled.TransientAllocations().timeline.logical_total_bytes);
    VR_CHECK(topology_view.transient_memory.summary.physical_total_bytes ==
             compiled.TransientAllocations().timeline.physical_total_bytes);
    VR_CHECK(topology_view.transient_memory.summary.peak_live_bytes ==
             compiled.TransientAllocations().timeline.peak_live_bytes);
    VR_CHECK(topology_view.transient_memory.summary.saved_bytes ==
             compiled.TransientAllocations().timeline.saved_bytes);
    VR_CHECK(topology_view.transient_memory.summary.page_count ==
             compiled.TransientAllocations().timeline.page_count);
    VR_CHECK(topology_view.transient_memory.summary.alias_barrier_count ==
             static_cast<std::uint32_t>(
                 compiled.TransientAllocations().alias_barriers.size()));
    VR_REQUIRE(topology_view.passes.size() == compiled.Passes().size());
    VR_REQUIRE(topology_view.queue_batches.size() ==
               compiled.PlannedBarriers().queue_batches.size());
    VR_REQUIRE(topology_view.queue_dependencies.size() ==
               compiled.PlannedBarriers().queue_dependencies.size());
    VR_REQUIRE(topology_view.barrier_batches.size() ==
               compiled.PlannedBarriers().barrier_batches.size());
    VR_REQUIRE(topology_view.transient_memory.records.size() ==
               compiled.TransientAllocations().records.size());
    VR_REQUIRE(topology_view.transient_memory.pages.size() ==
               compiled.TransientAllocations().pages.size());
    VR_REQUIRE(topology_view.transient_memory.timeline_samples.size() ==
               compiled.TransientAllocations().timeline.samples.size());
    VR_REQUIRE(topology_view.transient_memory.alias_candidates.size() ==
               compiled.TransientAllocations().alias_candidates.size());
    VR_REQUIRE(topology_view.transient_memory.alias_barriers.size() ==
               compiled.TransientAllocations().alias_barriers.size());

    const auto compiled_upload_it = std::find_if(
        compiled.Passes().begin(),
        compiled.Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.queue == vr::render_graph::QueueClass::transfer;
        });
    const auto compiled_compute_it = std::find_if(
        compiled.Passes().begin(),
        compiled.Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.queue == vr::render_graph::QueueClass::compute;
        });
    const auto compiled_graphics_it = std::find_if(
        compiled.Passes().begin(),
        compiled.Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.queue == vr::render_graph::QueueClass::graphics &&
                   pass_.raster_pass.has_value();
        });
    VR_REQUIRE(compiled_upload_it != compiled.Passes().end());
    VR_REQUIRE(compiled_compute_it != compiled.Passes().end());
    VR_REQUIRE(compiled_graphics_it != compiled.Passes().end());

    const auto pass_it = std::find_if(
        topology_view.passes.begin(),
        topology_view.passes.end(),
        [&](const vr::render_graph::CompiledRenderGraphPassTopologyView& pass_) {
            return pass_.pass.index == compiled_compute_it->handle.index;
        });
    VR_REQUIRE(pass_it != topology_view.passes.end());
    VR_CHECK(pass_it->debug_name == compiled_compute_it->debug_name);
    VR_CHECK(pass_it->queue == vr::render_graph::QueueClass::compute);
    VR_REQUIRE(pass_it->reads.size() == 1U);
    VR_REQUIRE(pass_it->writes.size() == 1U);
    VR_CHECK(pass_it->reads[0].resource.kind ==
             vr::render_graph::ResourceKind::buffer);
    VR_CHECK(pass_it->reads[0].access ==
             vr::render_graph::AccessKind::shader_storage_read);
    VR_CHECK(pass_it->writes[0].access ==
             vr::render_graph::AccessKind::shader_storage_write);

    VR_CHECK(std::any_of(
        topology_view.queue_batches.begin(),
        topology_view.queue_batches.end(),
        [&](const vr::render_graph::RenderGraphQueueBatchTopologyView& batch_) {
            return batch_.queue == vr::render_graph::QueueClass::transfer &&
                   std::any_of(
                       batch_.pass_ids.begin(),
                       batch_.pass_ids.end(),
                       [&](const vr::render_graph::PassHandle pass_handle_) {
                           return pass_handle_.index ==
                                  compiled_upload_it->handle.index;
                       });
        }));
    VR_CHECK(std::any_of(
        topology_view.queue_batches.begin(),
        topology_view.queue_batches.end(),
        [&](const vr::render_graph::RenderGraphQueueBatchTopologyView& batch_) {
            return batch_.queue == vr::render_graph::QueueClass::compute &&
                   std::any_of(
                       batch_.pass_ids.begin(),
                       batch_.pass_ids.end(),
                       [&](const vr::render_graph::PassHandle pass_handle_) {
                           return pass_handle_.index ==
                                  compiled_compute_it->handle.index;
                       });
        }));
    VR_CHECK(std::any_of(
        topology_view.queue_batches.begin(),
        topology_view.queue_batches.end(),
        [&](const vr::render_graph::RenderGraphQueueBatchTopologyView& batch_) {
            return batch_.queue == vr::render_graph::QueueClass::graphics &&
                   std::any_of(
                       batch_.pass_ids.begin(),
                       batch_.pass_ids.end(),
                       [&](const vr::render_graph::PassHandle pass_handle_) {
                           return pass_handle_.index ==
                                  compiled_graphics_it->handle.index;
                       });
        }));
    VR_CHECK(topology_json.find("\"transientMemory\"") != std::string::npos);
    VR_CHECK(topology_json.find("\"timelineSamples\"") != std::string::npos);
    VR_CHECK(compiled_json.find("\"transientMemory\"") != std::string::npos);
    VR_CHECK(std::any_of(
        topology_view.queue_dependencies.begin(),
        topology_view.queue_dependencies.end(),
        [&](const vr::render_graph::RenderGraphQueueDependencyTopologyView&
               dependency_) {
            return dependency_.source_pass.index ==
                       compiled_upload_it->handle.index &&
                   dependency_.target_pass.index ==
                       compiled_compute_it->handle.index;
        }));
    VR_CHECK(std::any_of(
        topology_view.queue_dependencies.begin(),
        topology_view.queue_dependencies.end(),
        [&](const vr::render_graph::RenderGraphQueueDependencyTopologyView&
               dependency_) {
            return dependency_.source_pass.index ==
                       compiled_compute_it->handle.index &&
                   dependency_.target_pass.index ==
                       compiled_graphics_it->handle.index;
        }));
    VR_CHECK(std::any_of(
        topology_view.native_pass_groups.begin(),
        topology_view.native_pass_groups.end(),
        [&](const vr::render_graph::RenderGraphNativePassGroupTopologyView&
               group_) {
            return std::any_of(
                group_.pass_ids.begin(),
                group_.pass_ids.end(),
                [&](const vr::render_graph::PassHandle pass_handle_) {
                    return pass_handle_.index ==
                           compiled_graphics_it->handle.index;
                });
        }));
    VR_CHECK(topology_json.find("\"queueBatchId\": 0") != std::string::npos);
    VR_CHECK(topology_json.find("\"sourcePassId\": " +
                                std::to_string(
                                    compiled_upload_it->handle.index)) !=
             std::string::npos);
    VR_CHECK(topology_json.find("\"nativePassTopology\"") != std::string::npos);
    const auto count_occurrences = [](std::string_view text_,
                                      std::string_view needle_) {
        std::size_t count = 0U;
        std::size_t position = 0U;
        while ((position = text_.find(needle_, position)) !=
               std::string_view::npos) {
            ++count;
            position += needle_.size();
        }
        return count;
    };
    const std::string compiled_topology_fragment =
        std::string{"\"topology\": "} + topology_json;
    VR_CHECK(compiled_json.find("\"topology\": {") != std::string::npos);
    VR_CHECK(compiled_json.find(compiled_topology_fragment) !=
             std::string::npos);
    VR_CHECK(count_occurrences(compiled_json, "\"topologySummary\"") == 1U);
    VR_CHECK(count_occurrences(compiled_json, "\"nativePassTopology\"") ==
             1U);
}

} // namespace
