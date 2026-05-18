#include "support/test_framework.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/render_graph_executor.hpp"
#include "vr/render_graph/vulkan_barrier_plan.hpp"
#include "vr/render_graph/vulkan_resource_table.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Host = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using RenderGraphRuntimeService = vr::runtime::services::RenderGraphRuntimeService;

[[nodiscard]] std::string ToLower(std::string_view value_) {
    std::string lowered{};
    lowered.reserve(value_.size());
    for (const unsigned char ch : value_) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view text_,
                                           std::string_view needle_) {
    const std::string lowered_text = ToLower(text_);
    const std::string lowered_needle = ToLower(needle_);
    return lowered_text.find(lowered_needle) != std::string::npos;
}

[[nodiscard]] bool IsEnvironmentSkipError(std::string_view message_) {
    constexpr std::array<std::string_view, 15U> patterns{
        "sdl_initsubsystem",
        "sdl_createwindow",
        "sdl_vulkan_getinstanceextensions",
        "sdl_vulkan_createsurface",
        "vkcreateinstance",
        "vkenumeratephysicaldevices",
        "no vulkan physical devices found",
        "no suitable vulkan physical device found",
        "missing required instance extension",
        "vkcreatedevice",
        "vkgetphysicaldevicesurfacesupportkhr",
        "vkgetphysicaldevicesurfaceformatskhr",
        "vkgetphysicaldevicesurfacepresentmodeskhr",
        "dynamicrendering",
        "synchronization2"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsValidationUnavailable(std::string_view message_) {
    constexpr std::array<std::string_view, 4U> patterns{
        "validation layer",
        "vk_layer_khronos_validation",
        "requested layer",
        "validation",
    };
    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Host::CreateInfo MakeMinimalRenderTargetRuntimeCreateInfo(
    const bool enable_validation_ = false) {
    Host::CreateInfo create_info{};
    create_info.platform.instance.enable_validation = enable_validation_;
    create_info.modules.enable_texture_host = false;
    create_info.modules.enable_frame_composer_host = false;
    create_info.modules.enable_ibl_host = false;
    create_info.modules.enable_ibl_bake_host = false;
    create_info.modules.enable_sky_environment_gpu_host = false;
    create_info.modules.enable_upload_host = false;
    create_info.modules.enable_descriptor_host = false;
    create_info.modules.enable_pipeline_host = false;
    create_info.modules.enable_render_target_host = true;
    create_info.modules.enable_render_target_pool = false;
    create_info.modules.enable_sampler_host = false;
    create_info.modules.enable_freetype_host = false;
    create_info.modules.enable_glyph_atlas_host = false;
    create_info.modules.enable_glyph_upload_host = false;
    create_info.modules.enable_particle_upload_host = false;
    create_info.modules.enable_particle_simulation_host = false;
    return create_info;
}

[[nodiscard]] const vr::render_graph::CompiledResource* FindResourceByName(
    const vr::render_graph::CompiledRenderGraph& compiled_graph_,
    std::string_view debug_name_) {
    for (const auto& resource_ : compiled_graph_.Resources()) {
        if (resource_.debug_name == debug_name_) {
            return &resource_;
        }
    }
    return nullptr;
}

VR_TEST_CASE(RenderGraphVulkanBackend_builds_vulkan_create_infos_from_graph_descs,
             "unit;core;render_graph;vulkan") {
    const vr::render_graph::TextureDesc texture_desc{
        .dimension = vr::render_graph::TextureDimension::image_2d,
        .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
        .extent = {.width = 320U, .height = 180U, .depth = 1U},
        .usage = vr::render_graph::texture_usage_sampled_flag |
                 vr::render_graph::texture_usage_color_attachment_flag |
                 vr::render_graph::texture_usage_transfer_dst_flag,
        .mip_level_count = 1U,
        .array_layer_count = 1U,
        .sample_count = vr::render_graph::SampleCount::x1,
    };
    const auto target_desc = vr::render_graph::VulkanResourceTable::BuildRenderTargetDesc(texture_desc);
    const auto image_create_info = vr::render_graph::VulkanResourceTable::BuildImageCreateInfo(texture_desc);

    VR_CHECK(target_desc.width == 320U);
    VR_CHECK(target_desc.height == 180U);
    VR_CHECK(target_desc.format == VK_FORMAT_R16G16B16A16_SFLOAT);
    VR_CHECK((target_desc.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0U);
    VR_CHECK((target_desc.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0U);
    VR_CHECK((target_desc.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0U);
    VR_CHECK(target_desc.aspect == VK_IMAGE_ASPECT_COLOR_BIT);

    VR_CHECK(image_create_info.extent.width == 320U);
    VR_CHECK(image_create_info.extent.height == 180U);
    VR_CHECK(image_create_info.format == VK_FORMAT_R16G16B16A16_SFLOAT);
    VR_CHECK((image_create_info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0U);
    VR_CHECK((image_create_info.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0U);
    VR_CHECK(image_create_info.default_view_aspect == VK_IMAGE_ASPECT_COLOR_BIT);

    const vr::render_graph::BufferDesc buffer_desc{
        .size_bytes = 4096U,
        .usage = vr::render_graph::buffer_usage_storage_flag |
                 vr::render_graph::buffer_usage_transfer_dst_flag,
        .host_visible = true,
        .persistently_mapped = true,
    };
    const auto buffer_create_info = vr::render_graph::VulkanResourceTable::BuildBufferCreateInfo(buffer_desc);

    VR_CHECK(buffer_create_info.size == 4096U);
    VR_CHECK((buffer_create_info.usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0U);
    VR_CHECK((buffer_create_info.usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0U);
    VR_CHECK((buffer_create_info.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U);
    VR_CHECK(buffer_create_info.persistently_mapped);
}

VR_TEST_CASE(RenderGraphVulkanBackend_maps_access_kinds_to_sync2_metadata,
             "unit;core;render_graph;vulkan") {
    const vr::render_graph::CompiledResource color_resource{
        .handle = {.index = 0U, .generation = 1U},
        .debug_name = "scene_color",
        .kind = vr::render_graph::ResourceKind::texture,
        .lifetime = vr::render_graph::ResourceLifetime::transient,
        .texture = {
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 64U, .height = 64U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        },
    };
    const auto color_write = vr::render_graph::DescribeVulkanAccess(
        color_resource,
        vr::render_graph::AccessKind::color_attachment_write);
    const auto sampled_read = vr::render_graph::DescribeVulkanAccess(
        color_resource,
        vr::render_graph::AccessKind::shader_sample_read);

    VR_CHECK(color_write.image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VR_CHECK((color_write.stage_mask & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) != 0U);
    VR_CHECK((color_write.access_mask & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) != 0U);
    VR_CHECK(sampled_read.image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VR_CHECK((sampled_read.access_mask & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) != 0U);

    const vr::render_graph::CompiledResource depth_resource{
        .handle = {.index = 1U, .generation = 1U},
        .debug_name = "scene_depth",
        .kind = vr::render_graph::ResourceKind::texture,
        .lifetime = vr::render_graph::ResourceLifetime::transient,
        .texture = {
            .format = vr::render_graph::TextureFormat::d32_sfloat,
            .extent = {.width = 64U, .height = 64U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_depth_stencil_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        },
    };
    const auto depth_sampled = vr::render_graph::DescribeVulkanAccess(
        depth_resource,
        vr::render_graph::AccessKind::shader_sample_read);
    VR_CHECK(depth_sampled.image_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    const vr::render_graph::CompiledResource buffer_resource{
        .handle = {.index = 2U, .generation = 1U},
        .debug_name = "scene_constants",
        .kind = vr::render_graph::ResourceKind::buffer,
        .lifetime = vr::render_graph::ResourceLifetime::persistent,
        .buffer = {
            .size_bytes = 1024U,
            .usage = vr::render_graph::buffer_usage_uniform_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        },
    };
    const auto transfer_write = vr::render_graph::DescribeVulkanAccess(
        buffer_resource,
        vr::render_graph::AccessKind::transfer_write);
    const auto uniform_read = vr::render_graph::DescribeVulkanAccess(
        buffer_resource,
        vr::render_graph::AccessKind::uniform_read);
    VR_CHECK((transfer_write.stage_mask & VK_PIPELINE_STAGE_2_TRANSFER_BIT) != 0U);
    VR_CHECK((transfer_write.access_mask & VK_ACCESS_2_TRANSFER_WRITE_BIT) != 0U);
    VR_CHECK((uniform_read.access_mask & VK_ACCESS_2_UNIFORM_READ_BIT) != 0U);
}

VR_TEST_CASE(RenderGraphVulkanBackend_lowers_logical_barriers_and_exports_dump,
             "unit;core;render_graph;vulkan") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_constants = builder.CreateBuffer(
        "scene_constants",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_uniform_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 128U, .height = 72U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        });
    const auto present_target = builder.CreateTexture(
        "present_target",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::unknown,
            .extent = {.width = 128U, .height = 72U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_present_flag,
        },
        vr::render_graph::ResourceLifetime::imported);

    const auto upload = builder.AddPass("upload_constants", false, vr::render_graph::QueueClass::transfer);
    const auto shade = builder.AddPass("main_scene_pass", false, vr::render_graph::QueueClass::graphics);
    const auto present = builder.AddPass("present_to_swapchain", true, vr::render_graph::QueueClass::graphics);

    const auto uploaded = builder.Write(
        upload,
        scene_constants,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::transfer_write,
            .buffer_range = {.offset_bytes = 64U, .size_bytes = 512U},
        });
    (void)builder.Read(
        shade,
        uploaded,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::uniform_read,
            .buffer_range = {.offset_bytes = 64U, .size_bytes = 512U},
        });
    const auto shaded = builder.Write(
        shade,
        scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
            .subresource_range = {.base_mip_level = 0U, .level_count = 1U, .base_array_layer = 0U, .layer_count = 1U},
        });
    (void)builder.Read(
        present,
        shaded,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
            .subresource_range = {.base_mip_level = 0U, .level_count = 1U, .base_array_layer = 0U, .layer_count = 1U},
        });
    (void)builder.Write(
        present,
        present_target,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::present});

    const auto compiled = builder.Compile();

    vr::QueueFamilyIndices queue_families{};
    queue_families.graphics = 2U;
    queue_families.compute = 5U;
    queue_families.transfer = 7U;
    const auto lowered = vr::render_graph::LowerToVulkanBarrierPlan(compiled, queue_families);
    const std::string logical_json = compiled.PlannedBarriers().BuildJson();
    const std::string lowered_json = lowered.BuildJson();
    const std::string compiled_json = compiled.BuildJson();

    VR_REQUIRE(lowered.barrier_batches.size() == 2U);
    VR_REQUIRE(lowered.barrier_batches[0].barriers.size() == 1U);
    VR_REQUIRE(lowered.barrier_batches[1].barriers.size() == 1U);

    const auto& transfer_barrier = lowered.barrier_batches[0].barriers[0];
    VR_CHECK(transfer_barrier.queue_transfer);
    VR_CHECK(transfer_barrier.src_queue_family_index == 7U);
    VR_CHECK(transfer_barrier.dst_queue_family_index == 2U);
    VR_CHECK((transfer_barrier.src_stage_mask & VK_PIPELINE_STAGE_2_TRANSFER_BIT) != 0U);
    VR_CHECK((transfer_barrier.dst_access_mask & VK_ACCESS_2_UNIFORM_READ_BIT) != 0U);

    const auto& image_barrier = lowered.barrier_batches[1].barriers[0];
    VR_CHECK(!image_barrier.queue_transfer);
    VR_CHECK(image_barrier.old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VR_CHECK(image_barrier.new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VR_CHECK(image_barrier.src_queue_family_index == VK_QUEUE_FAMILY_IGNORED);
    VR_CHECK(image_barrier.dst_queue_family_index == VK_QUEUE_FAMILY_IGNORED);

    VR_CHECK(logical_json.find("\"queueTransfer\": true") != std::string::npos);
    VR_CHECK(logical_json.find("\"barrierBatches\"") != std::string::npos);
    VR_CHECK(lowered_json.find("\"oldLayout\": \"color_attachment_optimal\"") != std::string::npos);
    VR_CHECK(lowered_json.find("\"newLayout\": \"shader_read_only_optimal\"") != std::string::npos);
    VR_CHECK(lowered_json.find("\"srcQueueFamilyIndex\": 7") != std::string::npos);
    VR_CHECK(compiled_json.find("\"barrierPlan\"") != std::string::npos);
}

VR_TEST_CASE(RenderGraphVulkanBackend_builds_command_ready_barrier_batches_from_physical_resources,
             "integration;render_graph;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    host.EnsureSwapchainTargetsForFrame(0U, 0U);
    const auto imported_present = host.SwapchainTargets().Get(0U);
    VR_REQUIRE(vr::render::IsValidRenderTargetHandle(imported_present));

    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_constants = builder.CreateBuffer(
        "scene_constants",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_uniform_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 128U, .height = 72U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        });
    const auto present_target = builder.CreateTexture(
        "present_target",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::unknown,
            .extent = {.width = 128U, .height = 72U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_present_flag,
        },
        vr::render_graph::ResourceLifetime::imported);

    const auto upload = builder.AddPass("upload_constants", false, vr::render_graph::QueueClass::transfer);
    const auto shade = builder.AddPass("main_scene_pass", false, vr::render_graph::QueueClass::graphics);
    const auto present = builder.AddPass("present_to_swapchain", true, vr::render_graph::QueueClass::graphics);

    const auto uploaded = builder.Write(
        upload,
        scene_constants,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::transfer_write,
            .buffer_range = {.offset_bytes = 64U, .size_bytes = 512U},
        });
    const auto shaded = builder.Write(
        shade,
        scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
            .subresource_range = {.base_mip_level = 0U, .level_count = 1U, .base_array_layer = 0U, .layer_count = 1U},
        });
    (void)builder.Read(
        shade,
        uploaded,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::uniform_read,
            .buffer_range = {.offset_bytes = 64U, .size_bytes = 512U},
        });
    (void)builder.Read(
        present,
        shaded,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
            .subresource_range = {.base_mip_level = 0U, .level_count = 1U, .base_array_layer = 0U, .layer_count = 1U},
        });
    (void)builder.Write(
        present,
        present_target,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::present});

    const auto compiled = builder.Compile();
    vr::QueueFamilyIndices queue_families{};
    queue_families.graphics = 2U;
    queue_families.compute = 5U;
    queue_families.transfer = 7U;
    const auto lowered = vr::render_graph::LowerToVulkanBarrierPlan(compiled, queue_families);

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.RegisterImportedTexture(present_target, imported_present);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);

    const auto command_ready = vr::render_graph::BuildCommandReadyVulkanBarrierPlan(
        lowered,
        table,
        host.RenderTarget());
    const std::string command_json = command_ready.BuildJson();

    VR_REQUIRE(command_ready.command_batches.size() == 1U);
    VR_REQUIRE(command_ready.queue_transfer_batches.size() == 1U);
    const auto command_dependency = command_ready.command_batches[0].dependency.BuildVkDependencyInfo();
    VR_CHECK(command_dependency.imageMemoryBarrierCount == 1U);
    VR_CHECK(command_dependency.bufferMemoryBarrierCount == 0U);
    const auto& image_barrier = command_ready.command_batches[0].dependency.image_barriers[0];
    VR_CHECK(image_barrier.image != VK_NULL_HANDLE);
    VR_CHECK(image_barrier.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VR_CHECK(image_barrier.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const auto& transfer_batch = command_ready.queue_transfer_batches[0];
    const auto release_dependency = transfer_batch.release_dependency.BuildVkDependencyInfo();
    const auto acquire_dependency = transfer_batch.acquire_dependency.BuildVkDependencyInfo();
    VR_CHECK(release_dependency.bufferMemoryBarrierCount == 1U);
    VR_CHECK(acquire_dependency.bufferMemoryBarrierCount == 1U);
    VR_REQUIRE(!transfer_batch.release_dependency.buffer_barriers.empty());
    VR_REQUIRE(!transfer_batch.acquire_dependency.buffer_barriers.empty());
    VR_CHECK(transfer_batch.release_dependency.buffer_barriers[0].buffer != VK_NULL_HANDLE);
    VR_CHECK(transfer_batch.release_dependency.buffer_barriers[0].srcQueueFamilyIndex == 7U);
    VR_CHECK(transfer_batch.release_dependency.buffer_barriers[0].dstQueueFamilyIndex == 2U);
    VR_CHECK((transfer_batch.release_dependency.buffer_barriers[0].srcAccessMask & VK_ACCESS_2_TRANSFER_WRITE_BIT) != 0U);
    VR_CHECK((transfer_batch.acquire_dependency.buffer_barriers[0].dstAccessMask & VK_ACCESS_2_UNIFORM_READ_BIT) != 0U);
    VR_CHECK(command_json.find("\"queueTransferBatches\"") != std::string::npos);
    VR_CHECK(command_json.find("\"bufferBarrierCount\": 1") != std::string::npos);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphExecutor_invokes_pass_execute_thunks,
             "integration;render_graph;executor;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    vr::render_graph::RenderGraphBuilder builder{};
    std::uint32_t executed_pass_count = 0U;
    const auto pass = builder.AddPass("execute_only_pass", true);
    builder.SetExecuteCallback(pass, [&](vr::render_graph::GraphCommandContext&) {
        executed_pass_count += 1U;
    });

    const auto compiled = builder.Compile();
    vr::QueueFamilyIndices queue_families{};
    queue_families.graphics = 2U;
    const auto lowered = vr::render_graph::LowerToVulkanBarrierPlan(compiled, queue_families);

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    const auto command_ready = vr::render_graph::BuildCommandReadyVulkanBarrierPlan(
        lowered,
        table,
        host.RenderTarget());

    const VkCommandBuffer command_buffer = host.Context().BeginSingleTimeCommands();
    const auto stats = vr::render_graph::RenderGraphExecutor::Record(
        vr::render_graph::GraphCommandContext{
            host.Context(),
            command_buffer,
            compiled,
            table,
            host.RenderTarget(),
            lowered,
            command_ready,
        });
    host.Context().EndSingleTimeCommands(command_buffer);

    VR_CHECK(executed_pass_count == 1U);
    VR_CHECK(stats.pass_count == 1U);
    VR_CHECK(stats.command_batch_count == 0U);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphExecutor_records_minimal_graph_barrier_batches,
             "integration;render_graph;executor;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
    if (host.Context().EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        VR_SKIP("synchronization2 feature unavailable");
    }

    vr::render_graph::RenderGraphBuilder builder{};
    const auto staging_buffer = builder.CreateBuffer(
        "staging_buffer",
        vr::render_graph::BufferDesc{
            .size_bytes = 1024U,
            .usage = vr::render_graph::buffer_usage_storage_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });
    const auto upload = builder.AddPass("upload_buffer", false, vr::render_graph::QueueClass::graphics);
    const auto consume = builder.AddPass("consume_buffer", true, vr::render_graph::QueueClass::graphics);
    const auto staged = builder.Write(
        upload,
        staging_buffer,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::transfer_write,
            .buffer_range = {.offset_bytes = 32U, .size_bytes = 256U},
        });
    (void)builder.Read(
        consume,
        staged,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::uniform_read,
            .buffer_range = {.offset_bytes = 32U, .size_bytes = 256U},
        });

    const auto compiled = builder.Compile();
    vr::QueueFamilyIndices queue_families{};
    queue_families.graphics = 2U;
    const auto lowered = vr::render_graph::LowerToVulkanBarrierPlan(compiled, queue_families);

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);
    const auto command_ready = vr::render_graph::BuildCommandReadyVulkanBarrierPlan(
        lowered,
        table,
        host.RenderTarget());

    const VkCommandBuffer command_buffer = host.Context().BeginSingleTimeCommands();
    const auto stats = vr::render_graph::RenderGraphExecutor::Record(
        vr::render_graph::GraphCommandContext{
            host.Context(),
            command_buffer,
            compiled,
            table,
            host.RenderTarget(),
            lowered,
            command_ready,
        });
    host.Context().EndSingleTimeCommands(command_buffer);

    VR_CHECK(stats.command_batch_count == 1U);
    VR_CHECK(stats.image_barrier_count == 0U);
    VR_CHECK(stats.buffer_barrier_count == 1U);
    VR_CHECK(stats.queue_transfer_batch_count == 0U);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphRuntimeService_builds_bloom_chain_from_scene_recorder_3d_in_pre_record,
             "integration;render_graph;runtime;postprocess;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    recorder.BindRuntime(host);

    struct GraphAwareRecorder final {
        vr::render::SceneRecorder3D& inner;
        const vr::render::RenderScenePacket3D* frame_packet = nullptr;

        void PrepareFrame(const vr::render::SceneRecorder3DPrepareView&) noexcept {}
        void Record(const vr::render::FrameRecordContext&) noexcept {}
        void BuildRenderGraph(vr::render_graph::RenderGraphBuilder& builder_,
                              const vr::render_graph::FrameSnapshot3D& snapshot_,
                              const vr::render_graph::MinimalFrameGraphBuildResult<vr::ecs::Dim3>& build_result_,
                              vr::render_graph::ResourceVersionHandle& color_chain_) {
            inner.BuildRenderGraph(builder_, snapshot_, build_result_, color_chain_);
        }
        [[nodiscard]] const vr::render::RenderScenePacket3D* FramePacket() const noexcept {
            return frame_packet;
        }
    } graph_recorder{.inner = recorder};

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 256.0F, .height = 144.0F};
    camera.runtime.revision = 1U;
    camera.runtime.culling_mask = 0xFFU;
    vr::render::RenderView3D main_view{};
    vr::render::RenderScenePacket3D main_scene_packet{};
    vr::ecs::Transform<vr::ecs::Dim3> camera_transform{};
    vr::ecs::TransformSystem<vr::ecs::Dim3>::Initialize(camera_transform);
    vr::ecs::TransformSystem<vr::ecs::Dim3>::SetLocalPosition(camera_transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.2F});
    vr::ecs::TransformSystem<vr::ecs::Dim3>::UpdateHierarchy(&camera_transform, 1U);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::Initialize(camera);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetAspectRatio(camera, 256.0F / 144.0F);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetNearFar(camera, 0.05F, 256.0F);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);
    vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                       main_scene_packet,
                                                       camera,
                                                       camera_transform,
                                                       host.Swapchain().Extent(),
                                                       1234U);
    recorder.SetFramePacket(&main_scene_packet);
    graph_recorder.frame_packet = &main_scene_packet;

    host.EnsureSwapchainTargetsForFrame(0U, 0U);
    host.PrepareTickFrame(graph_recorder, 0U);

    struct MockPhaseContext final {
        struct FrameContext final {
            vr::VulkanContext& device;
            Host::RuntimeServicesType& services;
            struct FrameInfo final {
                std::uint32_t frame_index = 0U;
                std::uint32_t image_index = 0U;
            } frame{};
            struct ProgressInfo final {
                std::uint64_t graphics_submitted = 0U;
                std::uint64_t graphics_completed = 0U;
            } progress{};
            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            vr::render::SwapchainTargetSet* swapchain_targets = nullptr;
        } frame_context;
    };

    auto& service = host.Services().Get<RenderGraphRuntimeService>();
    MockPhaseContext context{
        .frame_context = {
            .device = host.Context(),
            .services = host.Services(),
            .frame = {.frame_index = 0U, .image_index = 0U},
            .progress = {.graphics_submitted = 0U, .graphics_completed = 0U},
            .command_buffer = VK_NULL_HANDLE,
            .swapchain_targets = &host.SwapchainTargets(),
        },
    };
    service.PreRecord(context);

    const auto* compiled = service.TryGetCompiledGraph();
    VR_REQUIRE(compiled != nullptr);
    VR_CHECK(!compiled->Passes().empty());
    VR_CHECK(compiled->HasExecutablePasses());

    recorder.ClearFramePacket();
    host.Shutdown();
}

