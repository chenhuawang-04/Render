#include "support/test_framework.hpp"
#include "vr/geometry/geometry_renderer_2d.hpp"
#include "vr/geometry/geometry_renderer_3d.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/render_target_composite_renderer.hpp"
#include "vr/render/scene_recorder_2d.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
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

#include <array>
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
    view.targets.color_final_state = vr::render::RenderTargetStateKind::present_src;
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
    VR_CHECK(snapshot.submission_id == 99U);
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
    VR_CHECK(active_view->targets.color_final_state == vr::render::RenderTargetStateKind::present_src);
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
    using Host = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;

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
    VR_CHECK(snapshot->submission_id == 333U);
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
    CheckSharedBindlessBindings(test_context_, compiled.Passes()[0]);

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
    CheckSharedBindlessBindings(test_context_, compiled.Passes()[0]);

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

} // namespace