VR_TEST_CASE(RenderGraphRuntimeService_records_barrier_batches_when_execution_enabled,
             "integration;render_graph;executor;runtime;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    host.EnsureSwapchainTargetsForFrame(0U, 0U);

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 256.0F, .height = 144.0F};
    camera.runtime.revision = 1U;
    camera.runtime.culling_mask = 0xFFU;
    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(view, 99U);
    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 4U);

    struct MockPhaseContext final {
        struct FrameContext final {
            vr::VulkanContext& device;
            Host::RuntimeServicesType& services;
            struct FrameInfo final {
                std::uint32_t frame_index = 0U;
                std::uint32_t image_index = 0U;
            } frame{};
            struct ProgressInfo final {
                std::uint64_t graphics_submitted = 0U;
                std::uint64_t graphics_completed = 0U;
            } progress{};
            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            vr::render::SwapchainTargetSet* swapchain_targets = nullptr;
        } frame_context;
    };

    auto& service = host.Services().Get<RenderGraphRuntimeService>();
    service.EnableRecordExecution();
    service.EnableGraphOnlyRecordPath();
    const VkCommandBuffer command_buffer = host.Context().BeginSingleTimeCommands();
    MockPhaseContext context{
        .frame_context = {
            .device = host.Context(),
            .services = host.Services(),
            .frame = {.frame_index = 4U, .image_index = 0U},
            .progress = {.graphics_submitted = 0U, .graphics_completed = 0U},
            .command_buffer = command_buffer,
            .swapchain_targets = &host.SwapchainTargets(),
        },
    };

    service.BeginFrame(context);
    service.SetFrameSnapshot<vr::ecs::Dim3>(snapshot);
    service.PreRecord(context);
    service.Record(context);
    host.Context().EndSingleTimeCommands(command_buffer);

    const auto& stats = service.LastRecordStats();
    if (host.Context().EnabledVulkan13Features().synchronization2 == VK_TRUE &&
        host.Context().EnabledVulkan13Features().dynamicRendering == VK_TRUE) {
        VR_CHECK(stats.command_batch_count >= 1U);
        VR_CHECK(stats.image_barrier_count >= 1U);
        VR_CHECK(stats.pass_count >= 2U);
    } else {
        VR_CHECK(stats.command_batch_count == 0U);
        VR_CHECK(stats.image_barrier_count == 0U);
    }
    VR_CHECK(stats.queue_transfer_batch_count == 0U);
}

VR_TEST_CASE(RenderGraphRuntimeService_executes_minimal_graph_during_runtime_tick,
             "integration;render_graph;runtime;executor;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    struct TickRecorder final {
        const vr::render::RenderScenePacket3D* frame_packet = nullptr;
        std::uint32_t legacy_record_count = 0U;

        void PrepareFrame(const vr::render::SceneRecorder3DPrepareView&) noexcept {}
        void Record(const vr::render::FrameRecordContext&) noexcept {
            legacy_record_count += 1U;
        }
        [[nodiscard]] const vr::render::RenderScenePacket3D* FramePacket() const noexcept {
            return frame_packet;
        }
    } recorder{};

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 192.0F, .height = 108.0F};
    camera.runtime.revision = 1U;
    camera.runtime.culling_mask = 0xFFU;
    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(view, 909U);
    recorder.frame_packet = &packet;

    auto& service = host.Services().Get<RenderGraphRuntimeService>();
    service.EnableRecordExecution();
    service.EnableGraphOnlyRecordPath();

    const bool graph_execution_supported =
        host.Context().EnabledVulkan13Features().synchronization2 == VK_TRUE &&
        host.Context().EnabledVulkan13Features().dynamicRendering == VK_TRUE;
    if (!graph_execution_supported) {
        host.Shutdown();
        VR_SKIP("synchronization2 or dynamicRendering feature unavailable");
    }

    std::uint32_t submitted_frames = 0U;
    std::uint32_t executed_graph_frames = 0U;
    constexpr std::uint32_t max_ticks = 4U;
    for (std::uint32_t tick_index = 0U; tick_index < max_ticks && host.IsRunning(); ++tick_index) {
        const auto tick_result = host.Tick(recorder);
        if (tick_result.render.code == vr::render::TickCode::Submitted ||
            tick_result.render.code == vr::render::TickCode::RecreateRequested) {
            ++submitted_frames;
        }
        if (service.LastRecordStats().pass_count > 0U) {
            ++executed_graph_frames;
        }
        SDL_Delay(1U);
    }

    VR_CHECK(submitted_frames > 0U);
    if (graph_execution_supported) {
        VR_CHECK(executed_graph_frames > 0U);
        VR_CHECK(recorder.legacy_record_count == 0U);
    } else {
        VR_CHECK(executed_graph_frames == 0U);
        VR_CHECK(recorder.legacy_record_count > 0U);
    }

    host.Shutdown();
}

VR_TEST_CASE(RenderGraphRuntimeService_handles_swapchain_recreate_with_graph_execution,
             "integration;render_graph;runtime;present;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    const bool graph_execution_supported =
        host.Context().EnabledVulkan13Features().synchronization2 == VK_TRUE &&
        host.Context().EnabledVulkan13Features().dynamicRendering == VK_TRUE;
    if (!graph_execution_supported) {
        host.Shutdown();
        VR_SKIP("synchronization2 or dynamicRendering feature unavailable");
    }

    struct TickRecorder final {
        const vr::render::RenderScenePacket3D* frame_packet = nullptr;

        void PrepareFrame(const vr::render::SceneRecorder3DPrepareView&) noexcept {}
        void Record(const vr::render::FrameRecordContext&) noexcept {}
        [[nodiscard]] const vr::render::RenderScenePacket3D* FramePacket() const noexcept {
            return frame_packet;
        }
    } recorder{};

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 224.0F, .height = 126.0F};
    camera.runtime.revision = 1U;
    camera.runtime.culling_mask = 0xFFU;
    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(view, 1001U);
    recorder.frame_packet = &packet;

    auto& service = host.Services().Get<RenderGraphRuntimeService>();
    service.EnableRecordExecution();
    service.EnableGraphOnlyRecordPath();

    auto tick_result = host.Tick(recorder);
    VR_CHECK(tick_result.render.code == vr::render::TickCode::Submitted ||
             tick_result.render.code == vr::render::TickCode::RecreateRequested);

    host.Swapchain().MarkDirty();

    std::uint32_t submitted_after_resize = 0U;
    std::uint32_t executed_after_resize = 0U;
    constexpr std::uint32_t max_ticks = 4U;
    for (std::uint32_t tick_index = 0U; tick_index < max_ticks && host.IsRunning(); ++tick_index) {
        const auto one_tick = host.Tick(recorder);
        if (one_tick.render.code == vr::render::TickCode::Submitted ||
            one_tick.render.code == vr::render::TickCode::RecreateRequested) {
            ++submitted_after_resize;
        }
        if (service.LastRecordStats().pass_count > 0U) {
            ++executed_after_resize;
        }
        SDL_Delay(1U);
    }

    VR_CHECK(submitted_after_resize > 0U);
    VR_CHECK(executed_after_resize > 0U);

    host.Shutdown();
}

VR_TEST_CASE(RenderGraphRuntimeService_validation_enabled_minimal_tick_and_resize_stay_clean,
             "integration;render_graph;runtime;validation;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo(true));
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what()) || IsValidationUnavailable(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    const bool graph_execution_supported =
        host.Context().EnabledVulkan13Features().synchronization2 == VK_TRUE &&
        host.Context().EnabledVulkan13Features().dynamicRendering == VK_TRUE;
    if (!graph_execution_supported) {
        host.Shutdown();
        VR_SKIP("synchronization2 or dynamicRendering feature unavailable");
    }

    struct TickRecorder final {
        const vr::render::RenderScenePacket3D* frame_packet = nullptr;

        void PrepareFrame(const vr::render::SceneRecorder3DPrepareView&) noexcept {}
        void Record(const vr::render::FrameRecordContext&) noexcept {}
        [[nodiscard]] const vr::render::RenderScenePacket3D* FramePacket() const noexcept {
            return frame_packet;
        }
    } recorder{};

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 208.0F, .height = 117.0F};
    camera.runtime.revision = 2U;
    camera.runtime.culling_mask = 0xFFU;
    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(view, 1002U);
    recorder.frame_packet = &packet;

    auto& service = host.Services().Get<RenderGraphRuntimeService>();
    service.EnableRecordExecution();
    service.EnableGraphOnlyRecordPath();

    std::uint32_t submitted_frames = 0U;
    const auto first_tick = host.Tick(recorder);
    if (first_tick.render.code == vr::render::TickCode::Submitted ||
        first_tick.render.code == vr::render::TickCode::RecreateRequested) {
        ++submitted_frames;
    }

    host.Swapchain().MarkDirty();
    constexpr std::uint32_t max_ticks = 4U;
    for (std::uint32_t tick_index = 0U; tick_index < max_ticks && host.IsRunning(); ++tick_index) {
        const auto one_tick = host.Tick(recorder);
        if (one_tick.render.code == vr::render::TickCode::Submitted ||
            one_tick.render.code == vr::render::TickCode::RecreateRequested) {
            ++submitted_frames;
        }
        SDL_Delay(1U);
    }

    VR_CHECK(submitted_frames > 0U);
    VR_CHECK(service.LastRecordStats().pass_count > 0U);

    host.Shutdown();
}

VR_TEST_CASE(RenderGraphVulkanBackend_resolves_imported_and_owned_resources,
             "integration;render_graph;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    host.EnsureSwapchainTargetsForFrame(0U, 0U);
    const vr::render::RenderTargetHandle imported_present = host.SwapchainTargets().Get(0U);
    VR_REQUIRE(vr::render::IsValidRenderTargetHandle(imported_present));

    vr::render_graph::RenderGraphBuilder builder{};
    const auto present_target = builder.CreateTexture(
        "present_target",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::unknown,
            .extent = {.width = 128U, .height = 128U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_present_flag,
        },
        vr::render_graph::ResourceLifetime::imported);
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 128U, .height = 128U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        },
        vr::render_graph::ResourceLifetime::transient);
    const auto scene_depth = builder.CreateTexture(
        "scene_depth",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::d32_sfloat,
            .extent = {.width = 128U, .height = 128U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_depth_stencil_attachment_flag,
        },
        vr::render_graph::ResourceLifetime::transient);
    const auto scene_constants = builder.CreateBuffer(
        "scene_constants",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        },
        vr::render_graph::ResourceLifetime::persistent);

    const auto scene_pass = builder.AddPass("main_scene_pass");
    const auto present_pass = builder.AddPass("present_to_swapchain", true);
    const auto scene_color_v1 = builder.Write(scene_pass, scene_color);
    (void)builder.Write(scene_pass, scene_depth);
    (void)builder.Write(scene_pass, scene_constants);
    (void)builder.Read(present_pass, scene_color_v1);
    (void)builder.Write(present_pass, present_target);

    const auto compiled = builder.Compile();

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.RegisterImportedTexture(present_target, imported_present);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);

    const auto* imported_record = table.FindTexture(present_target);
    const auto* color_record = table.FindTexture(scene_color);
    const auto* depth_record = table.FindTexture(scene_depth);
    const auto* buffer_record = table.FindBuffer(scene_constants);

    VR_REQUIRE(imported_record != nullptr);
    VR_REQUIRE(color_record != nullptr);
    VR_REQUIRE(depth_record != nullptr);
    VR_REQUIRE(buffer_record != nullptr);
    VR_CHECK(imported_record->imported);
    VR_CHECK(imported_record->render_target.index == imported_present.index);
    VR_CHECK(vr::render::IsValidRenderTargetHandle(color_record->render_target));
    VR_CHECK(vr::render::IsValidRenderTargetHandle(depth_record->render_target));
    VR_CHECK(buffer_record->owned_resource.buffer != VK_NULL_HANDLE);
    VR_CHECK(buffer_record->owned_resource.size == 4096U);

    const auto* color_target = host.RenderTarget().Resolve(color_record->render_target);
    const auto* depth_target = host.RenderTarget().Resolve(depth_record->render_target);
    VR_REQUIRE(color_target != nullptr);
    VR_REQUIRE(depth_target != nullptr);
    VR_CHECK(color_target->format == VK_FORMAT_R16G16B16A16_SFLOAT);
    VR_CHECK(depth_target->format == VK_FORMAT_D32_SFLOAT);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
    VR_CHECK(table.Textures().empty());
    VR_CHECK(table.Buffers().empty());
}

VR_TEST_CASE(RenderGraphVulkanBackend_runtime_service_resolves_physical_resources_in_pre_record,
             "integration;render_graph;runtime;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    host.EnsureSwapchainTargetsForFrame(0U, 0U);

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 256.0F, .height = 144.0F};
    camera.runtime.revision = 1U;
    camera.runtime.culling_mask = 0xFFU;
    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(view, 77U);
    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 3U);

    struct MockPhaseContext final {
        struct FrameContext final {
            vr::VulkanContext& device;
            Host::RuntimeServicesType& services;
            struct FrameInfo final {
                std::uint32_t frame_index = 0U;
                std::uint32_t image_index = 0U;
            } frame{};
            struct ProgressInfo final {
                std::uint64_t graphics_submitted = 0U;
                std::uint64_t graphics_completed = 0U;
            } progress{};
            vr::render::SwapchainTargetSet* swapchain_targets = nullptr;
        } frame_context;
    };

    auto& service = host.Services().Get<RenderGraphRuntimeService>();
    MockPhaseContext context{
        .frame_context = {
            .device = host.Context(),
            .services = host.Services(),
            .frame = {.frame_index = 3U, .image_index = 0U},
            .progress = {.graphics_submitted = 0U, .graphics_completed = 0U},
            .swapchain_targets = &host.SwapchainTargets(),
        },
    };

    service.BeginFrame(context);
    service.SetFrameSnapshot<vr::ecs::Dim3>(snapshot);
    service.PreRecord(context);

    const auto* compiled = service.TryGetCompiledGraph();
    VR_REQUIRE(compiled != nullptr);

    const auto* present_resource = FindResourceByName(*compiled, "present_target");
    const auto* scene_color_resource = FindResourceByName(*compiled, "scene_color");
    const auto* scene_depth_resource = FindResourceByName(*compiled, "scene_depth");
    VR_REQUIRE(present_resource != nullptr);
    VR_REQUIRE(scene_color_resource != nullptr);
    VR_REQUIRE(scene_depth_resource != nullptr);

    const auto* present_physical = service.PhysicalResources().FindTexture(present_resource->handle);
    const auto* scene_color_physical = service.PhysicalResources().FindTexture(scene_color_resource->handle);
    const auto* scene_depth_physical = service.PhysicalResources().FindTexture(scene_depth_resource->handle);
    const auto& lowered_barriers = service.PlannedVulkanBarriers();
    const auto& command_ready_barriers = service.PlannedCommandReadyVulkanBarriers();
    const std::string lowered_json = lowered_barriers.BuildJson();
    const std::string command_ready_json = command_ready_barriers.BuildJson();

    VR_REQUIRE(present_physical != nullptr);
    VR_REQUIRE(scene_color_physical != nullptr);
    VR_REQUIRE(scene_depth_physical != nullptr);
    VR_CHECK(present_physical->imported);
    VR_CHECK(vr::render::IsValidRenderTargetHandle(present_physical->render_target));
    VR_CHECK(vr::render::IsValidRenderTargetHandle(scene_color_physical->render_target));
    VR_CHECK(vr::render::IsValidRenderTargetHandle(scene_depth_physical->render_target));
    VR_REQUIRE(!lowered_barriers.barrier_batches.empty());
    VR_REQUIRE(!command_ready_barriers.command_batches.empty());
    VR_CHECK(lowered_json.find("\"barrierBatches\"") != std::string::npos);
    VR_CHECK(lowered_json.find("\"oldLayout\": \"color_attachment_optimal\"") != std::string::npos);
    VR_CHECK(command_ready_json.find("\"commandBatches\"") != std::string::npos);
}

VR_TEST_CASE(RenderGraphVulkanBackend_builds_minimal_frame_graph_present_transition_batches,
             "integration;render_graph;present;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    host.EnsureSwapchainTargetsForFrame(0U, 0U);
    const auto imported_present = host.SwapchainTargets().Get(0U);
    VR_REQUIRE(vr::render::IsValidRenderTargetHandle(imported_present));

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 160.0F, .height = 90.0F};
    camera.runtime.revision = 1U;
    camera.runtime.culling_mask = 0xFFU;
    auto view = vr::render::MakeRenderViewFromCamera(
        camera,
        static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
        vr::render::RenderViewKind::world,
        0U);
    auto packet = vr::render::MakeSingleViewScenePacket(view, 808U);
    const auto snapshot = vr::render_graph::MakeFrameSnapshot(packet, 6U);

    vr::render_graph::RenderGraphBuilder builder{};
    const auto build_result = vr::render_graph::BuildMinimalFrameGraph(builder, snapshot);
    const auto compiled = builder.Compile();
    VR_CHECK(build_result.built);
    VR_CHECK(compiled.HasExecutablePasses());

    vr::QueueFamilyIndices queue_families{};
    queue_families.graphics = 2U;
    const auto lowered = vr::render_graph::LowerToVulkanBarrierPlan(compiled, queue_families);

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.RegisterImportedTexture(build_result.present_target, imported_present);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);
    const auto command_ready = vr::render_graph::BuildCommandReadyVulkanBarrierPlan(
        lowered,
        table,
        host.RenderTarget());
    const std::string command_json = command_ready.BuildJson();

    VR_REQUIRE(command_ready.command_batches.size() >= 2U);
    bool saw_present_transition = false;
    for (const auto& batch_ : command_ready.command_batches) {
        for (const auto& image_barrier : batch_.dependency.image_barriers) {
            if (image_barrier.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                saw_present_transition = true;
            }
        }
    }
    VR_CHECK(saw_present_transition);
    VR_CHECK(command_json.find("\"commandBatches\"") != std::string::npos);
    VR_CHECK(command_json.find("\"imageBarrierCount\": 1") != std::string::npos);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphVulkanBackend_resolves_imported_buffers,
             "integration;render_graph;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    const auto buffer_create_info = vr::render_graph::VulkanResourceTable::BuildBufferCreateInfo(
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_storage_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });
    auto imported_buffer = host.CreateBuffer(buffer_create_info);

    vr::render_graph::RenderGraphBuilder builder{};
    const auto imported_constants = builder.CreateBuffer(
        "imported_constants",
        vr::render_graph::BufferDesc{
            .size_bytes = 2048U,
            .usage = vr::render_graph::buffer_usage_storage_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        },
        vr::render_graph::ResourceLifetime::imported);
    const auto upload_pass = builder.AddPass("upload_constants", true);
    (void)builder.Write(upload_pass, imported_constants);
    const auto compiled = builder.Compile();

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.RegisterImportedBuffer(imported_constants,
                                 vr::render_graph::ImportedBufferBinding{
                                     .buffer = imported_buffer.buffer,
                                     .size_bytes = imported_buffer.size,
                                     .usage = imported_buffer.usage,
                                 });
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);

    const auto* imported_record = table.FindBuffer(imported_constants);
    VR_REQUIRE(imported_record != nullptr);
    VR_CHECK(imported_record->imported);
    VR_CHECK(imported_record->imported_buffer.buffer == imported_buffer.buffer);
    VR_CHECK(imported_record->imported_buffer.size_bytes == imported_buffer.size);
    VR_CHECK(imported_record->owned_resource.buffer == VK_NULL_HANDLE);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
    host.DestroyBuffer(imported_buffer);
}

VR_TEST_CASE(RenderGraphVulkanBackend_reuses_persistent_textures_across_resolves,
             "integration;render_graph;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    vr::render_graph::RenderGraphBuilder builder{};
    const auto history_color = builder.CreateTexture(
        "history_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
            .extent = {.width = 96U, .height = 64U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        },
        vr::render_graph::ResourceLifetime::persistent);
    const auto history_pass = builder.AddPass("history_update", true);
    (void)builder.Write(history_pass, history_color);
    const auto compiled = builder.Compile();

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);
    const auto* first_record = table.FindTexture(history_color);
    VR_REQUIRE(first_record != nullptr);
    const auto first_handle = first_record->render_target;

    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);
    const auto* second_record = table.FindTexture(history_color);
    VR_REQUIRE(second_record != nullptr);
    const auto second_handle = second_record->render_target;

    VR_CHECK(first_handle.index == second_handle.index);
    VR_CHECK(first_handle.generation == second_handle.generation);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphVulkanBackend_repeated_shutdown_clears_owned_resources,
             "integration;render_graph;vulkan") {
    Host host{};
    try {
        host.Initialize(MakeMinimalRenderTargetRuntimeCreateInfo());
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    vr::render_graph::RenderGraphBuilder builder{};
    const auto transient_color = builder.CreateTexture(
        "transient_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
            .extent = {.width = 80U, .height = 48U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag,
        },
        vr::render_graph::ResourceLifetime::transient);
    const auto persistent_buffer = builder.CreateBuffer(
        "persistent_buffer",
        vr::render_graph::BufferDesc{
            .size_bytes = 1024U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        },
        vr::render_graph::ResourceLifetime::persistent);
    const auto pass = builder.AddPass("write_resources", true);
    (void)builder.Write(pass, transient_color);
    (void)builder.Write(pass, persistent_buffer);
    const auto compiled = builder.Compile();

    vr::render_graph::VulkanResourceTable table{};
    for (std::uint32_t iteration = 0U; iteration < 3U; ++iteration) {
        table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
        table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);
        VR_CHECK(!table.Textures().empty());
        VR_CHECK(!table.Buffers().empty());

        table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
        host.RenderTarget().BeginFrame(host.Context(), 0U);
        VR_CHECK(table.Textures().empty());
        VR_CHECK(table.Buffers().empty());
        VR_CHECK(host.RenderTarget().Stats().target_count == 0U);
        VR_CHECK(host.RenderTarget().Stats().owned_target_count == 0U);
        VR_CHECK(host.RenderTarget().Stats().retired_target_count == 0U);
    }
}

} // namespace
