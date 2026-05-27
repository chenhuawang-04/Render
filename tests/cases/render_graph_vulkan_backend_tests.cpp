#include "support/test_framework.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/runtime/detail/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render_graph/compiled_render_graph_observability.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/queue_execution_policy.hpp"
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

namespace vr::render_graph {
void SetVulkanResourceTableResolveFailureBeforePublishForTesting(bool enabled_) noexcept;
}

namespace {

using Host = vr::runtime::detail::RuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using RenderGraphRuntimeService = vr::runtime::services::RenderGraphRuntimeService;

struct ResolveFailureInjectionScope final {
    explicit ResolveFailureInjectionScope(const bool enabled_) noexcept {
        vr::render_graph::SetVulkanResourceTableResolveFailureBeforePublishForTesting(enabled_);
    }

    ~ResolveFailureInjectionScope() {
        vr::render_graph::SetVulkanResourceTableResolveFailureBeforePublishForTesting(false);
    }

    ResolveFailureInjectionScope(const ResolveFailureInjectionScope&) = delete;
    ResolveFailureInjectionScope& operator=(const ResolveFailureInjectionScope&) = delete;
};

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

[[nodiscard]] bool HasEffectiveQueueBatchWithPass(
    const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_,
    const vr::render_graph::QueueClass queue_,
    std::string_view pass_name_) {
    return std::any_of(
        diagnostics_.effective_queue_batches.begin(),
        diagnostics_.effective_queue_batches.end(),
        [&](const vr::runtime::RenderGraphQueueBatchDiagnostics& batch_) {
            if (batch_.queue != queue_) {
                return false;
            }
            return std::any_of(
                batch_.pass_debug_names.begin(),
                batch_.pass_debug_names.end(),
                [&](const std::string& debug_name_) {
                    return debug_name_ == pass_name_;
                });
        });
}

[[nodiscard]] bool AllEffectiveQueueBatchesUseQueue(
    const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_,
    const vr::render_graph::QueueClass queue_) {
    return !diagnostics_.effective_queue_batches.empty() &&
           std::all_of(
               diagnostics_.effective_queue_batches.begin(),
               diagnostics_.effective_queue_batches.end(),
               [&](const vr::runtime::RenderGraphQueueBatchDiagnostics& batch_) {
                   return batch_.queue == queue_;
               });
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

[[nodiscard]] vr::render_graph::CompiledRenderGraph BuildCrossQueuePolicyGraph() {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto payload = builder.CreateBuffer(
        "cross_queue_payload",
        vr::render_graph::BufferDesc{
            .size_bytes = 256U,
            .usage = vr::render_graph::buffer_usage_storage_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });
    const auto upload = builder.AddPass("upload_payload",
                                        false,
                                        vr::render_graph::QueueClass::transfer);
    const auto compute = builder.AddPass("simulate_payload",
                                         false,
                                         vr::render_graph::QueueClass::compute);
    const auto consume = builder.AddPass("consume_payload",
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
        compute,
        uploaded,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_read,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    const auto simulated = builder.Write(
        compute,
        uploaded,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_write,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    (void)builder.Read(
        consume,
        simulated,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::uniform_read,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    return builder.Compile();
}

[[nodiscard]] bool HasDistinctTransferQueue(const vr::VulkanContext& device_) noexcept {
    return device_.TransferQueue() != VK_NULL_HANDLE &&
           device_.QueueFamilies().graphics.has_value() &&
           device_.QueueFamilies().transfer.has_value() &&
           device_.QueueFamilies().transfer.value() != device_.QueueFamilies().graphics.value();
}

[[nodiscard]] bool HasDistinctComputeQueue(const vr::VulkanContext& device_) noexcept {
    return device_.ComputeQueue() != VK_NULL_HANDLE &&
           device_.QueueFamilies().graphics.has_value() &&
           device_.QueueFamilies().compute.has_value() &&
           device_.QueueFamilies().compute.value() != device_.QueueFamilies().graphics.value();
}

[[nodiscard]] bool HasOwnedTransferSubmitQueue(const vr::VulkanContext& device_) noexcept {
    return device_.TransferQueue() != VK_NULL_HANDLE;
}

[[nodiscard]] bool HasOwnedComputeSubmitQueue(const vr::VulkanContext& device_) noexcept {
    return device_.ComputeQueue() != VK_NULL_HANDLE;
}

void EnableRenderGraphRuntimeExecutionFeatures(Host::CreateInfo& create_info_) noexcept {
    create_info_.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
    create_info_.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
    create_info_.render_loop.submit_wait_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    create_info_.diagnostics.level = vr::runtime::DiagnosticsLevel::CountersOnly;
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeGraphicsFallbackQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.transfer_queue_requested = true;
    diagnostics.compute_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.graphics_fallback_active = true;
    diagnostics.queue_fallback_reason = "distinct transfer/compute queues unavailable";
    diagnostics.effective_queue_batch_count = 1U;
    diagnostics.effective_graphics_queue_batch_count = 1U;
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 0U,
            .queue = vr::render_graph::QueueClass::graphics,
            .pass_ids = {0U, 1U},
            .pass_debug_names = {"upload_payload", "prepare_present_target"},
        });
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeTransferEnabledQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.transfer_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.transfer_queue_enabled = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.effective_queue_batch_count = 2U;
    diagnostics.effective_queue_dependency_count = 1U;
    diagnostics.effective_graphics_queue_batch_count = 1U;
    diagnostics.effective_transfer_queue_batch_count = 1U;
    diagnostics.effective_owned_submit_batch_count = 1U;
    diagnostics.effective_cross_queue_dependency_count = 1U;
    diagnostics.effective_total_submit_wait_count = 1U;
    diagnostics.effective_total_submit_signal_count = 1U;
    diagnostics.graphics_submit_wait_count = 1U;
    diagnostics.non_graphics_submit_batch_count = 1U;
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 0U,
            .queue = vr::render_graph::QueueClass::transfer,
            .pass_ids = {0U},
            .pass_debug_names = {"text_2d_upload_instances"},
            .signal_dependency_ids = {0U},
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 1U,
            .queue = vr::render_graph::QueueClass::graphics,
            .pass_ids = {1U},
            .pass_debug_names = {"overlay_pass"},
            .wait_dependency_ids = {0U},
            .submit_wait_count = 1U,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_id = 0U,
            .source_queue = vr::render_graph::QueueClass::transfer,
            .target_queue = vr::render_graph::QueueClass::graphics,
            .source_batch_id = 0U,
            .target_batch_id = 1U,
            .source_pass_id = 0U,
            .target_pass_id = 1U,
            .source_pass_debug_name = "text_2d_upload_instances",
            .target_pass_debug_name = "overlay_pass",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeComputeEnabledQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.compute_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.compute_queue_enabled = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.effective_queue_batch_count = 2U;
    diagnostics.effective_queue_dependency_count = 1U;
    diagnostics.effective_graphics_queue_batch_count = 1U;
    diagnostics.effective_compute_queue_batch_count = 1U;
    diagnostics.effective_owned_submit_batch_count = 1U;
    diagnostics.effective_cross_queue_dependency_count = 1U;
    diagnostics.effective_total_submit_wait_count = 1U;
    diagnostics.effective_total_submit_signal_count = 1U;
    diagnostics.graphics_submit_wait_count = 1U;
    diagnostics.non_graphics_submit_batch_count = 1U;
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 0U,
            .queue = vr::render_graph::QueueClass::compute,
            .pass_ids = {0U},
            .pass_debug_names = {"particle_2d_gpu_build"},
            .signal_dependency_ids = {0U},
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 1U,
            .queue = vr::render_graph::QueueClass::graphics,
            .pass_ids = {1U},
            .pass_debug_names = {"overlay_pass"},
            .wait_dependency_ids = {0U},
            .submit_wait_count = 1U,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_id = 0U,
            .source_queue = vr::render_graph::QueueClass::compute,
            .target_queue = vr::render_graph::QueueClass::graphics,
            .source_batch_id = 0U,
            .target_batch_id = 1U,
            .source_pass_id = 0U,
            .target_pass_id = 1U,
            .source_pass_debug_name = "particle_2d_gpu_build",
            .target_pass_debug_name = "overlay_pass",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeTransferComputeEnabledQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.transfer_queue_requested = true;
    diagnostics.compute_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.transfer_queue_enabled = true;
    diagnostics.compute_queue_enabled = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.effective_queue_batch_count = 3U;
    diagnostics.effective_queue_dependency_count = 2U;
    diagnostics.effective_graphics_queue_batch_count = 1U;
    diagnostics.effective_transfer_queue_batch_count = 1U;
    diagnostics.effective_compute_queue_batch_count = 1U;
    diagnostics.effective_owned_submit_batch_count = 2U;
    diagnostics.effective_cross_queue_dependency_count = 2U;
    diagnostics.effective_total_submit_wait_count = 3U;
    diagnostics.effective_total_submit_signal_count = 2U;
    diagnostics.graphics_submit_wait_count = 2U;
    diagnostics.non_graphics_submit_batch_count = 2U;
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 0U,
            .queue = vr::render_graph::QueueClass::transfer,
            .pass_ids = {0U},
            .pass_debug_names = {"upload_payload"},
            .signal_dependency_ids = {0U},
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 1U,
            .queue = vr::render_graph::QueueClass::compute,
            .pass_ids = {1U},
            .pass_debug_names = {"simulate_payload"},
            .wait_dependency_ids = {0U},
            .signal_dependency_ids = {1U},
            .submit_wait_count = 1U,
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 2U,
            .queue = vr::render_graph::QueueClass::graphics,
            .pass_ids = {2U},
            .pass_debug_names = {"prepare_present_target"},
            .wait_dependency_ids = {1U},
            .submit_wait_count = 2U,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_id = 0U,
            .source_queue = vr::render_graph::QueueClass::transfer,
            .target_queue = vr::render_graph::QueueClass::compute,
            .source_batch_id = 0U,
            .target_batch_id = 1U,
            .source_pass_id = 0U,
            .target_pass_id = 1U,
            .source_pass_debug_name = "upload_payload",
            .target_pass_debug_name = "simulate_payload",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_id = 1U,
            .source_queue = vr::render_graph::QueueClass::compute,
            .target_queue = vr::render_graph::QueueClass::graphics,
            .source_batch_id = 1U,
            .target_batch_id = 2U,
            .source_pass_id = 1U,
            .target_pass_id = 2U,
            .source_pass_debug_name = "simulate_payload",
            .target_pass_debug_name = "prepare_present_target",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeMultiGraphicsComputeEnabledQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.compute_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.compute_queue_enabled = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.effective_queue_batch_count = 3U;
    diagnostics.effective_queue_dependency_count = 2U;
    diagnostics.effective_graphics_queue_batch_count = 2U;
    diagnostics.effective_compute_queue_batch_count = 1U;
    diagnostics.effective_owned_submit_batch_count = 2U;
    diagnostics.effective_cross_queue_dependency_count = 2U;
    diagnostics.effective_total_submit_wait_count = 2U;
    diagnostics.effective_total_submit_signal_count = 2U;
    diagnostics.graphics_submit_wait_count = 1U;
    diagnostics.non_graphics_submit_batch_count = 2U;
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 0U,
            .queue = vr::render_graph::QueueClass::graphics,
            .pass_ids = {0U},
            .pass_debug_names = {"scene_prepare"},
            .signal_dependency_ids = {0U},
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 1U,
            .queue = vr::render_graph::QueueClass::compute,
            .pass_ids = {1U},
            .pass_debug_names = {"simulate_payload"},
            .wait_dependency_ids = {0U},
            .signal_dependency_ids = {1U},
            .submit_wait_count = 1U,
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_id = 2U,
            .queue = vr::render_graph::QueueClass::graphics,
            .pass_ids = {2U, 3U},
            .pass_debug_names = {"prepare_present_target", "present_transition"},
            .wait_dependency_ids = {1U},
            .submit_wait_count = 1U,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_id = 0U,
            .source_queue = vr::render_graph::QueueClass::graphics,
            .target_queue = vr::render_graph::QueueClass::compute,
            .source_batch_id = 0U,
            .target_batch_id = 1U,
            .source_pass_id = 0U,
            .target_pass_id = 1U,
            .source_pass_debug_name = "scene_prepare",
            .target_pass_debug_name = "simulate_payload",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_id = 1U,
            .source_queue = vr::render_graph::QueueClass::compute,
            .target_queue = vr::render_graph::QueueClass::graphics,
            .source_batch_id = 1U,
            .target_batch_id = 2U,
            .source_pass_id = 1U,
            .target_pass_id = 2U,
            .source_pass_debug_name = "simulate_payload",
            .target_pass_debug_name = "prepare_present_target",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    return diagnostics;
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

VR_TEST_CASE(RenderGraphVulkanBackend_lazy_memory_preference_does_not_fake_backend_policy,
             "unit;core;render_graph;vulkan") {
    const vr::render_graph::TextureDesc texture_desc{
        .dimension = vr::render_graph::TextureDimension::image_2d,
        .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
        .extent = {.width = 320U, .height = 180U, .depth = 1U},
        .usage = vr::render_graph::texture_usage_sampled_flag |
                 vr::render_graph::texture_usage_color_attachment_flag,
        .prefer_lazy_memory = true,
    };

    const auto target_desc = vr::render_graph::VulkanResourceTable::BuildRenderTargetDesc(texture_desc);

    VR_CHECK(target_desc.memory_policy == vr::render::RenderTargetMemoryPolicy::auto_select);
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

    auto compiled = builder.Compile();

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

VR_TEST_CASE(RenderGraphQueueExecutionPolicy_falls_back_to_graphics_until_multi_queue_submit_is_enabled,
             "unit;render_graph;runtime;queue") {
    const auto compiled = BuildCrossQueuePolicyGraph();

    vr::render_graph::QueueExecutionCapabilities capabilities{};
    capabilities.queue_families.graphics = 1U;
    capabilities.queue_families.present = 1U;
    capabilities.queue_families.transfer = 2U;
    capabilities.queue_families.compute = 3U;
    capabilities.has_graphics_queue = true;
    capabilities.has_transfer_queue = true;
    capabilities.has_compute_queue = true;

    const auto policy = vr::render_graph::ResolveQueueExecutionPolicy(
        compiled,
        capabilities,
        false);

    VR_CHECK(policy.transfer_requested);
    VR_CHECK(policy.compute_requested);
    VR_CHECK(policy.multi_queue_requested);
    VR_CHECK(!policy.transfer_enabled);
    VR_CHECK(!policy.compute_enabled);
    VR_CHECK(!policy.multi_queue_enabled);
    VR_CHECK(policy.graphics_fallback_active);
    VR_REQUIRE(policy.effective_queue_families.graphics.has_value());
    VR_CHECK(policy.effective_queue_families.transfer ==
             policy.effective_queue_families.graphics);
    VR_CHECK(policy.effective_queue_families.compute ==
             policy.effective_queue_families.graphics);
    VR_CHECK(ContainsCaseInsensitive(policy.fallback_reason, "not enabled"));
}

VR_TEST_CASE(RenderGraphQueueExecutionPolicy_keeps_distinct_transfer_and_compute_queues_when_enabled,
             "unit;render_graph;runtime;queue") {
    const auto compiled = BuildCrossQueuePolicyGraph();

    vr::render_graph::QueueExecutionCapabilities capabilities{};
    capabilities.queue_families.graphics = 1U;
    capabilities.queue_families.present = 1U;
    capabilities.queue_families.transfer = 2U;
    capabilities.queue_families.compute = 3U;
    capabilities.has_graphics_queue = true;
    capabilities.has_transfer_queue = true;
    capabilities.has_compute_queue = true;

    const auto policy = vr::render_graph::ResolveQueueExecutionPolicy(
        compiled,
        capabilities,
        true);

    VR_CHECK(policy.transfer_requested);
    VR_CHECK(policy.compute_requested);
    VR_CHECK(policy.transfer_enabled);
    VR_CHECK(policy.compute_enabled);
    VR_CHECK(policy.multi_queue_enabled);
    VR_CHECK(!policy.graphics_fallback_active);
    VR_CHECK(policy.fallback_reason.empty());
    VR_CHECK(policy.effective_queue_families.transfer == capabilities.queue_families.transfer);
    VR_CHECK(policy.effective_queue_families.compute == capabilities.queue_families.compute);
}

VR_TEST_CASE(RenderGraphQueueExecutionPolicy_keeps_owned_submit_enabled_when_transfer_and_compute_alias_graphics,
             "unit;render_graph;runtime;queue") {
    const auto compiled = BuildCrossQueuePolicyGraph();

    vr::render_graph::QueueExecutionCapabilities capabilities{};
    capabilities.queue_families.graphics = 1U;
    capabilities.queue_families.present = 1U;
    capabilities.queue_families.transfer = 1U;
    capabilities.queue_families.compute = 1U;
    capabilities.has_graphics_queue = true;
    capabilities.has_transfer_queue = true;
    capabilities.has_compute_queue = true;

    const auto policy = vr::render_graph::ResolveQueueExecutionPolicy(
        compiled,
        capabilities,
        true);

    VR_CHECK(policy.transfer_requested);
    VR_CHECK(policy.compute_requested);
    VR_CHECK(policy.transfer_enabled);
    VR_CHECK(policy.compute_enabled);
    VR_CHECK(policy.multi_queue_enabled);
    VR_CHECK(!policy.graphics_fallback_active);
    VR_CHECK(policy.fallback_reason.empty());
    VR_CHECK(policy.effective_queue_families.transfer == capabilities.queue_families.transfer);
    VR_CHECK(policy.effective_queue_families.compute == capabilities.queue_families.compute);
}

VR_TEST_CASE(RenderGraphQueueTimelineView_classifies_fallback_transfer_and_compute_shapes,
             "unit;render_graph;runtime;queue;diagnostics") {
    const auto fallback_view = vr::runtime::BuildRenderGraphQueueTimelineView(
        MakeGraphicsFallbackQueueTimelineDiagnostics());
    VR_CHECK(fallback_view.available);
    VR_CHECK(fallback_view.mode == vr::runtime::RenderGraphQueueTimelineMode::graphics_fallback);
    VR_CHECK(fallback_view.mode_name == "graphics_fallback");
    VR_CHECK(fallback_view.graphics_batch_count == 1U);
    VR_CHECK(fallback_view.transfer_batch_count == 0U);
    VR_CHECK(fallback_view.compute_batch_count == 0U);
    VR_CHECK(fallback_view.cross_queue_dependency_count == 0U);

    const auto transfer_view = vr::runtime::BuildRenderGraphQueueTimelineView(
        MakeTransferEnabledQueueTimelineDiagnostics());
    VR_CHECK(transfer_view.mode == vr::runtime::RenderGraphQueueTimelineMode::transfer_enabled);
    VR_CHECK(transfer_view.graphics_batch_count == 1U);
    VR_CHECK(transfer_view.transfer_batch_count == 1U);
    VR_CHECK(transfer_view.compute_batch_count == 0U);
    VR_CHECK(transfer_view.cross_queue_dependency_count == 1U);
    VR_CHECK(transfer_view.total_submit_wait_count == 1U);
    VR_CHECK(transfer_view.total_submit_signal_count == 1U);

    const auto compute_view = vr::runtime::BuildRenderGraphQueueTimelineView(
        MakeComputeEnabledQueueTimelineDiagnostics());
    VR_CHECK(compute_view.mode == vr::runtime::RenderGraphQueueTimelineMode::compute_enabled);
    VR_CHECK(compute_view.graphics_batch_count == 1U);
    VR_CHECK(compute_view.transfer_batch_count == 0U);
    VR_CHECK(compute_view.compute_batch_count == 1U);
    VR_CHECK(compute_view.cross_queue_dependency_count == 1U);
    VR_CHECK(compute_view.total_submit_wait_count == 1U);
    VR_CHECK(compute_view.total_submit_signal_count == 1U);

    const auto multi_graphics_view = vr::runtime::BuildRenderGraphQueueTimelineView(
        MakeMultiGraphicsComputeEnabledQueueTimelineDiagnostics());
    VR_CHECK(multi_graphics_view.mode == vr::runtime::RenderGraphQueueTimelineMode::compute_enabled);
    VR_CHECK(multi_graphics_view.graphics_batch_count == 2U);
    VR_CHECK(multi_graphics_view.transfer_batch_count == 0U);
    VR_CHECK(multi_graphics_view.compute_batch_count == 1U);
    VR_CHECK(multi_graphics_view.owned_submit_batch_count == 2U);
    VR_CHECK(multi_graphics_view.cross_queue_dependency_count == 2U);
    VR_CHECK(multi_graphics_view.total_submit_wait_count == 2U);
    VR_CHECK(multi_graphics_view.total_submit_signal_count == 2U);
}

VR_TEST_CASE(RenderGraphQueueTimelineJson_serializes_stable_mode_batches_and_dependencies,
             "unit;render_graph;runtime;queue;diagnostics") {
    const auto diagnostics = MakeTransferComputeEnabledQueueTimelineDiagnostics();
    const auto view = vr::runtime::BuildRenderGraphQueueTimelineView(diagnostics);
    const std::string json = vr::runtime::BuildRenderGraphQueueTimelineJson(view);

    VR_CHECK(view.mode == vr::runtime::RenderGraphQueueTimelineMode::transfer_compute_enabled);
    VR_CHECK(view.batch_count == 3U);
    VR_CHECK(view.dependency_count == 2U);
    VR_CHECK(view.transfer_batch_count == 1U);
    VR_CHECK(view.compute_batch_count == 1U);
    VR_CHECK(view.graphics_batch_count == 1U);
    VR_CHECK(view.owned_submit_batch_count == 2U);
    VR_CHECK(view.cross_queue_dependency_count == 2U);
    VR_CHECK(view.total_submit_wait_count == 3U);
    VR_CHECK(view.total_submit_signal_count == 2U);
    VR_CHECK(json.find("\"fallbackReason\": \"\"") != std::string::npos);
    VR_CHECK(json.find("\"ownedSubmitBatches\": 2") != std::string::npos);
    VR_CHECK(json.find("\"mode\": \"transfer_compute_enabled\"") != std::string::npos);
    VR_CHECK(json.find("\"queue\": \"transfer\"") != std::string::npos);
    VR_CHECK(json.find("\"queue\": \"compute\"") != std::string::npos);
    VR_CHECK(json.find("\"queue\": \"graphics\"") != std::string::npos);
    VR_CHECK(json.find("\"id\": 0") != std::string::npos);
    VR_CHECK(json.find("\"sourceBatchId\": 0") != std::string::npos);
    VR_CHECK(json.find("\"targetBatchId\": 2") != std::string::npos);
    VR_CHECK(json.find("\"sourcePassId\": 1") != std::string::npos);
    VR_CHECK(json.find("\"sourceQueue\": \"transfer\"") != std::string::npos);
    VR_CHECK(json.find("\"targetQueue\": \"graphics\"") != std::string::npos);
    VR_CHECK(json.find("\"passes\": [\"upload_payload\"]") != std::string::npos);
}

VR_TEST_CASE(RenderGraphVulkanBackend_builds_command_ready_barrier_batches_from_physical_resources,
             "integration;render_graph;vulkan") {
    Host host{};
    try {
        auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
        EnableRenderGraphRuntimeExecutionFeatures(create_info);
        host.Initialize(create_info);
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

    auto compiled = builder.Compile();
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

VR_TEST_CASE(RenderGraphVulkanBackend_aliases_non_overlapping_transient_buffers_onto_shared_page,
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

    auto compiled = builder.Compile();
    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);

    const auto* record_a = table.FindBuffer(temp_a);
    const auto* record_b = table.FindBuffer(temp_b);
    VR_REQUIRE(record_a != nullptr);
    VR_REQUIRE(record_b != nullptr);
    VR_CHECK(record_a->owned_resource.buffer != VK_NULL_HANDLE);
    VR_CHECK(record_b->owned_resource.buffer != VK_NULL_HANDLE);
    VR_CHECK(record_a->alias_page_index == record_b->alias_page_index);
    VR_CHECK(record_a->alias_page_index != vr::render_graph::invalid_render_graph_index);
    VR_CHECK(record_a->owned_resource.allocation_slice.handle ==
             record_b->owned_resource.allocation_slice.handle);
    VR_CHECK(record_a->owned_resource.allocation_slice.offset ==
             record_b->owned_resource.allocation_slice.offset);
    VR_CHECK(!record_a->owned_resource.owns_allocation);
    VR_CHECK(!record_b->owned_resource.owns_allocation);
    VR_CHECK(record_a->aliased);
    VR_CHECK(record_b->aliased);
    VR_CHECK(table.Stats().transient_buffer_page_count == 1U);
    VR_CHECK(table.Stats().transient_aliased_buffer_count == 2U);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphVulkanBackend_rejects_cross_queue_alias_realization_before_mutation,
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

    const auto pass_a = builder.AddPass("pass_a", false, vr::render_graph::QueueClass::graphics);
    const auto pass_b = builder.AddPass("pass_b", true, vr::render_graph::QueueClass::graphics);
    const auto pass_c = builder.AddPass("pass_c", false, vr::render_graph::QueueClass::compute);
    const auto pass_d = builder.AddPass("pass_d", true, vr::render_graph::QueueClass::compute);

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

    auto compiled = builder.Compile();
    const std::string graph_before_resolve = compiled.BuildJson();

    const auto alias_batch = std::find_if(
        compiled.PlannedBarriers().barrier_batches.begin(),
        compiled.PlannedBarriers().barrier_batches.end(),
        [&](const vr::render_graph::CompiledBarrierBatch& batch_) {
            return batch_.pass.index == pass_c.index;
        });
    VR_REQUIRE(alias_batch != compiled.PlannedBarriers().barrier_batches.end());
    const auto alias_barrier = std::find_if(
        alias_batch->barriers.begin(),
        alias_batch->barriers.end(),
        [](const vr::render_graph::LogicalBarrier& barrier_) {
            return barrier_.aliasing;
        });
    VR_REQUIRE(alias_barrier != alias_batch->barriers.end());
    VR_CHECK(alias_barrier->queue_transfer);
    VR_CHECK(alias_barrier->src_queue == vr::render_graph::QueueClass::graphics);
    VR_CHECK(alias_barrier->dst_queue == vr::render_graph::QueueClass::compute);

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);

    bool threw = false;
    try {
        table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);
    } catch (const vr::render_graph::VulkanResourceResolveError& error_) {
        threw = true;
        VR_CHECK(error_.Code() ==
                 vr::render_graph::VulkanResourceResolveErrorCode::unsupported_cross_queue_alias);
        VR_CHECK(error_.SourceQueue() == vr::render_graph::QueueClass::graphics);
        VR_CHECK(error_.TargetQueue() == vr::render_graph::QueueClass::compute);
        VR_CHECK(error_.PreviousResource().index == temp_a.index);
        VR_CHECK(error_.NextResource().index == temp_b.index);
    }
    VR_REQUIRE(threw);

    VR_CHECK(compiled.BuildJson() == graph_before_resolve);
    VR_CHECK(table.Textures().empty());
    VR_CHECK(table.Buffers().empty());
    VR_CHECK(table.Stats().persistent_buffer_count == 0U);
    VR_CHECK(table.Stats().transient_buffer_count == 0U);
    VR_CHECK(table.Stats().transient_buffer_page_count == 0U);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphVulkanBackend_realizes_alias_barriers_into_command_ready_memory_barriers,
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
    if (host.Context().EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        VR_SKIP("synchronization2 feature unavailable");
    }

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

    const auto pass_a = builder.AddPass("pass_a", true, vr::render_graph::QueueClass::graphics);
    const auto pass_b = builder.AddPass("pass_b", true, vr::render_graph::QueueClass::graphics);
    const auto pass_c = builder.AddPass("pass_c", true, vr::render_graph::QueueClass::graphics);
    const auto pass_d = builder.AddPass("pass_d", true, vr::render_graph::QueueClass::graphics);

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

    auto compiled = builder.Compile();
    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);

    vr::QueueFamilyIndices queue_families{};
    queue_families.graphics = 2U;
    const auto lowered = vr::render_graph::LowerToVulkanBarrierPlan(compiled, queue_families);
    const auto command_ready = vr::render_graph::BuildCommandReadyVulkanBarrierPlan(
        lowered,
        table,
        host.RenderTarget());

    VR_REQUIRE(compiled.PlannedBarriers().alias_barriers.size() == 1U);
    VR_CHECK(compiled.PlannedBarriers().alias_barriers[0].realized);

    const auto lowered_batch = std::find_if(
        lowered.barrier_batches.begin(),
        lowered.barrier_batches.end(),
        [&](const vr::render_graph::VulkanBarrierBatch& batch_) {
            return batch_.pass.index == pass_c.index;
        });
    VR_REQUIRE(lowered_batch != lowered.barrier_batches.end());
    const auto lowered_alias = std::find_if(
        lowered_batch->barriers.begin(),
        lowered_batch->barriers.end(),
        [](const vr::render_graph::LoweredVulkanBarrier& barrier_) {
            return barrier_.aliasing;
        });
    VR_REQUIRE(lowered_alias != lowered_batch->barriers.end());

    const auto command_batch = std::find_if(
        command_ready.command_batches.begin(),
        command_ready.command_batches.end(),
        [&](const vr::render_graph::VulkanCommandBarrierBatch& batch_) {
            return batch_.pass.index == pass_c.index;
        });
    VR_REQUIRE(command_batch != command_ready.command_batches.end());
    const auto dependency_info = command_batch->dependency.BuildVkDependencyInfo();
    VR_CHECK(dependency_info.memoryBarrierCount >= 1U);
    VR_CHECK(command_batch->dependency.memory_barriers.size() >= 1U);

    const VkCommandBuffer command_buffer = host.Context().BeginSingleTimeCommands();
    auto graph_context = vr::render_graph::GraphCommandContext{
        host.Context(),
        0U,
        command_buffer,
        compiled,
        table,
        host.RenderTarget(),
        nullptr,
        lowered,
        command_ready,
    };
    const auto stats = vr::render_graph::RenderGraphExecutor::Record(graph_context);
    host.Context().EndSingleTimeCommands(command_buffer);

    VR_CHECK(stats.memory_barrier_count >= 1U);
    VR_CHECK(stats.queue_transfer_batch_count == 0U);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphVulkanBackend_rebuilds_transient_buffer_plan_from_exact_requirements,
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
    const auto temp_buffer = builder.CreateBuffer(
        "temp_buffer",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });
    const auto pass = builder.AddPass("write_temp_buffer", true);
    (void)builder.Write(
        pass,
        temp_buffer,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});

    auto compiled = builder.Compile();
    const auto fallback_record = std::find_if(
        compiled.TransientAllocations().records.begin(),
        compiled.TransientAllocations().records.end(),
        [&](const vr::render_graph::TransientAllocationRecord& record_) {
            return record_.resource.index == temp_buffer.index;
        });
    VR_REQUIRE(fallback_record != compiled.TransientAllocations().records.end());
    VR_CHECK(fallback_record->footprint.alignment_bytes == 1U);
    VR_CHECK(fallback_record->footprint.memory_type_bits == 0xFFFFFFFFU);

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);

    const auto* physical_record = table.FindBuffer(temp_buffer);
    VR_REQUIRE(physical_record != nullptr);
    VR_REQUIRE(physical_record->owned_resource.buffer != VK_NULL_HANDLE);

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(host.Context().Device(),
                                  physical_record->owned_resource.buffer,
                                  &requirements);

    const auto realized_record = std::find_if(
        compiled.TransientAllocations().records.begin(),
        compiled.TransientAllocations().records.end(),
        [&](const vr::render_graph::TransientAllocationRecord& record_) {
            return record_.resource.index == temp_buffer.index;
        });
    VR_REQUIRE(realized_record != compiled.TransientAllocations().records.end());
    VR_CHECK(realized_record->footprint.size_bytes == requirements.size);
    VR_CHECK(realized_record->footprint.alignment_bytes == requirements.alignment);
    VR_CHECK(realized_record->footprint.memory_type_bits == requirements.memoryTypeBits);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphVulkanBackend_resolve_failure_rolls_back_graph_and_previous_table_state,
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

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);

    vr::render_graph::RenderGraphBuilder seed_builder{};
    const auto persistent_buffer = seed_builder.CreateBuffer(
        "persistent_history",
        vr::render_graph::BufferDesc{
            .size_bytes = 1024U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        },
        vr::render_graph::ResourceLifetime::persistent);
    const auto seed_pass = seed_builder.AddPass("seed_pass", true);
    (void)seed_builder.Write(
        seed_pass,
        persistent_buffer,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});

    auto seed_compiled = seed_builder.Compile();
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), seed_compiled, 0U, 0U);

    VR_REQUIRE(table.Buffers().size() == 1U);
    const VkBuffer baseline_buffer_handle = table.Buffers()[0].owned_resource.buffer;
    VR_REQUIRE(baseline_buffer_handle != VK_NULL_HANDLE);
    VR_CHECK(table.Stats().persistent_buffer_count == 1U);
    VR_CHECK(table.Stats().transient_buffer_count == 0U);

    vr::render_graph::RenderGraphBuilder failing_builder{};
    const auto temp_buffer = failing_builder.CreateBuffer(
        "temp_buffer",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });
    const auto missing_import = failing_builder.CreateBuffer(
        "missing_import",
        vr::render_graph::BufferDesc{
            .size_bytes = 256U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        },
        vr::render_graph::ResourceLifetime::imported);
    const auto fail_pass = failing_builder.AddPass("fail_pass", true);
    (void)failing_builder.Write(
        fail_pass,
        temp_buffer,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});
    (void)failing_builder.Read(
        fail_pass,
        missing_import,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_read});

    auto failing_compiled = failing_builder.Compile();
    const std::string graph_before_resolve = failing_compiled.BuildJson();
    const auto fallback_record = std::find_if(
        failing_compiled.TransientAllocations().records.begin(),
        failing_compiled.TransientAllocations().records.end(),
        [&](const vr::render_graph::TransientAllocationRecord& record_) {
            return record_.resource.index == temp_buffer.index;
        });
    VR_REQUIRE(fallback_record != failing_compiled.TransientAllocations().records.end());
    VR_CHECK(fallback_record->footprint.alignment_bytes == 1U);
    VR_CHECK(fallback_record->footprint.memory_type_bits == 0xFFFFFFFFU);

    bool threw = false;
    try {
        table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), failing_compiled, 0U, 0U);
    } catch (const std::invalid_argument& error_) {
        threw = true;
        VR_CHECK(ContainsCaseInsensitive(error_.what(), "missing imported buffer binding"));
    }
    VR_REQUIRE(threw);

    VR_CHECK(failing_compiled.BuildJson() == graph_before_resolve);
    VR_REQUIRE(table.Buffers().size() == 1U);
    VR_CHECK(table.Textures().empty());
    VR_CHECK(table.Buffers()[0].logical.index == persistent_buffer.index);
    VR_CHECK(table.Buffers()[0].owned_resource.buffer == baseline_buffer_handle);
    VR_CHECK(table.Stats().persistent_buffer_count == 1U);
    VR_CHECK(table.Stats().transient_buffer_count == 0U);
    VR_CHECK(table.Stats().transient_buffer_page_count == 0U);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphVulkanBackend_resolve_late_failure_before_publish_rolls_back_graph_and_table_state,
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

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);

    vr::render_graph::RenderGraphBuilder seed_builder{};
    const auto persistent_buffer = seed_builder.CreateBuffer(
        "persistent_history",
        vr::render_graph::BufferDesc{
            .size_bytes = 1024U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        },
        vr::render_graph::ResourceLifetime::persistent);
    const auto seed_pass = seed_builder.AddPass("seed_pass", true);
    (void)seed_builder.Write(
        seed_pass,
        persistent_buffer,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});

    auto seed_compiled = seed_builder.Compile();
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), seed_compiled, 0U, 0U);

    VR_REQUIRE(table.Buffers().size() == 1U);
    const VkBuffer baseline_buffer_handle = table.Buffers()[0].owned_resource.buffer;
    const auto baseline_stats = table.Stats();

    vr::render_graph::RenderGraphBuilder staged_builder{};
    const auto transient_buffer = staged_builder.CreateBuffer(
        "staged_temp",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag |
                     vr::render_graph::buffer_usage_transfer_dst_flag,
        });
    const auto staged_pass = staged_builder.AddPass("staged_pass", true);
    (void)staged_builder.Write(
        staged_pass,
        transient_buffer,
        vr::render_graph::AccessDesc{.access = vr::render_graph::AccessKind::shader_storage_write});

    auto staged_compiled = staged_builder.Compile();
    const std::string graph_before_resolve = staged_compiled.BuildJson();

    bool threw = false;
    {
        ResolveFailureInjectionScope failure_scope{true};
        try {
            table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), staged_compiled, 0U, 0U);
        } catch (const std::runtime_error& error_) {
            threw = true;
            VR_CHECK(ContainsCaseInsensitive(error_.what(), "before publish"));
        }
    }
    VR_REQUIRE(threw);

    VR_CHECK(staged_compiled.BuildJson() == graph_before_resolve);
    VR_REQUIRE(table.Buffers().size() == 1U);
    VR_CHECK(table.Textures().empty());
    VR_CHECK(table.Buffers()[0].logical.index == persistent_buffer.index);
    VR_CHECK(table.Buffers()[0].owned_resource.buffer == baseline_buffer_handle);
    VR_CHECK(table.Stats().persistent_buffer_count == baseline_stats.persistent_buffer_count);
    VR_CHECK(table.Stats().transient_buffer_count == baseline_stats.transient_buffer_count);
    VR_CHECK(table.Stats().transient_buffer_page_count == baseline_stats.transient_buffer_page_count);
    VR_CHECK(table.Stats().transient_image_page_count == baseline_stats.transient_image_page_count);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphVulkanBackend_alias_image_first_raster_use_drives_fresh_layout_transition,
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
    if (host.Context().EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        VR_SKIP("synchronization2 feature unavailable");
    }

    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateTexture(
        "temp_a",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 128U, .height = 72U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
            .allow_alias = true,
        });
    const auto temp_b = builder.CreateTexture(
        "temp_b",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 128U, .height = 72U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
            .allow_alias = true,
        });

    const auto pass_a = builder.AddPass("pass_a", true);
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c", true);
    const auto pass_d = builder.AddPass("pass_d", true);
    builder.SetExecuteCallback(pass_b, [](vr::render_graph::GraphCommandContext&) {});
    builder.SetExecuteCallback(pass_d, [](vr::render_graph::GraphCommandContext&) {});

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
    builder.SetRasterPassDesc(
        pass_a,
        vr::render_graph::RasterPassDesc{
            .color_attachments = {
                vr::render_graph::RasterColorAttachmentDesc{
                    .target = temp_a,
                    .load_op = vr::render_graph::AttachmentLoadOp::clear,
                    .store_op = vr::render_graph::AttachmentStoreOp::store,
                },
            },
        });
    builder.SetRasterPassDesc(
        pass_c,
        vr::render_graph::RasterPassDesc{
            .color_attachments = {
                vr::render_graph::RasterColorAttachmentDesc{
                    .target = temp_b,
                    .load_op = vr::render_graph::AttachmentLoadOp::clear,
                    .store_op = vr::render_graph::AttachmentStoreOp::store,
                },
            },
        });

    auto compiled = builder.Compile();
    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);

    const auto* record_a = table.FindTexture(temp_a);
    const auto* record_b = table.FindTexture(temp_b);
    VR_REQUIRE(record_a != nullptr);
    VR_REQUIRE(record_b != nullptr);
    VR_CHECK(record_a->owned_resource.image != VK_NULL_HANDLE);
    VR_CHECK(record_b->owned_resource.image != VK_NULL_HANDLE);
    VR_CHECK(record_a->alias_page_index == record_b->alias_page_index);
    VR_CHECK(record_a->alias_page_index != vr::render_graph::invalid_render_graph_index);
    VR_CHECK(record_a->owned_resource.allocation_slice.handle ==
             record_b->owned_resource.allocation_slice.handle);
    VR_CHECK(record_a->owned_resource.allocation_slice.offset ==
             record_b->owned_resource.allocation_slice.offset);
    VR_CHECK(!record_a->owned_resource.owns_allocation);
    VR_CHECK(!record_b->owned_resource.owns_allocation);
    VR_CHECK(record_a->aliased);
    VR_CHECK(record_b->aliased);
    VR_CHECK(table.Stats().transient_image_page_count == 1U);
    VR_CHECK(table.Stats().transient_aliased_texture_count == 2U);

    const auto* target_a = host.RenderTarget().Resolve(record_a->render_target);
    const auto* target_b = host.RenderTarget().Resolve(record_b->render_target);
    VR_REQUIRE(target_a != nullptr);
    VR_REQUIRE(target_b != nullptr);
    VR_CHECK(target_a->format == VK_FORMAT_R16G16B16A16_SFLOAT);
    VR_CHECK(target_b->format == VK_FORMAT_R16G16B16A16_SFLOAT);

    vr::QueueFamilyIndices queue_families{};
    queue_families.graphics = 2U;
    const auto lowered = vr::render_graph::LowerToVulkanBarrierPlan(compiled, queue_families);
    const auto command_ready = vr::render_graph::BuildCommandReadyVulkanBarrierPlan(
        lowered,
        table,
        host.RenderTarget());

    VR_REQUIRE(compiled.PlannedBarriers().alias_barriers.size() == 1U);
    VR_CHECK(compiled.PlannedBarriers().alias_barriers[0].realized);

    const auto lowered_batch = std::find_if(
        lowered.barrier_batches.begin(),
        lowered.barrier_batches.end(),
        [&](const vr::render_graph::VulkanBarrierBatch& batch_) {
            return batch_.pass.index == pass_c.index;
        });
    VR_REQUIRE(lowered_batch != lowered.barrier_batches.end());
    const auto lowered_alias = std::find_if(
        lowered_batch->barriers.begin(),
        lowered_batch->barriers.end(),
        [](const vr::render_graph::LoweredVulkanBarrier& barrier_) {
            return barrier_.aliasing;
        });
    VR_REQUIRE(lowered_alias != lowered_batch->barriers.end());
    VR_CHECK(lowered_alias->old_layout == VK_IMAGE_LAYOUT_UNDEFINED);
    VR_CHECK(lowered_alias->new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto command_batch = std::find_if(
        command_ready.command_batches.begin(),
        command_ready.command_batches.end(),
        [&](const vr::render_graph::VulkanCommandBarrierBatch& batch_) {
            return batch_.pass.index == pass_c.index;
        });
    VR_REQUIRE(command_batch != command_ready.command_batches.end());
    const auto resolved_view_b = host.RenderTarget().ResolveView(record_b->render_target);
    const auto image_barrier = std::find_if(
        command_batch->dependency.image_barriers.begin(),
        command_batch->dependency.image_barriers.end(),
        [&](const VkImageMemoryBarrier2& barrier_) {
            return barrier_.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                   barrier_.newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                   barrier_.image == resolved_view_b.image;
        });
    VR_REQUIRE(image_barrier != command_batch->dependency.image_barriers.end());

    const VkCommandBuffer command_buffer = host.Context().BeginSingleTimeCommands();
    auto graph_context = vr::render_graph::GraphCommandContext{
        host.Context(),
        0U,
        command_buffer,
        compiled,
        table,
        host.RenderTarget(),
        nullptr,
        lowered,
        command_ready,
    };
    const auto stats = vr::render_graph::RenderGraphExecutor::Record(graph_context);
    host.Context().EndSingleTimeCommands(command_buffer);

    VR_CHECK(stats.image_barrier_count >= 1U);

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

    auto compiled = builder.Compile();
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
    auto graph_context = vr::render_graph::GraphCommandContext{
        host.Context(),
        0U,
        command_buffer,
        compiled,
        table,
        host.RenderTarget(),
        nullptr,
        lowered,
        command_ready,
    };
    const auto stats = vr::render_graph::RenderGraphExecutor::Record(graph_context);
    host.Context().EndSingleTimeCommands(command_buffer);

    VR_CHECK(executed_pass_count == 1U);
    VR_CHECK(stats.pass_count == 1U);
    VR_CHECK(stats.command_batch_count == 0U);

    table.Shutdown(host.Context(), host.RenderTarget(), 0U, 0U);
}

VR_TEST_CASE(RenderGraphExecutor_fuses_native_pass_groups_into_single_dynamic_rendering_scope,
             "integration;render_graph;executor;native_pass;vulkan") {
    Host host{};
    try {
        auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
        EnableRenderGraphRuntimeExecutionFeatures(create_info);
        host.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
    if (host.Context().EnabledVulkan13Features().dynamicRendering != VK_TRUE) {
        VR_SKIP("dynamicRendering feature unavailable");
    }
    if (host.Context().EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        VR_SKIP("synchronization2 feature unavailable");
    }

    host.EnsureSwapchainTargetsForFrame(0U, 0U);
    const auto imported_present = host.SwapchainTargets().Get(0U);
    VR_REQUIRE(vr::render::IsValidRenderTargetHandle(imported_present));

    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 160U, .height = 90U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_sampled_flag,
        });
    const auto scene_depth = builder.CreateTexture(
        "scene_depth",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::d32_sfloat,
            .extent = {.width = 160U, .height = 90U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_depth_stencil_attachment_flag,
        });
    const auto present_target = builder.CreateTexture(
        "present_target",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::unknown,
            .extent = {.width = 160U, .height = 90U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_color_attachment_flag |
                     vr::render_graph::texture_usage_present_flag,
        },
        vr::render_graph::ResourceLifetime::imported);

    const auto opaque = builder.AddPass("opaque_scene");
    const auto transparent = builder.AddPass("transparent_scene");
    const auto present = builder.AddPass("present_to_swapchain", true);

    const auto color_v1 = builder.Write(
        opaque,
        scene_color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    const auto depth_v1 = builder.Write(
        opaque,
        scene_depth,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });
    (void)builder.Read(
        transparent,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_read,
        });
    const auto color_v2 = builder.Write(
        transparent,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)builder.Read(
        transparent,
        depth_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_read,
        });
    (void)builder.Write(
        transparent,
        depth_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::depth_stencil_write,
        });
    (void)builder.Read(
        present,
        color_v2,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
        });
    (void)builder.Write(
        present,
        present_target,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::present,
        });

    const auto make_raster_pass_desc =
        [](const vr::render_graph::ResourceHandle color_,
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
        };
    builder.SetRasterPassDesc(
        opaque,
        make_raster_pass_desc(scene_color,
                              scene_depth,
                              vr::render_graph::AttachmentLoadOp::load,
                              vr::render_graph::AttachmentLoadOp::load));
    builder.SetRasterPassDesc(
        transparent,
        make_raster_pass_desc(scene_color,
                              scene_depth,
                              vr::render_graph::AttachmentLoadOp::load,
                              vr::render_graph::AttachmentLoadOp::load));

    std::vector<std::uint32_t> executed_passes{};
    builder.SetExecuteCallback(opaque, [&](vr::render_graph::GraphCommandContext& context_) {
        executed_passes.push_back(context_.CurrentPass().index);
        VR_CHECK(context_.CurrentPass().index == opaque.index);
    });
    builder.SetExecuteCallback(transparent, [&](vr::render_graph::GraphCommandContext& context_) {
        executed_passes.push_back(context_.CurrentPass().index);
        VR_CHECK(context_.CurrentPass().index == transparent.index);
    });

    auto compiled = builder.Compile();
    VR_REQUIRE(compiled.NativePasses().groups.size() == 1U);
    VR_CHECK(compiled.NativePasses().groups[0].first_pass_order == 0U);
    VR_CHECK(compiled.NativePasses().groups[0].last_pass_order == 1U);
    VR_CHECK(compiled.NativePasses().summary.fused_raster_pass_count == 1U);
    VR_CHECK(compiled.NativePasses().summary.load_inference_count == 2U);
    VR_CHECK(compiled.NativePasses().summary.store_elision_count == 1U);
    VR_REQUIRE(compiled.NativePasses().groups[0].attachments.color_attachments.size() == 1U);
    VR_CHECK(compiled.NativePasses().groups[0].attachments.color_attachments[0].effective_load_op ==
             vr::render_graph::AttachmentLoadOp::dont_care);
    VR_REQUIRE(compiled.NativePasses().groups[0].attachments.has_depth_attachment);
    VR_CHECK(compiled.NativePasses().groups[0].attachments.depth_attachment.effective_load_op ==
             vr::render_graph::AttachmentLoadOp::dont_care);
    VR_CHECK(compiled.NativePasses().groups[0].attachments.depth_attachment.effective_store_op ==
             vr::render_graph::AttachmentStoreOp::dont_care);

    vr::QueueFamilyIndices queue_families{};
    queue_families.graphics = 2U;
    const auto lowered = vr::render_graph::LowerToVulkanBarrierPlan(compiled, queue_families);

    vr::render_graph::VulkanResourceTable table{};
    table.BeginFrame(host.Context(), host.RenderTarget(), 0U, 0U);
    table.RegisterImportedTexture(present_target, imported_present);
    table.Resolve(host.Context(), host.GpuMemory(), host.RenderTarget(), compiled, 0U, 0U);
    const auto command_ready = vr::render_graph::BuildCommandReadyVulkanBarrierPlan(
        lowered,
        table,
        host.RenderTarget());

    const auto lowered_transparent_batch = std::find_if(
        lowered.barrier_batches.begin(),
        lowered.barrier_batches.end(),
        [&](const vr::render_graph::VulkanBarrierBatch& batch_) {
            return batch_.pass.index == transparent.index;
        });
    VR_CHECK(lowered_transparent_batch == lowered.barrier_batches.end());

    const auto command_transparent_batch = std::find_if(
        command_ready.command_batches.begin(),
        command_ready.command_batches.end(),
        [&](const vr::render_graph::VulkanCommandBarrierBatch& batch_) {
            return batch_.pass.index == transparent.index;
        });
    VR_CHECK(command_transparent_batch == command_ready.command_batches.end());

    const VkCommandBuffer command_buffer = host.Context().BeginSingleTimeCommands();
    auto graph_context = vr::render_graph::GraphCommandContext{
        host.Context(),
        0U,
        command_buffer,
        compiled,
        table,
        host.RenderTarget(),
        nullptr,
        lowered,
        command_ready,
    };
    const auto rendering_info =
        graph_context.BuildRenderingInfo(compiled.NativePasses().groups[0]);
    VR_CHECK(rendering_info.color_attachments[0].loadOp ==
             VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    VR_CHECK(rendering_info.color_attachments[0].storeOp ==
             VK_ATTACHMENT_STORE_OP_STORE);
    VR_CHECK(rendering_info.depth_attachment.loadOp ==
             VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    VR_CHECK(rendering_info.depth_attachment.storeOp ==
             VK_ATTACHMENT_STORE_OP_DONT_CARE);
    const auto stats = vr::render_graph::RenderGraphExecutor::Record(graph_context);
    host.Context().EndSingleTimeCommands(command_buffer);

    VR_REQUIRE(executed_passes.size() == 2U);
    VR_CHECK(executed_passes[0] == opaque.index);
    VR_CHECK(executed_passes[1] == transparent.index);
    VR_CHECK(graph_context.RenderingScopeCount() == 1U);
    VR_CHECK(stats.pass_count == 2U);
    VR_CHECK(stats.rendering_scope_count == 1U);
    VR_CHECK(stats.command_batch_count == 2U);
    VR_CHECK(stats.queue_transfer_batch_count == 0U);

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

    auto compiled = builder.Compile();
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
    auto graph_context = vr::render_graph::GraphCommandContext{
        host.Context(),
        0U,
        command_buffer,
        compiled,
        table,
        host.RenderTarget(),
        nullptr,
        lowered,
        command_ready,
    };
    const auto stats = vr::render_graph::RenderGraphExecutor::Record(graph_context);
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
    auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
    create_info.platform.device.request_dynamic_rendering_local_read = true;
    try {
        host.Initialize(create_info);
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
    VR_CHECK(recorder.Stats().frame_packet_record_count == 0U);

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
    VR_CHECK(recorder.Stats().frame_packet_record_count == 0U);
    VR_CHECK(!compiled->Passes().empty());
    VR_CHECK(compiled->HasExecutablePasses());
    VR_CHECK(compiled->TransientAllocations().timeline.saved_bytes > 0U);
    VR_CHECK(compiled->TransientAllocations().timeline.page_count > 0U);
    const std::string compiled_debug = compiled->BuildDebugString();
    const std::string compiled_json = compiled->BuildJson();
    const auto topology_view =
        vr::render_graph::BuildCompiledRenderGraphTopologyView(*compiled);
    const std::string topology_json =
        vr::render_graph::BuildCompiledRenderGraphTopologyJson(topology_view);
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
    const auto& local_read_caps =
        host.Context().DynamicRenderingLocalReadCapsInfo();
    VR_CHECK(local_read_caps.requested);

    const auto& diagnostics = service.LastDiagnostics();
    const auto observability_view =
        vr::runtime::BuildRenderGraphObservabilityView(diagnostics);
    VR_CHECK(diagnostics.available);
    VR_CHECK(diagnostics.frame_compiled);
    VR_CHECK(diagnostics.compiled_pass_count >= 1U);
    VR_CHECK(diagnostics.logical_raster_pass_count ==
             compiled->NativePasses().summary.logical_raster_pass_count);
    VR_CHECK(diagnostics.native_pass_group_count ==
             compiled->NativePasses().summary.native_pass_group_count);
    VR_CHECK(diagnostics.fused_raster_pass_count ==
             compiled->NativePasses().summary.fused_raster_pass_count);
    VR_CHECK(diagnostics.store_elision_count ==
             compiled->NativePasses().summary.store_elision_count);
    VR_CHECK(diagnostics.load_inference_count ==
             compiled->NativePasses().summary.load_inference_count);
    VR_CHECK(diagnostics.effective_clear_attachment_count ==
             compiled->NativePasses().summary.clear_attachment_count);
    VR_CHECK(diagnostics.local_read_candidate_count ==
             compiled->NativePasses().summary.local_read_candidate_count);
    VR_CHECK(diagnostics.dynamic_rendering_local_read_supported ==
             compiled->NativePasses().local_read.supported);
    VR_CHECK(diagnostics.dynamic_rendering_local_read_requested ==
             compiled->NativePasses().local_read.requested);
    VR_CHECK(diagnostics.dynamic_rendering_local_read_enabled ==
             compiled->NativePasses().local_read.device_enabled);
    VR_CHECK(diagnostics.dynamic_rendering_local_read_supported ==
             local_read_caps.supported);
    VR_CHECK(compiled->NativePasses().local_read.requested ==
             local_read_caps.requested);
    VR_CHECK(diagnostics.dynamic_rendering_local_read_requested ==
             local_read_caps.requested);
    VR_CHECK(diagnostics.dynamic_rendering_local_read_status ==
             compiled->NativePasses().local_read.status);
    VR_CHECK(diagnostics.dynamic_rendering_local_read_reason ==
             compiled->NativePasses().local_read.reason);
    VR_CHECK(diagnostics.transient_saved_bytes == compiled->TransientAllocations().timeline.saved_bytes);
    VR_CHECK(diagnostics.transient_page_count == compiled->TransientAllocations().timeline.page_count);
    VR_CHECK(diagnostics.lazy_memory_requested_count > 0U);
    VR_CHECK(diagnostics.logical_raster_pass_count >=
             diagnostics.native_pass_group_count);
    VR_CHECK(compiled_debug.find("native_pass_store_elisions=") !=
             std::string::npos);
    VR_CHECK(compiled_debug.find("native_pass_effective_clears=") !=
             std::string::npos);
    VR_CHECK(compiled_debug.find("native_pass_local_read requested=1") !=
             std::string::npos);
    VR_CHECK(compiled_json.find("\"nativePassPlan\"") != std::string::npos);
    VR_CHECK(compiled_json.find("\"localRead\"") != std::string::npos);
    VR_CHECK(compiled_json.find("\"requested\": true") != std::string::npos);
    VR_CHECK(compiled_json.find("\"effectiveLoadOp\":") != std::string::npos);
    VR_CHECK(compiled_json.find("\"effectiveStoreOp\":") != std::string::npos);
    VR_CHECK(compiled_json.find("\"loadReason\":") != std::string::npos);
    VR_CHECK(compiled_json.find("\"storeReason\":") != std::string::npos);
    VR_CHECK(topology_view.summary.pass_count ==
             static_cast<std::uint32_t>(compiled->Passes().size()));
    VR_CHECK(topology_view.summary.queue_batch_count ==
             static_cast<std::uint32_t>(
                 compiled->PlannedBarriers().queue_batches.size()));
    VR_CHECK(topology_view.summary.queue_dependency_count ==
             static_cast<std::uint32_t>(
                 compiled->PlannedBarriers().queue_dependencies.size()));
    VR_CHECK(topology_view.summary.native_pass_group_count ==
             static_cast<std::uint32_t>(compiled->NativePasses().groups.size()));
    VR_CHECK(topology_json.find("\"nativePassTopology\"") !=
             std::string::npos);
    const std::string compiled_topology_fragment =
        std::string{"\"topology\": "} + topology_json;
    VR_CHECK(compiled_json.find("\"topology\": {") != std::string::npos);
    VR_CHECK(compiled_json.find(compiled_topology_fragment) !=
             std::string::npos);
    VR_CHECK(count_occurrences(compiled_json, "\"topologySummary\"") == 1U);
    VR_CHECK(count_occurrences(compiled_json, "\"nativePassTopology\"") ==
             1U);
    VR_REQUIRE(observability_view.compile.liveness_ranges != nullptr);
    VR_REQUIRE(observability_view.compile.transient_memory != nullptr);
    VR_CHECK(observability_view.compile.transient_memory->summary.saved_bytes ==
             compiled->TransientAllocations().timeline.saved_bytes);
    VR_CHECK(observability_view.compile.transient_memory->summary.page_count ==
             compiled->TransientAllocations().timeline.page_count);
    VR_CHECK(observability_view.compile.transient_memory->summary.alias_barrier_count ==
             static_cast<std::uint32_t>(
                 compiled->TransientAllocations().alias_barriers.size()));
    VR_CHECK(observability_view.compile.transient_memory->records.size() ==
             compiled->TransientAllocations().records.size());
    VR_CHECK(observability_view.compile.transient_memory->pages.size() ==
             compiled->TransientAllocations().pages.size());
    VR_CHECK(observability_view.compile.transient_memory->timeline_samples.size() ==
             compiled->TransientAllocations().timeline.samples.size());
    VR_CHECK(observability_view.compile.liveness_ranges->size() ==
             compiled->LivenessRanges().size());
    VR_CHECK(std::any_of(diagnostics.lazy_memory_resources.begin(),
                         diagnostics.lazy_memory_resources.end(),
                         [](const vr::runtime::RenderGraphLazyMemoryResourceDiagnostics& resource_) {
                             return resource_.requested;
                         }));
    VR_CHECK(std::all_of(diagnostics.lazy_memory_resources.begin(),
                         diagnostics.lazy_memory_resources.end(),
                         [](const vr::runtime::RenderGraphLazyMemoryResourceDiagnostics& resource_) {
                             return !resource_.requested ||
                                    resource_.realized ||
                                    !resource_.unavailable_reason.empty();
                         }));

    recorder.ClearFramePacket();
    host.Shutdown();
}

VR_TEST_CASE(RenderGraphRuntimeService_builds_temporal_resolve_stub_from_scene_recorder_3d_with_explicit_fallback_and_history_imports,
             "integration;render_graph;runtime;temporal;scene3d;vulkan") {
    Host host{};
    auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
    EnableRenderGraphRuntimeExecutionFeatures(create_info);
    create_info.modules.enable_descriptor_host = true;
    create_info.modules.enable_pipeline_host = true;
    create_info.modules.enable_sampler_host = true;
    try {
        host.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    recorder.BindRuntime(host);

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    camera.runtime.revision = 1U;
    camera.runtime.culling_mask = 0xFFU;
    vr::render::RenderView3D main_view{};
    vr::render::RenderScenePacket3D main_scene_packet{};
    vr::ecs::Transform<vr::ecs::Dim3> camera_transform{};
    vr::ecs::TransformSystem<vr::ecs::Dim3>::Initialize(camera_transform);
    vr::ecs::TransformSystem<vr::ecs::Dim3>::SetLocalPosition(
        camera_transform,
        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.0F});
    vr::ecs::TransformSystem<vr::ecs::Dim3>::UpdateHierarchy(&camera_transform, 1U);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::Initialize(camera);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetAspectRatio(camera, 320.0F / 180.0F);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetNearFar(camera, 0.05F, 256.0F);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetVerticalFovRadians(
        camera,
        60.0F * 0.01745329251994329577F);
    vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                       main_scene_packet,
                                                       camera,
                                                       camera_transform,
                                                       host.Swapchain().Extent(),
                                                       4200U);
    recorder.SetFramePacket(&main_scene_packet);

    host.EnsureSwapchainTargetsForFrame(0U, 0U);

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
    const auto make_context =
        [&](const std::uint32_t frame_index_, const VkCommandBuffer command_buffer_) {
            return MockPhaseContext{
                .frame_context =
                    {
                        .device = host.Context(),
                        .services = host.Services(),
                        .frame = {.frame_index = frame_index_, .image_index = 0U},
                        .progress = {.graphics_submitted = 0U, .graphics_completed = 0U},
                        .command_buffer = command_buffer_,
                        .swapchain_targets = &host.SwapchainTargets(),
                    },
            };
        };

    auto warmup_context = make_context(0U, VK_NULL_HANDLE);
    service.BeginFrame(warmup_context);
    host.PrepareTickFrame(recorder, 0U);
    service.PreRecord(warmup_context);

    const auto* warmup_snapshot = service.TryGetFrameSnapshot<vr::ecs::Dim3>();
    VR_REQUIRE(warmup_snapshot != nullptr);
    VR_CHECK(!warmup_snapshot->temporal.color.previous_available);
    VR_CHECK(!warmup_snapshot->temporal.depth.previous_available);
    VR_CHECK(!warmup_snapshot->temporal.motion.previous_available);
    VR_CHECK(warmup_snapshot->temporal.motion.current_writable);
    VR_CHECK(warmup_snapshot->temporal.reprojection.current_available);
    VR_CHECK(!warmup_snapshot->temporal.reprojection.previous_available);
    VR_CHECK(warmup_snapshot->temporal.jitter.current_available);
    VR_CHECK(!warmup_snapshot->temporal.jitter.previous_available);
    VR_CHECK(warmup_snapshot->temporal.jitter.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);
    const auto* compiled_warmup = service.TryGetCompiledGraph();
    VR_REQUIRE(compiled_warmup != nullptr);
    const auto warmup_motion_pass = std::find_if(
        compiled_warmup->Passes().begin(),
        compiled_warmup->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "temporal_motion_vector_pass";
        });
    VR_REQUIRE(warmup_motion_pass != compiled_warmup->Passes().end());
    const auto warmup_temporal_pass = std::find_if(
        compiled_warmup->Passes().begin(),
        compiled_warmup->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "temporal_history_resolve_stub";
        });
    VR_REQUIRE(warmup_temporal_pass != compiled_warmup->Passes().end());
    VR_CHECK(std::none_of(
        compiled_warmup->Resources().begin(),
        compiled_warmup->Resources().end(),
        [](const vr::render_graph::CompiledResource& resource_) {
            return resource_.debug_name == "temporal_previous_color" ||
                   resource_.debug_name == "temporal_previous_depth";
        }));
    VR_CHECK(std::any_of(
        compiled_warmup->Resources().begin(),
        compiled_warmup->Resources().end(),
        [](const vr::render_graph::CompiledResource& resource_) {
            return resource_.debug_name == "frame_motion_history_current" &&
                   resource_.lifetime == vr::render_graph::ResourceLifetime::imported;
        }));
    service.MarkGraphicsSubmissionEnqueued(
        vr::render::FrameToken{
            .frame_index = 0U,
            .graphics_signal_value = 1U,
        });

    host.EnsureSwapchainTargetsForFrame(1U, 0U);
    auto ready_context = make_context(1U, VK_NULL_HANDLE);
    service.BeginFrame(ready_context);
    host.PrepareTickFrame(recorder, 1U);
    service.PreRecord(ready_context);

    const auto* ready_snapshot = service.TryGetFrameSnapshot<vr::ecs::Dim3>();
    VR_REQUIRE(ready_snapshot != nullptr);
    VR_CHECK(ready_snapshot->temporal.color.previous_available);
    VR_CHECK(ready_snapshot->temporal.depth.previous_available);
    VR_CHECK(ready_snapshot->temporal.motion.previous_available);
    VR_CHECK(ready_snapshot->temporal.motion.current_writable);
    VR_CHECK(ready_snapshot->temporal.reprojection.current_available);
    VR_CHECK(ready_snapshot->temporal.reprojection.previous_available);
    VR_CHECK(ready_snapshot->temporal.jitter.current_available);
    VR_CHECK(ready_snapshot->temporal.jitter.previous_available);
    VR_CHECK(ready_snapshot->temporal.jitter.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    const auto* compiled_ready = service.TryGetCompiledGraph();
    VR_REQUIRE(compiled_ready != nullptr);
    const auto ready_motion_pass = std::find_if(
        compiled_ready->Passes().begin(),
        compiled_ready->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "temporal_motion_vector_pass";
        });
    VR_REQUIRE(ready_motion_pass != compiled_ready->Passes().end());
    const auto ready_temporal_pass = std::find_if(
        compiled_ready->Passes().begin(),
        compiled_ready->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "temporal_history_resolve_stub";
        });
    VR_REQUIRE(ready_temporal_pass != compiled_ready->Passes().end());
    VR_CHECK(std::any_of(
        compiled_ready->Resources().begin(),
        compiled_ready->Resources().end(),
        [](const vr::render_graph::CompiledResource& resource_) {
            return resource_.debug_name == "temporal_previous_color" &&
                   resource_.lifetime == vr::render_graph::ResourceLifetime::imported;
        }));
    VR_CHECK(std::any_of(
        compiled_ready->Resources().begin(),
        compiled_ready->Resources().end(),
        [](const vr::render_graph::CompiledResource& resource_) {
            return resource_.debug_name == "temporal_previous_depth" &&
                   resource_.lifetime == vr::render_graph::ResourceLifetime::imported;
        }));
    VR_CHECK(std::any_of(
        compiled_ready->Resources().begin(),
        compiled_ready->Resources().end(),
        [](const vr::render_graph::CompiledResource& resource_) {
            return resource_.debug_name == "frame_motion_history_current" &&
                   resource_.lifetime == vr::render_graph::ResourceLifetime::imported;
        }));

    recorder.ClearFramePacket();
    host.Shutdown();
}

VR_TEST_CASE(RenderGraphRuntimeService_builds_temporal_object_motion_overlay_when_scene_renderer_supports_temporal_motion,
             "integration;render_graph;runtime;temporal;scene3d;vulkan") {
    Host host{};
    auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
    EnableRenderGraphRuntimeExecutionFeatures(create_info);
    create_info.modules.enable_descriptor_host = true;
    create_info.modules.enable_pipeline_host = true;
    create_info.modules.enable_sampler_host = true;
    try {
        host.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    struct MockTemporalMotionRenderer final {
        void PrepareFrame(const vr::render::SceneRecorder3DPrepareView&) noexcept {}
        void RecordGraphSceneStage(vr::render_graph::GraphCommandContext&,
                                   vr::render::SceneRenderStage,
                                   vr::render_graph::ResourceHandle,
                                   vr::render_graph::ResourceHandle) {}
        void SetFrameTemporalMotionProducerState(
            const vr::render::SceneTemporalMotionProducerState& state_) noexcept {
            current_state_active = true;
            last_state = state_;
            if (configured_state_count < configured_states.size()) {
                configured_states[configured_state_count] = state_;
            }
            configured_state_count += 1U;
        }
        void ClearFrameTemporalMotionProducerState() noexcept {
            current_state_active = false;
            clear_state_count += 1U;
        }
        void DescribeGraphTemporalMotionBindings(
            vr::render_graph::RenderGraphBuilder&,
            vr::render_graph::PassHandle) noexcept {
            temporal_motion_binding_count += 1U;
        }
        void RegisterGraphImportedResources(
            vr::runtime::services::RenderGraphRuntimeService&) noexcept {
            imported_resource_registration_count += 1U;
        }
        void RecordGraphTemporalMotion(vr::render_graph::GraphCommandContext&,
                                       vr::render_graph::ResourceHandle,
                                       vr::render_graph::ResourceHandle) {}
        void OnSwapchainRecreated(std::uint32_t,
                                  VkExtent2D,
                                  VkFormat,
                                  std::uint64_t,
                                  std::uint64_t) noexcept {}

        std::uint32_t temporal_motion_binding_count = 0U;
        std::uint32_t imported_resource_registration_count = 0U;
        std::array<vr::render::SceneTemporalMotionProducerState, 4U>
            configured_states{};
        vr::render::SceneTemporalMotionProducerState last_state{};
        std::uint32_t configured_state_count = 0U;
        std::uint32_t clear_state_count = 0U;
        bool current_state_active = false;
    };

    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    recorder.BindRuntime(host);
    MockTemporalMotionRenderer renderer{};
    recorder.RegisterTransparentSceneRenderer(
        renderer,
        vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    camera.runtime.revision = 1U;
    camera.runtime.culling_mask = 0xFFU;
    vr::ecs::Transform<vr::ecs::Dim3> camera_transform{};
    vr::ecs::TransformSystem<vr::ecs::Dim3>::Initialize(camera_transform);
    vr::ecs::TransformSystem<vr::ecs::Dim3>::SetLocalPosition(
        camera_transform,
        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.0F});
    vr::ecs::TransformSystem<vr::ecs::Dim3>::UpdateHierarchy(
        &camera_transform,
        1U);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::Initialize(camera);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetAspectRatio(camera, 320.0F / 180.0F);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetNearFar(camera, 0.05F, 256.0F);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetVerticalFovRadians(
        camera,
        60.0F * 0.01745329251994329577F);
    vr::render::RenderView3D main_view{};
    vr::render::RenderScenePacket3D main_scene_packet{};
    vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                       main_scene_packet,
                                                       camera,
                                                       camera_transform,
                                                       host.Swapchain().Extent(),
                                                       4300U);
    recorder.SetFramePacket(&main_scene_packet);

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
    const auto make_context =
        [&](const std::uint32_t frame_index_) {
            return MockPhaseContext{
                .frame_context =
                    {
                        .device = host.Context(),
                        .services = host.Services(),
                        .frame = {.frame_index = frame_index_, .image_index = 0U},
                        .progress = {.graphics_submitted = 0U, .graphics_completed = 0U},
                        .command_buffer = VK_NULL_HANDLE,
                        .swapchain_targets = &host.SwapchainTargets(),
                    },
            };
        };

    host.EnsureSwapchainTargetsForFrame(0U, 0U);
    auto context_frame0 = make_context(0U);
    service.BeginFrame(context_frame0);
    host.PrepareTickFrame(recorder, 0U);
    service.PreRecord(context_frame0);

    const auto* compiled_warmup = service.TryGetCompiledGraph();
    VR_REQUIRE(compiled_warmup != nullptr);
    const auto warmup_overlay_pass = std::find_if(
        compiled_warmup->Passes().begin(),
        compiled_warmup->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "temporal_object_motion_overlay_pass";
        });
    VR_REQUIRE(warmup_overlay_pass != compiled_warmup->Passes().end());
    VR_REQUIRE(warmup_overlay_pass->raster_pass.has_value());
    VR_CHECK(warmup_overlay_pass->raster_pass->color_attachments[0].load_op ==
             vr::render_graph::AttachmentLoadOp::load);
    VR_CHECK(warmup_overlay_pass->raster_pass->has_depth_attachment);
    VR_CHECK(warmup_overlay_pass->raster_pass->depth_attachment.read_only);
    VR_CHECK(std::any_of(
        warmup_overlay_pass->reads.begin(),
        warmup_overlay_pass->reads.end(),
        [](const vr::render_graph::AccessDesc& access_) {
            return access_.access ==
                   vr::render_graph::AccessKind::color_attachment_read;
        }));
    VR_CHECK(std::any_of(
        warmup_overlay_pass->reads.begin(),
        warmup_overlay_pass->reads.end(),
        [](const vr::render_graph::AccessDesc& access_) {
            return access_.access ==
                   vr::render_graph::AccessKind::depth_stencil_read;
        }));
    VR_REQUIRE(warmup_overlay_pass->writes.size() == 1U);
    VR_CHECK(warmup_overlay_pass->writes[0].access ==
             vr::render_graph::AccessKind::color_attachment_write);
    VR_CHECK(renderer.temporal_motion_binding_count == 1U);
    VR_CHECK(renderer.imported_resource_registration_count == 2U);
    VR_CHECK(renderer.current_state_active);
    VR_CHECK(renderer.clear_state_count == 1U);
    VR_CHECK(renderer.configured_state_count == 1U);
    VR_CHECK(!renderer.configured_states[0U].previous_available);
    VR_CHECK(renderer.configured_states[0U].invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);

    service.MarkGraphicsSubmissionEnqueued(
        vr::render::FrameToken{
            .frame_index = 0U,
            .graphics_signal_value = 1U,
        });

    host.EnsureSwapchainTargetsForFrame(1U, 0U);
    auto context_frame1 = make_context(1U);
    service.BeginFrame(context_frame1);
    host.PrepareTickFrame(recorder, 1U);
    service.PreRecord(context_frame1);

    const auto* compiled_ready = service.TryGetCompiledGraph();
    VR_REQUIRE(compiled_ready != nullptr);
    VR_CHECK(std::any_of(
        compiled_ready->Passes().begin(),
        compiled_ready->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "temporal_object_motion_overlay_pass";
        }));
    VR_CHECK(renderer.current_state_active);
    VR_CHECK(renderer.clear_state_count == 2U);
    VR_CHECK(renderer.configured_state_count == 2U);
    VR_CHECK(renderer.configured_states[1U].previous_available);
    VR_CHECK(renderer.configured_states[1U].invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    VR_CHECK(renderer.configured_states[1U].previous_frame_index == 0U);
    VR_CHECK(renderer.configured_states[1U].previous_submission_id ==
             main_scene_packet.Metadata().submission_id);

    service.RequestFrameColorHistoryReset();
    service.MarkGraphicsSubmissionEnqueued(
        vr::render::FrameToken{
            .frame_index = 1U,
            .graphics_signal_value = 2U,
        });

    host.EnsureSwapchainTargetsForFrame(0U, 0U);
    auto context_frame2 = make_context(2U);
    service.BeginFrame(context_frame2);
    host.PrepareTickFrame(recorder, 0U);
    service.PreRecord(context_frame2);

    VR_CHECK(renderer.current_state_active);
    VR_CHECK(renderer.clear_state_count == 3U);
    VR_CHECK(renderer.configured_state_count == 3U);
    VR_CHECK(!renderer.configured_states[2U].previous_available);
    VR_CHECK(renderer.configured_states[2U].invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::reset_requested);

    recorder.ClearFramePacket();
    host.Shutdown();
}

VR_TEST_CASE(RenderGraphRuntimeService_skips_temporal_object_motion_overlay_when_scene_renderer_lacks_temporal_motion_producer,
             "integration;render_graph;runtime;temporal;scene3d;vulkan") {
    Host host{};
    auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
    EnableRenderGraphRuntimeExecutionFeatures(create_info);
    create_info.modules.enable_descriptor_host = true;
    create_info.modules.enable_pipeline_host = true;
    create_info.modules.enable_sampler_host = true;
    try {
        host.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    struct MockSceneRenderer final {
        void PrepareFrame(const vr::render::SceneRecorder3DPrepareView&) noexcept {}
        void RecordGraphSceneStage(vr::render_graph::GraphCommandContext&,
                                   vr::render::SceneRenderStage,
                                   vr::render_graph::ResourceHandle,
                                   vr::render_graph::ResourceHandle) {}
        void OnSwapchainRecreated(std::uint32_t,
                                  VkExtent2D,
                                  VkFormat,
                                  std::uint64_t,
                                  std::uint64_t) noexcept {}
    };

    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();
    recorder.BindRuntime(host);
    MockSceneRenderer renderer{};
    recorder.RegisterTransparentSceneRenderer(
        renderer,
        vr::render::SceneRenderPassRole::single);

    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    camera.style.viewport = {.origin_x = 0.0F, .origin_y = 0.0F, .width = 320.0F, .height = 180.0F};
    camera.runtime.revision = 1U;
    camera.runtime.culling_mask = 0xFFU;
    vr::ecs::Transform<vr::ecs::Dim3> camera_transform{};
    vr::ecs::TransformSystem<vr::ecs::Dim3>::Initialize(camera_transform);
    vr::ecs::TransformSystem<vr::ecs::Dim3>::SetLocalPosition(
        camera_transform,
        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.0F});
    vr::ecs::TransformSystem<vr::ecs::Dim3>::UpdateHierarchy(
        &camera_transform,
        1U);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::Initialize(camera);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetAspectRatio(camera, 320.0F / 180.0F);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetNearFar(camera, 0.05F, 256.0F);
    vr::ecs::CameraSystem<vr::ecs::Dim3>::SetVerticalFovRadians(
        camera,
        60.0F * 0.01745329251994329577F);
    vr::render::RenderView3D main_view{};
    vr::render::RenderScenePacket3D main_scene_packet{};
    vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                       main_scene_packet,
                                                       camera,
                                                       camera_transform,
                                                       host.Swapchain().Extent(),
                                                       4301U);
    recorder.SetFramePacket(&main_scene_packet);

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

    host.EnsureSwapchainTargetsForFrame(0U, 0U);
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
    service.BeginFrame(context);
    host.PrepareTickFrame(recorder, 0U);
    service.PreRecord(context);

    const auto* compiled = service.TryGetCompiledGraph();
    VR_REQUIRE(compiled != nullptr);
    VR_CHECK(std::none_of(
        compiled->Passes().begin(),
        compiled->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "temporal_object_motion_overlay_pass";
        }));
    VR_CHECK(std::any_of(
        compiled->Passes().begin(),
        compiled->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "temporal_motion_vector_pass";
        }));

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

VR_TEST_CASE(RenderGraphRuntimeService_temporal_history_warms_up_then_hands_off_and_invalidates_on_reset_or_resize,
             "integration;render_graph;runtime;temporal;vulkan") {
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

    const auto make_snapshot =
        [](const std::uint32_t width_,
           const std::uint32_t height_,
           const std::uint64_t frame_index_,
           const vr::render::SceneSubmissionId submission_id_) {
            vr::ecs::Camera<vr::ecs::Dim3> camera{};
            camera.style.viewport = {
                .origin_x = 0.0F,
                .origin_y = 0.0F,
                .width = static_cast<float>(width_),
                .height = static_cast<float>(height_),
            };
            camera.runtime.revision = static_cast<std::uint32_t>(frame_index_) + 1U;
            camera.runtime.culling_mask = 0xFFU;
            camera.runtime.view_projection_matrix.m[0] = 1.0F;
            camera.runtime.view_projection_matrix.m[5] = 1.0F;
            camera.runtime.view_projection_matrix.m[10] = 1.0F;
            camera.runtime.view_projection_matrix.m[15] = 1.0F;
            auto view = vr::render::MakeRenderViewFromCamera(
                camera,
                static_cast<const vr::ecs::Transform<vr::ecs::Dim3>*>(nullptr),
                vr::render::RenderViewKind::world,
                0U);
            auto packet =
                vr::render::MakeSingleViewScenePacket(view, submission_id_);
            return vr::render_graph::MakeFrameSnapshot(
                packet,
                frame_index_,
                vr::render_graph::Extent3D{
                    .width = width_,
                    .height = height_,
                    .depth = 1U,
                });
        };

    auto make_context =
        [&](const std::uint32_t frame_index_) {
            return MockPhaseContext{
                .frame_context =
                    {
                        .device = host.Context(),
                        .services = host.Services(),
                        .frame = {.frame_index = frame_index_, .image_index = 0U},
                        .progress = {.graphics_submitted = 0U, .graphics_completed = 0U},
                        .command_buffer = VK_NULL_HANDLE,
                        .swapchain_targets = &host.SwapchainTargets(),
                    },
            };
        };

    auto& service = host.Services().Get<RenderGraphRuntimeService>();

    auto context_frame4 = make_context(4U);
    service.BeginFrame(context_frame4);
    service.SetFrameSnapshot<vr::ecs::Dim3>(
        make_snapshot(256U, 144U, 4U, {410U}));
    service.PreRecord(context_frame4);

    const auto* warmup_snapshot =
        service.TryGetFrameSnapshot<vr::ecs::Dim3>();
    VR_REQUIRE(warmup_snapshot != nullptr);
    VR_CHECK(!warmup_snapshot->temporal.color.previous_available);
    VR_CHECK(warmup_snapshot->temporal.color.current_writable);
    VR_CHECK(warmup_snapshot->temporal.color.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);
    VR_CHECK(!warmup_snapshot->temporal.depth.previous_available);
    VR_CHECK(warmup_snapshot->temporal.depth.current_writable);
    VR_CHECK(warmup_snapshot->temporal.depth.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);
    VR_CHECK(!warmup_snapshot->temporal.motion.previous_available);
    VR_CHECK(warmup_snapshot->temporal.motion.current_writable);
    VR_CHECK(warmup_snapshot->temporal.motion.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);
    VR_CHECK(warmup_snapshot->temporal.reprojection.current_available);
    VR_CHECK(!warmup_snapshot->temporal.reprojection.previous_available);
    VR_CHECK(warmup_snapshot->temporal.reprojection.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);
    VR_CHECK(warmup_snapshot->temporal.jitter.current_available);
    VR_CHECK(!warmup_snapshot->temporal.jitter.previous_available);
    VR_CHECK(warmup_snapshot->temporal.jitter.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);
    VR_CHECK(vr::render::IsValidRenderTargetHandle(
        warmup_snapshot->temporal.color.current.handle));
    VR_CHECK(vr::render::IsValidRenderTargetHandle(
        warmup_snapshot->temporal.depth.current.handle));
    VR_CHECK(vr::render::IsValidRenderTargetHandle(
        warmup_snapshot->temporal.motion.current.handle));
    const auto warmup_current_handle =
        warmup_snapshot->temporal.color.current.handle;
    const auto warmup_current_revision =
        warmup_snapshot->temporal.color.current.resource_revision;
    const auto warmup_depth_handle =
        warmup_snapshot->temporal.depth.current.handle;
    const auto warmup_depth_revision =
        warmup_snapshot->temporal.depth.current.resource_revision;
    const auto warmup_motion_handle =
        warmup_snapshot->temporal.motion.current.handle;
    const auto warmup_motion_revision =
        warmup_snapshot->temporal.motion.current.resource_revision;

    const auto* compiled_warmup = service.TryGetCompiledGraph();
    VR_REQUIRE(compiled_warmup != nullptr);
    const auto history_capture_pass = std::find_if(
        compiled_warmup->Passes().begin(),
        compiled_warmup->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "frame_color_history_capture";
        });
    VR_REQUIRE(history_capture_pass != compiled_warmup->Passes().end());
    const auto history_import = std::find_if(
        compiled_warmup->Resources().begin(),
        compiled_warmup->Resources().end(),
        [](const vr::render_graph::CompiledResource& resource_) {
            return resource_.debug_name == "frame_color_history_current" &&
                   resource_.lifetime == vr::render_graph::ResourceLifetime::imported;
        });
    VR_REQUIRE(history_import != compiled_warmup->Resources().end());
    const auto depth_history_capture_pass = std::find_if(
        compiled_warmup->Passes().begin(),
        compiled_warmup->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "frame_depth_history_capture";
        });
    VR_REQUIRE(depth_history_capture_pass != compiled_warmup->Passes().end());
    const auto depth_history_import = std::find_if(
        compiled_warmup->Resources().begin(),
        compiled_warmup->Resources().end(),
        [](const vr::render_graph::CompiledResource& resource_) {
            return resource_.debug_name == "frame_depth_history_current" &&
                   resource_.lifetime == vr::render_graph::ResourceLifetime::imported;
        });
    VR_REQUIRE(depth_history_import != compiled_warmup->Resources().end());
    const auto motion_history_pass = std::find_if(
        compiled_warmup->Passes().begin(),
        compiled_warmup->Passes().end(),
        [](const vr::render_graph::CompiledPass& pass_) {
            return pass_.debug_name == "temporal_motion_vector_pass";
        });
    VR_CHECK(motion_history_pass == compiled_warmup->Passes().end());
    const auto motion_history_import = std::find_if(
        compiled_warmup->Resources().begin(),
        compiled_warmup->Resources().end(),
        [](const vr::render_graph::CompiledResource& resource_) {
            return resource_.debug_name == "frame_motion_history_current" &&
                   resource_.lifetime == vr::render_graph::ResourceLifetime::imported;
        });
    VR_CHECK(motion_history_import == compiled_warmup->Resources().end());

    service.MarkGraphicsSubmissionEnqueued(
        vr::render::FrameToken{
            .frame_index = 4U,
            .graphics_signal_value = 1U,
        });

    auto context_frame5 = make_context(5U);
    service.BeginFrame(context_frame5);
    service.SetFrameSnapshot<vr::ecs::Dim3>(
        make_snapshot(256U, 144U, 5U, {510U}));
    service.PreRecord(context_frame5);

    const auto* unavailable_snapshot =
        service.TryGetFrameSnapshot<vr::ecs::Dim3>();
    VR_REQUIRE(unavailable_snapshot != nullptr);
    VR_CHECK(unavailable_snapshot->temporal.color.previous_available);
    VR_CHECK(unavailable_snapshot->temporal.color.current_writable);
    VR_CHECK(unavailable_snapshot->temporal.color.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    VR_CHECK(unavailable_snapshot->temporal.color.previous.handle.index ==
             warmup_current_handle.index);
    VR_CHECK(unavailable_snapshot->temporal.color.previous.handle.generation ==
             warmup_current_handle.generation);
    VR_CHECK(unavailable_snapshot->temporal.color.previous.resource_revision ==
             warmup_current_revision);
    VR_CHECK(unavailable_snapshot->temporal.color.previous_submission_id ==
             vr::render::SceneSubmissionId{410U});
    VR_CHECK(unavailable_snapshot->temporal.color.previous_frame_index == 4U);
    VR_CHECK(unavailable_snapshot->temporal.depth.previous_available);
    VR_CHECK(unavailable_snapshot->temporal.depth.current_writable);
    VR_CHECK(unavailable_snapshot->temporal.depth.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    VR_CHECK(unavailable_snapshot->temporal.depth.previous.handle.index ==
             warmup_depth_handle.index);
    VR_CHECK(unavailable_snapshot->temporal.depth.previous.handle.generation ==
             warmup_depth_handle.generation);
    VR_CHECK(unavailable_snapshot->temporal.depth.previous.resource_revision ==
             warmup_depth_revision);
    VR_CHECK(unavailable_snapshot->temporal.depth.previous_submission_id ==
             vr::render::SceneSubmissionId{410U});
    VR_CHECK(unavailable_snapshot->temporal.depth.previous_frame_index == 4U);
    VR_CHECK(!unavailable_snapshot->temporal.motion.previous_available);
    VR_CHECK(unavailable_snapshot->temporal.motion.current_writable);
    VR_CHECK(unavailable_snapshot->temporal.motion.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::first_frame);
    VR_CHECK(unavailable_snapshot->temporal.reprojection.current_available);
    VR_CHECK(unavailable_snapshot->temporal.reprojection.previous_available);
    VR_CHECK(unavailable_snapshot->temporal.reprojection.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    VR_CHECK(unavailable_snapshot->temporal.reprojection.previous_submission_id ==
             vr::render::SceneSubmissionId{410U});
    VR_CHECK(unavailable_snapshot->temporal.reprojection.previous_frame_index == 4U);
    VR_CHECK(unavailable_snapshot->temporal.jitter.current_available);
    VR_CHECK(unavailable_snapshot->temporal.jitter.previous_available);
    VR_CHECK(unavailable_snapshot->temporal.jitter.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    VR_CHECK(unavailable_snapshot->temporal.jitter.previous_submission_id ==
             vr::render::SceneSubmissionId{410U});
    VR_CHECK(unavailable_snapshot->temporal.jitter.previous_frame_index == 4U);
    VR_CHECK(unavailable_snapshot->temporal.color.current.handle.index !=
                 unavailable_snapshot->temporal.color.previous.handle.index ||
             unavailable_snapshot->temporal.color.current.handle.generation !=
                 unavailable_snapshot->temporal.color.previous.handle.generation);

    service.QueueFrameMotionHistoryPublish(5U, {510U});

    service.MarkGraphicsSubmissionEnqueued(
        vr::render::FrameToken{
            .frame_index = 5U,
            .graphics_signal_value = 2U,
        });

    auto context_frame6 = make_context(6U);
    service.BeginFrame(context_frame6);
    service.SetFrameSnapshot<vr::ecs::Dim3>(
        make_snapshot(256U, 144U, 6U, {610U}));
    service.PreRecord(context_frame6);

    const auto* ready_snapshot =
        service.TryGetFrameSnapshot<vr::ecs::Dim3>();
    VR_REQUIRE(ready_snapshot != nullptr);
    VR_CHECK(ready_snapshot->temporal.color.previous_available);
    VR_CHECK(ready_snapshot->temporal.color.current_writable);
    VR_CHECK(ready_snapshot->temporal.color.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    VR_CHECK(ready_snapshot->temporal.depth.previous_available);
    VR_CHECK(ready_snapshot->temporal.depth.current_writable);
    VR_CHECK(ready_snapshot->temporal.depth.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    VR_CHECK(ready_snapshot->temporal.motion.previous_available);
    VR_CHECK(ready_snapshot->temporal.motion.current_writable);
    VR_CHECK(ready_snapshot->temporal.motion.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    VR_CHECK(ready_snapshot->temporal.motion.previous.handle.index ==
             warmup_motion_handle.index);
    VR_CHECK(ready_snapshot->temporal.motion.previous.handle.generation ==
             warmup_motion_handle.generation);
    VR_CHECK(ready_snapshot->temporal.motion.previous.resource_revision ==
             warmup_motion_revision);
    VR_CHECK(ready_snapshot->temporal.motion.previous_submission_id ==
             vr::render::SceneSubmissionId{510U});
    VR_CHECK(ready_snapshot->temporal.motion.previous_frame_index == 5U);
    VR_CHECK(ready_snapshot->temporal.reprojection.current_available);
    VR_CHECK(ready_snapshot->temporal.reprojection.previous_available);
    VR_CHECK(ready_snapshot->temporal.reprojection.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    VR_CHECK(ready_snapshot->temporal.reprojection.previous_submission_id ==
             vr::render::SceneSubmissionId{510U});
    VR_CHECK(ready_snapshot->temporal.reprojection.previous_frame_index == 5U);
    VR_CHECK(ready_snapshot->temporal.jitter.current_available);
    VR_CHECK(ready_snapshot->temporal.jitter.previous_available);
    VR_CHECK(ready_snapshot->temporal.jitter.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::none);
    VR_CHECK(ready_snapshot->temporal.jitter.previous_submission_id ==
             vr::render::SceneSubmissionId{510U});
    VR_CHECK(ready_snapshot->temporal.jitter.previous_frame_index == 5U);

    service.RequestFrameColorHistoryReset();
    service.MarkGraphicsSubmissionEnqueued(
        vr::render::FrameToken{
            .frame_index = 6U,
            .graphics_signal_value = 3U,
        });

    auto context_frame7 = make_context(7U);
    service.BeginFrame(context_frame7);
    service.SetFrameSnapshot<vr::ecs::Dim3>(
        make_snapshot(256U, 144U, 7U, {710U}));
    service.PreRecord(context_frame7);

    const auto* reset_snapshot =
        service.TryGetFrameSnapshot<vr::ecs::Dim3>();
    VR_REQUIRE(reset_snapshot != nullptr);
    VR_CHECK(!reset_snapshot->temporal.color.previous_available);
    VR_CHECK(reset_snapshot->temporal.color.current_writable);
    VR_CHECK(reset_snapshot->temporal.color.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::reset_requested);
    VR_CHECK(!reset_snapshot->temporal.depth.previous_available);
    VR_CHECK(reset_snapshot->temporal.depth.current_writable);
    VR_CHECK(reset_snapshot->temporal.depth.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::reset_requested);
    VR_CHECK(!reset_snapshot->temporal.motion.previous_available);
    VR_CHECK(reset_snapshot->temporal.motion.current_writable);
    VR_CHECK(reset_snapshot->temporal.motion.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::reset_requested);
    VR_CHECK(reset_snapshot->temporal.reprojection.current_available);
    VR_CHECK(!reset_snapshot->temporal.reprojection.previous_available);
    VR_CHECK(reset_snapshot->temporal.reprojection.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::reset_requested);
    VR_CHECK(reset_snapshot->temporal.jitter.current_available);
    VR_CHECK(!reset_snapshot->temporal.jitter.previous_available);
    VR_CHECK(reset_snapshot->temporal.jitter.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::reset_requested);

    service.MarkGraphicsSubmissionEnqueued(
        vr::render::FrameToken{
            .frame_index = 7U,
            .graphics_signal_value = 4U,
        });

    auto context_frame8 = make_context(8U);
    service.BeginFrame(context_frame8);
    service.SetFrameSnapshot<vr::ecs::Dim3>(
        make_snapshot(512U, 288U, 8U, {810U}));
    service.PreRecord(context_frame8);

    const auto* resized_snapshot =
        service.TryGetFrameSnapshot<vr::ecs::Dim3>();
    VR_REQUIRE(resized_snapshot != nullptr);
    VR_CHECK(!resized_snapshot->temporal.color.previous_available);
    VR_CHECK(resized_snapshot->temporal.color.current_writable);
    VR_CHECK(resized_snapshot->temporal.color.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::extent_changed);
    VR_CHECK(!resized_snapshot->temporal.depth.previous_available);
    VR_CHECK(resized_snapshot->temporal.depth.current_writable);
    VR_CHECK(resized_snapshot->temporal.depth.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::extent_changed);
    VR_CHECK(!resized_snapshot->temporal.motion.previous_available);
    VR_CHECK(resized_snapshot->temporal.motion.current_writable);
    VR_CHECK(resized_snapshot->temporal.motion.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::extent_changed);
    VR_CHECK(resized_snapshot->temporal.reprojection.current_available);
    VR_CHECK(!resized_snapshot->temporal.reprojection.previous_available);
    VR_CHECK(resized_snapshot->temporal.reprojection.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::extent_changed);
    VR_CHECK(resized_snapshot->temporal.jitter.current_available);
    VR_CHECK(!resized_snapshot->temporal.jitter.previous_available);
    VR_CHECK(resized_snapshot->temporal.jitter.invalidation_reason ==
             vr::render_graph::FrameHistoryInvalidationReason::extent_changed);
    VR_CHECK(resized_snapshot->temporal.color.current.handle.index ==
             warmup_current_handle.index);
    VR_CHECK(resized_snapshot->temporal.color.current.handle.generation ==
             warmup_current_handle.generation);
    VR_CHECK(resized_snapshot->temporal.color.current.resource_revision !=
             warmup_current_revision);
    VR_CHECK(resized_snapshot->temporal.depth.current.handle.index ==
             warmup_depth_handle.index);
    VR_CHECK(resized_snapshot->temporal.depth.current.handle.generation ==
             warmup_depth_handle.generation);
    VR_CHECK(resized_snapshot->temporal.depth.current.resource_revision !=
             warmup_depth_revision);
    VR_CHECK(resized_snapshot->temporal.motion.current.handle.index ==
             warmup_motion_handle.index);
    VR_CHECK(resized_snapshot->temporal.motion.current.handle.generation ==
             warmup_motion_handle.generation);
    VR_CHECK(resized_snapshot->temporal.motion.current.resource_revision !=
             warmup_motion_revision);
}

VR_TEST_CASE(RenderGraphRuntimeService_resolves_cross_queue_graph_to_multi_queue_or_graphics_fallback,
             "integration;render_graph;runtime;queue;vulkan") {
    Host host{};
    try {
        auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
        EnableRenderGraphRuntimeExecutionFeatures(create_info);
        host.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    const bool graph_execution_supported =
        host.Context().EnabledVulkan13Features().synchronization2 == VK_TRUE &&
        host.Context().EnabledVulkan13Features().dynamicRendering == VK_TRUE;
    const bool multi_queue_expected =
        graph_execution_supported &&
        HasOwnedTransferSubmitQueue(host.Context());
    const bool distinct_transfer_queue = HasDistinctTransferQueue(host.Context());

    host.EnsureSwapchainTargetsForFrame(0U, 0U);

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
    const VkCommandBuffer command_buffer = host.Context().BeginSingleTimeCommands();
    MockPhaseContext context{
        .frame_context = {
            .device = host.Context(),
            .services = host.Services(),
            .frame = {.frame_index = 0U, .image_index = 0U},
            .progress = {.graphics_submitted = 0U, .graphics_completed = 0U},
            .command_buffer = command_buffer,
            .swapchain_targets = &host.SwapchainTargets(),
        },
    };

    service.BeginFrame(context);
    service.SetDirectGraphBuildCallback(
        [](vr::render_graph::RenderGraphBuilder& builder_,
           const vr::render_graph::ResourceHandle present_target_,
           const vr::render_graph::Extent3D&,
           vr::render_graph::ResourceVersionHandle& present_ready_version_,
           const RenderGraphRuntimeService::ImportedTextureRegisterFn&) {
            const auto upload = builder_.AddPass("upload_present_target",
                                                 false,
                                                 vr::render_graph::QueueClass::transfer);
            builder_.SetExecuteCallback(upload,
                                        [](vr::render_graph::GraphCommandContext&) {});
            present_ready_version_ = builder_.Write(
                upload,
                present_target_,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::transfer_write,
                    .subresource_range = {
                        .base_mip_level = 0U,
                        .level_count = 1U,
                        .base_array_layer = 0U,
                        .layer_count = 1U,
                    },
                });
        });
    service.PreRecord(context);

    const auto& diagnostics = service.LastDiagnostics();
    VR_CHECK(diagnostics.transfer_queue_requested);
    VR_CHECK(!diagnostics.compute_queue_requested);
    VR_CHECK(diagnostics.multi_queue_requested);
    VR_CHECK(
        !vr::runtime::BuildRenderGraphQueueTimelineDebugString(diagnostics).empty());
    VR_CHECK(
        !vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics).empty());
    VR_CHECK(diagnostics.effective_queue_batch_count > 0U);
    VR_CHECK(multi_queue_expected);
    VR_CHECK(!diagnostics.graphics_fallback_active);
    VR_CHECK(diagnostics.transfer_queue_enabled);
    VR_CHECK(diagnostics.multi_queue_enabled);
    VR_CHECK(diagnostics.effective_queue_dependency_count > 0U);
    VR_CHECK(HasEffectiveQueueBatchWithPass(diagnostics,
                                           vr::render_graph::QueueClass::transfer,
                                           "upload_present_target"));
    VR_CHECK(HasEffectiveQueueBatchWithPass(diagnostics,
                                           vr::render_graph::QueueClass::graphics,
                                           "present_transition"));
    VR_CHECK(service.PlannedCommandReadyVulkanBarriers().queue_transfer_batches.size() ==
             (distinct_transfer_queue ? 1U : 0U));

    service.Record(context);
    host.Context().EndSingleTimeCommands(command_buffer);

    const auto& stats = service.LastRecordStats();
    if (graph_execution_supported) {
        VR_CHECK(stats.pass_count >= 1U);
        VR_CHECK(stats.command_batch_count >= 1U);
        VR_CHECK(stats.image_barrier_count >= 1U);
    } else {
        VR_CHECK(stats.pass_count == 0U);
        VR_CHECK(stats.command_batch_count == 0U);
        VR_CHECK(stats.image_barrier_count == 0U);
    }
    VR_CHECK(stats.queue_transfer_batch_count == (distinct_transfer_queue ? 1U : 0U));
}

VR_TEST_CASE(RenderGraphRuntimeService_falls_back_to_graphics_for_host_boundary_cross_queue_graph,
             "integration;render_graph;runtime;queue;fallback;vulkan") {
    Host host{};
    try {
        auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
        EnableRenderGraphRuntimeExecutionFeatures(create_info);
        host.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    const bool graph_execution_supported =
        host.Context().EnabledVulkan13Features().synchronization2 == VK_TRUE &&
        host.Context().EnabledVulkan13Features().dynamicRendering == VK_TRUE;

    host.EnsureSwapchainTargetsForFrame(0U, 0U);

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
    const VkCommandBuffer command_buffer = host.Context().BeginSingleTimeCommands();
    MockPhaseContext context{
        .frame_context = {
            .device = host.Context(),
            .services = host.Services(),
            .frame = {.frame_index = 0U, .image_index = 0U},
            .progress = {.graphics_submitted = 0U, .graphics_completed = 0U},
            .command_buffer = command_buffer,
            .swapchain_targets = &host.SwapchainTargets(),
        },
    };

    service.BeginFrame(context);
    service.SetDirectGraphBuildCallback(
        [](vr::render_graph::RenderGraphBuilder& builder_,
           const vr::render_graph::ResourceHandle present_target_,
           const vr::render_graph::Extent3D&,
           vr::render_graph::ResourceVersionHandle& present_ready_version_,
           const RenderGraphRuntimeService::ImportedTextureRegisterFn&) {
            const auto staging = builder_.CreateBuffer(
                "host_boundary_payload",
                vr::render_graph::BufferDesc{
                    .size_bytes = 256U,
                    .usage = vr::render_graph::buffer_usage_storage_flag |
                             vr::render_graph::buffer_usage_transfer_dst_flag,
                    .host_visible = true,
                });
            const auto upload = builder_.AddPass("upload_payload",
                                                 false,
                                                 vr::render_graph::QueueClass::transfer);
            const auto cpu_read = builder_.AddPass("cpu_read_payload",
                                                   true,
                                                   vr::render_graph::QueueClass::graphics);
            const auto present_prepare = builder_.AddPass("prepare_present_target",
                                                          false,
                                                          vr::render_graph::QueueClass::graphics);
            builder_.SetExecuteCallback(upload,
                                        [](vr::render_graph::GraphCommandContext&) {});
            builder_.SetExecuteCallback(cpu_read,
                                        [](vr::render_graph::GraphCommandContext&) {});
            builder_.SetExecuteCallback(present_prepare,
                                        [](vr::render_graph::GraphCommandContext&) {});

            const auto uploaded = builder_.Write(
                upload,
                staging,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::transfer_write,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            (void)builder_.Read(
                cpu_read,
                uploaded,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::host_read,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            (void)builder_.Read(
                present_prepare,
                uploaded,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::uniform_read,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            present_ready_version_ = builder_.Write(
                present_prepare,
                present_target_,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::transfer_write,
                    .subresource_range = {
                        .base_mip_level = 0U,
                        .level_count = 1U,
                        .base_array_layer = 0U,
                        .layer_count = 1U,
                    },
                });
        });
    service.PreRecord(context);

    const auto& diagnostics = service.LastDiagnostics();
    VR_CHECK(diagnostics.transfer_queue_requested);
    VR_CHECK(!diagnostics.compute_queue_requested);
    VR_CHECK(diagnostics.multi_queue_requested);
    VR_CHECK(diagnostics.graphics_fallback_active);
    VR_CHECK(!diagnostics.transfer_queue_enabled);
    VR_CHECK(!diagnostics.multi_queue_enabled);
    VR_CHECK(ContainsCaseInsensitive(diagnostics.queue_fallback_reason, "host boundary"));
    VR_CHECK(
        !vr::runtime::BuildRenderGraphQueueTimelineDebugString(diagnostics).empty());
    VR_CHECK(
        !vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics).empty());
    VR_CHECK(diagnostics.effective_queue_batch_count == 3U);
    VR_CHECK(diagnostics.effective_queue_dependency_count == 2U);
    VR_CHECK(AllEffectiveQueueBatchesUseQueue(
        diagnostics,
        vr::render_graph::QueueClass::graphics));
    VR_CHECK(std::any_of(diagnostics.effective_queue_batches.begin(),
                         diagnostics.effective_queue_batches.end(),
                         [](const vr::runtime::RenderGraphQueueBatchDiagnostics& batch_) {
                             return batch_.contains_host_boundary;
                         }));
    VR_CHECK(std::any_of(diagnostics.effective_queue_dependencies.begin(),
                         diagnostics.effective_queue_dependencies.end(),
                         [](const vr::runtime::RenderGraphQueueDependencyDiagnostics& dependency_) {
                             return dependency_.host_boundary;
                         }));
    VR_CHECK(service.PlannedCommandReadyVulkanBarriers().queue_transfer_batches.empty());

    service.Record(context);
    host.Context().EndSingleTimeCommands(command_buffer);

    const auto& stats = service.LastRecordStats();
    if (graph_execution_supported) {
        VR_CHECK(stats.pass_count >= 3U);
        VR_CHECK(stats.command_batch_count >= 1U);
        VR_CHECK(stats.image_barrier_count >= 1U);
    } else {
        VR_CHECK(stats.pass_count == 0U);
        VR_CHECK(stats.command_batch_count == 0U);
        VR_CHECK(stats.image_barrier_count == 0U);
    }
    VR_CHECK(stats.queue_transfer_batch_count == 0U);

    host.Shutdown();
}

VR_TEST_CASE(RenderGraphRuntimeService_runtime_tick_counters_only_keeps_aggregate_queue_diagnostics,
             "integration;render_graph;runtime;queue;diagnostics;vulkan") {
    Host host{};
    try {
        auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
        EnableRenderGraphRuntimeExecutionFeatures(create_info);
        host.Initialize(create_info);
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
        std::uint32_t legacy_record_count = 0U;

        void PrepareFrame(const vr::render::FrameComposerPrepareView&) noexcept {}
        void Record(const vr::render::FrameRecordContext&) noexcept {
            legacy_record_count += 1U;
        }
        void BuildRenderGraph(
            vr::render_graph::RenderGraphBuilder& builder_,
            const vr::render_graph::ResourceHandle present_target_,
            const vr::render_graph::Extent3D&,
            vr::render_graph::ResourceVersionHandle& present_ready_version_,
            const RenderGraphRuntimeService::ImportedTextureRegisterFn&) {
            const auto payload = builder_.CreateBuffer(
                "tick_counters_payload",
                vr::render_graph::BufferDesc{
                    .size_bytes = 256U,
                    .usage = vr::render_graph::buffer_usage_storage_flag |
                             vr::render_graph::buffer_usage_transfer_dst_flag,
                });
            const auto upload = builder_.AddPass("upload_payload",
                                                 false,
                                                 vr::render_graph::QueueClass::transfer);
            const auto compute = builder_.AddPass("simulate_payload",
                                                  false,
                                                  vr::render_graph::QueueClass::compute);
            const auto present_prepare = builder_.AddPass(
                "prepare_present_target",
                false,
                vr::render_graph::QueueClass::graphics);

            builder_.SetExecuteCallback(
                upload,
                [](vr::render_graph::GraphCommandContext&) {});
            builder_.SetExecuteCallback(
                compute,
                [](vr::render_graph::GraphCommandContext&) {});
            builder_.SetExecuteCallback(
                present_prepare,
                [](vr::render_graph::GraphCommandContext&) {});

            const auto uploaded = builder_.Write(
                upload,
                payload,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::transfer_write,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            (void)builder_.Read(
                compute,
                uploaded,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::shader_storage_read,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            const auto simulated = builder_.Write(
                compute,
                uploaded,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::shader_storage_write,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            (void)builder_.Read(
                present_prepare,
                simulated,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::uniform_read,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            present_ready_version_ = builder_.Write(
                present_prepare,
                present_target_,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::transfer_write,
                    .subresource_range = {
                        .base_mip_level = 0U,
                        .level_count = 1U,
                        .base_array_layer = 0U,
                        .layer_count = 1U,
                    },
                });
        }
    } recorder{};

    auto& service = host.Services().Get<RenderGraphRuntimeService>();
    Host::RuntimeTickResult last_tick{};
    std::uint32_t submitted_frames = 0U;
    for (std::uint32_t tick_index = 0U; tick_index < 4U && host.IsRunning(); ++tick_index) {
        last_tick = host.Tick(recorder);
        if (last_tick.render.code == vr::render::TickCode::Submitted ||
            last_tick.render.code == vr::render::TickCode::RecreateRequested) {
            ++submitted_frames;
        }
        SDL_Delay(1U);
    }

    const auto& diagnostics = service.LastDiagnostics();
    const auto queue_timeline_view =
        vr::runtime::BuildRenderGraphQueueTimelineView(diagnostics);
    const auto observability_view =
        vr::runtime::BuildRenderGraphObservabilityView(diagnostics);

    VR_CHECK(submitted_frames > 0U);
    VR_CHECK(recorder.legacy_record_count == 0U);
    VR_CHECK(diagnostics.multi_queue_enabled);
    VR_CHECK(diagnostics.effective_queue_batch_count == 3U);
    VR_CHECK(diagnostics.effective_queue_dependency_count == 2U);
    VR_CHECK(diagnostics.effective_graphics_queue_batch_count == 1U);
    VR_CHECK(diagnostics.effective_transfer_queue_batch_count == 1U);
    VR_CHECK(diagnostics.effective_compute_queue_batch_count == 1U);
    VR_CHECK(diagnostics.effective_owned_submit_batch_count == 2U);
    VR_CHECK(diagnostics.effective_cross_queue_dependency_count == 2U);
    VR_CHECK(diagnostics.effective_total_submit_wait_count > 0U);
    VR_CHECK(diagnostics.effective_total_submit_signal_count > 0U);
    VR_CHECK(diagnostics.effective_queue_batches.empty());
    VR_CHECK(diagnostics.effective_queue_dependencies.empty());
    VR_CHECK(diagnostics.lazy_memory_resources.empty());
    VR_CHECK(queue_timeline_view.available);
    VR_CHECK(queue_timeline_view.batch_count == 3U);
    VR_CHECK(queue_timeline_view.dependency_count == 2U);
    VR_CHECK(queue_timeline_view.graphics_batch_count == 1U);
    VR_CHECK(queue_timeline_view.transfer_batch_count == 1U);
    VR_CHECK(queue_timeline_view.compute_batch_count == 1U);
    VR_CHECK(queue_timeline_view.owned_submit_batch_count == 2U);
    VR_CHECK(queue_timeline_view.cross_queue_dependency_count == 2U);
    VR_CHECK(queue_timeline_view.total_submit_wait_count ==
             diagnostics.effective_total_submit_wait_count);
    VR_CHECK(queue_timeline_view.total_submit_signal_count ==
             diagnostics.effective_total_submit_signal_count);
    VR_CHECK(queue_timeline_view.batches.empty());
    VR_CHECK(queue_timeline_view.dependencies.empty());
    VR_CHECK(!vr::runtime::BuildRenderGraphQueueTimelineDebugString(diagnostics).empty());
    VR_CHECK(!vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics).empty());
    VR_CHECK(observability_view.runtime.graph_only_active);
    VR_CHECK(observability_view.submission.effective_queue_batch_count == 3U);
    VR_CHECK(observability_view.submission.effective_queue_batches->empty());
    VR_CHECK(last_tick.diagnostics.queues.transfer_submitted > 0U);
    VR_CHECK(last_tick.diagnostics.queues.compute_submitted > 0U);

    host.Shutdown();
}

VR_TEST_CASE(
    RenderGraphRuntimeService_runtime_tick_capture_level_exposes_canonical_timing_and_capture_contract,
    "integration;render_graph;runtime;timing;capture;diagnostics;vulkan") {
    Host host{};
    try {
        auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
        EnableRenderGraphRuntimeExecutionFeatures(create_info);
        create_info.diagnostics.level = vr::runtime::DiagnosticsLevel::Capture;
        host.Initialize(create_info);
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
        std::uint32_t legacy_record_count = 0U;

        void PrepareFrame(const vr::render::FrameComposerPrepareView&) noexcept {}
        void Record(const vr::render::FrameRecordContext&) noexcept {
            legacy_record_count += 1U;
        }
        void BuildRenderGraph(
            vr::render_graph::RenderGraphBuilder& builder_,
            const vr::render_graph::ResourceHandle present_target_,
            const vr::render_graph::Extent3D&,
            vr::render_graph::ResourceVersionHandle& present_ready_version_,
            const RenderGraphRuntimeService::ImportedTextureRegisterFn&) {
            const auto payload = builder_.CreateBuffer(
                "capture_payload",
                vr::render_graph::BufferDesc{
                    .size_bytes = 256U,
                    .usage = vr::render_graph::buffer_usage_storage_flag |
                             vr::render_graph::buffer_usage_transfer_dst_flag,
                });
            const auto upload = builder_.AddPass("upload_payload",
                                                 false,
                                                 vr::render_graph::QueueClass::transfer);
            const auto compute = builder_.AddPass("simulate_payload",
                                                  false,
                                                  vr::render_graph::QueueClass::compute);
            const auto present_prepare = builder_.AddPass(
                "prepare_present_target",
                false,
                vr::render_graph::QueueClass::graphics);

            builder_.SetExecuteCallback(
                upload,
                [](vr::render_graph::GraphCommandContext&) {});
            builder_.SetExecuteCallback(
                compute,
                [](vr::render_graph::GraphCommandContext&) {});
            builder_.SetExecuteCallback(
                present_prepare,
                [](vr::render_graph::GraphCommandContext&) {});

            const auto uploaded = builder_.Write(
                upload,
                payload,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::transfer_write,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            (void)builder_.Read(
                compute,
                uploaded,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::shader_storage_read,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            const auto simulated = builder_.Write(
                compute,
                uploaded,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::shader_storage_write,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            (void)builder_.Read(
                present_prepare,
                simulated,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::uniform_read,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            present_ready_version_ = builder_.Write(
                present_prepare,
                present_target_,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::transfer_write,
                    .subresource_range = {
                        .base_mip_level = 0U,
                        .level_count = 1U,
                        .base_array_layer = 0U,
                        .layer_count = 1U,
                    },
                });
        }
    } recorder{};

    auto& service = host.Services().Get<RenderGraphRuntimeService>();
    Host::RuntimeTickResult last_tick{};
    std::uint32_t submitted_frames = 0U;
    for (std::uint32_t tick_index = 0U; tick_index < 4U && host.IsRunning(); ++tick_index) {
        last_tick = host.Tick(recorder);
        if (last_tick.render.code == vr::render::TickCode::Submitted ||
            last_tick.render.code == vr::render::TickCode::RecreateRequested) {
            ++submitted_frames;
        }
        SDL_Delay(1U);
    }

    const auto& diagnostics = service.LastDiagnostics();
    const auto observability_view =
        vr::runtime::BuildRenderGraphObservabilityView(diagnostics);

    VR_CHECK(submitted_frames > 0U);
    VR_CHECK(recorder.legacy_record_count == 0U);
    if (vr::runtime::RuntimeDiagnosticsAvailableInBuild()) {
        VR_REQUIRE(last_tick.diagnostics.collected);
        VR_CHECK(last_tick.diagnostics.level == vr::runtime::DiagnosticsLevel::Capture);
        VR_CHECK(diagnostics.timing.available);
        VR_CHECK(diagnostics.timing.enabled);
        VR_CHECK(!diagnostics.timing.gpu_timestamp_supported);
        VR_CHECK(diagnostics.timing.domain ==
                 vr::runtime::RenderGraphTimingDomain::cpu_record);
        VR_CHECK(diagnostics.timing.queue_batch_range_count ==
                 diagnostics.effective_queue_batch_count);
        VR_CHECK(diagnostics.timing.resolved_queue_batch_range_count ==
                 diagnostics.timing.queue_batch_ranges.size());
        VR_CHECK(diagnostics.timing.total_duration_ns > 0U);
        VR_CHECK(diagnostics.capture.available);
        VR_CHECK(diagnostics.capture.enabled);
        VR_CHECK(diagnostics.capture.marker_count ==
                 diagnostics.capture.markers.size());
        VR_CHECK(diagnostics.capture.marker_count ==
                 diagnostics.timing.queue_batch_ranges.size());
        VR_CHECK(diagnostics.capture.artifact_count == 1U);
        VR_REQUIRE(observability_view.timing.queue_batch_ranges != nullptr);
        VR_REQUIRE(observability_view.capture.markers != nullptr);
        VR_REQUIRE(observability_view.capture.artifacts != nullptr);
        VR_CHECK(observability_view.capture.artifacts->size() == 1U);
        VR_CHECK(std::any_of(
            diagnostics.timing.queue_batch_ranges.begin(),
            diagnostics.timing.queue_batch_ranges.end(),
            [](const vr::runtime::RenderGraphQueueBatchTimingDiagnostics& range_) {
                return range_.resolved &&
                       ContainsCaseInsensitive(range_.marker_label, "upload_payload");
            }));
        VR_CHECK(std::any_of(
            diagnostics.timing.queue_batch_ranges.begin(),
            diagnostics.timing.queue_batch_ranges.end(),
            [](const vr::runtime::RenderGraphQueueBatchTimingDiagnostics& range_) {
                return range_.resolved &&
                       ContainsCaseInsensitive(range_.marker_label, "simulate_payload");
            }));
        VR_CHECK(std::any_of(
            diagnostics.timing.queue_batch_ranges.begin(),
            diagnostics.timing.queue_batch_ranges.end(),
            [](const vr::runtime::RenderGraphQueueBatchTimingDiagnostics& range_) {
                return range_.resolved &&
                       ContainsCaseInsensitive(range_.marker_label, "prepare_present_target");
            }));

        const auto* compiled = service.TryGetCompiledGraph();
        VR_REQUIRE(compiled != nullptr);
        const auto topology_view =
            vr::render_graph::BuildCompiledRenderGraphTopologyView(*compiled);
        const auto& artifact = diagnostics.capture.artifacts.front();
        VR_CHECK(artifact.captured);
        VR_CHECK(artifact.kind ==
                 vr::runtime::RenderGraphCaptureArtifactKind::observability_snapshot);
        VR_CHECK(artifact.topology_json ==
                 vr::render_graph::BuildCompiledRenderGraphTopologyJson(topology_view));
        VR_CHECK(artifact.queue_timeline_json ==
                 vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics));
        VR_CHECK(last_tick.diagnostics.render_graph.capture.artifact_count == 1U);
    } else {
        VR_CHECK(!last_tick.diagnostics.collected);
        VR_CHECK(last_tick.diagnostics.level == vr::runtime::DiagnosticsLevel::Off);
        VR_CHECK(!diagnostics.timing.enabled);
        VR_CHECK(diagnostics.timing.queue_batch_ranges.empty());
        VR_CHECK(!diagnostics.capture.enabled);
        VR_CHECK(diagnostics.capture.markers.empty());
        VR_CHECK(diagnostics.capture.artifacts.empty());
        VR_CHECK(observability_view.timing.queue_batch_ranges == nullptr);
        VR_CHECK(observability_view.capture.markers == nullptr);
        VR_CHECK(observability_view.capture.artifacts == nullptr);
    }

    host.Shutdown();
}

VR_TEST_CASE(RenderGraphRuntimeService_submits_cross_queue_graph_during_runtime_tick,
             "integration;render_graph;runtime;queue;submit;vulkan") {
    Host host{};
    try {
        auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
        EnableRenderGraphRuntimeExecutionFeatures(create_info);
        create_info.diagnostics.level = vr::runtime::DiagnosticsLevel::Detailed;
        host.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    const bool graph_execution_supported =
        host.Context().EnabledVulkan13Features().synchronization2 == VK_TRUE &&
        host.Context().EnabledVulkan13Features().dynamicRendering == VK_TRUE;
    const bool distinct_transfer_queue = HasDistinctTransferQueue(host.Context());
    const bool distinct_compute_queue = HasDistinctComputeQueue(host.Context());
    if (!graph_execution_supported) {
        host.Shutdown();
        VR_SKIP("synchronization2 or dynamicRendering feature unavailable");
    }

    struct TickRecorder final {
        std::uint32_t legacy_record_count = 0U;

        void PrepareFrame(const vr::render::FrameComposerPrepareView&) noexcept {}
        void Record(const vr::render::FrameRecordContext&) noexcept {
            legacy_record_count += 1U;
        }
        void BuildRenderGraph(
            vr::render_graph::RenderGraphBuilder& builder_,
            const vr::render_graph::ResourceHandle present_target_,
            const vr::render_graph::Extent3D&,
            vr::render_graph::ResourceVersionHandle& present_ready_version_,
            const RenderGraphRuntimeService::ImportedTextureRegisterFn&) {
            const auto payload = builder_.CreateBuffer(
                "tick_cross_queue_payload",
                vr::render_graph::BufferDesc{
                    .size_bytes = 256U,
                    .usage = vr::render_graph::buffer_usage_storage_flag |
                             vr::render_graph::buffer_usage_transfer_dst_flag,
                });
            const auto upload = builder_.AddPass("upload_payload",
                                                 false,
                                                 vr::render_graph::QueueClass::transfer);
            const auto simulate = builder_.AddPass("simulate_payload",
                                                   false,
                                                   vr::render_graph::QueueClass::compute);
            const auto present_prepare = builder_.AddPass("prepare_present_target",
                                                          false,
                                                          vr::render_graph::QueueClass::graphics);
            builder_.SetExecuteCallback(upload,
                                        [](vr::render_graph::GraphCommandContext&) {});
            builder_.SetExecuteCallback(simulate,
                                        [](vr::render_graph::GraphCommandContext&) {});
            builder_.SetExecuteCallback(present_prepare,
                                        [](vr::render_graph::GraphCommandContext&) {});

            const auto uploaded = builder_.Write(
                upload,
                payload,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::transfer_write,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            (void)builder_.Read(
                simulate,
                uploaded,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::shader_storage_read,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            const auto simulated = builder_.Write(
                simulate,
                uploaded,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::shader_storage_write,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            (void)builder_.Read(
                present_prepare,
                simulated,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::uniform_read,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            present_ready_version_ = builder_.Write(
                present_prepare,
                present_target_,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::transfer_write,
                    .subresource_range = {
                        .base_mip_level = 0U,
                        .level_count = 1U,
                        .base_array_layer = 0U,
                        .layer_count = 1U,
                    },
                });
        }
    } recorder{};

    auto& service = host.Services().Get<RenderGraphRuntimeService>();
    Host::RuntimeTickResult last_tick{};
    std::uint32_t submitted_frames = 0U;
    constexpr std::uint32_t max_ticks = 4U;
    for (std::uint32_t tick_index = 0U; tick_index < max_ticks && host.IsRunning(); ++tick_index) {
        last_tick = host.Tick(recorder);
        if (last_tick.render.code == vr::render::TickCode::Submitted ||
            last_tick.render.code == vr::render::TickCode::RecreateRequested) {
            ++submitted_frames;
        }
        SDL_Delay(1U);
    }

    VR_CHECK(submitted_frames > 0U);
    VR_CHECK(recorder.legacy_record_count == 0U);
    VR_CHECK(service.LastDiagnostics().multi_queue_enabled);
    VR_CHECK(!service.LastDiagnostics().graphics_fallback_active);
    VR_CHECK(!vr::runtime::BuildRenderGraphQueueTimelineDebugString(
                  service.LastDiagnostics())
                  .empty());
    VR_CHECK(!vr::runtime::BuildRenderGraphQueueTimelineJson(
                  service.LastDiagnostics())
                  .empty());
    VR_CHECK(service.LastDiagnostics().effective_queue_batch_count == 3U);
    VR_CHECK(service.LastDiagnostics().effective_queue_dependency_count == 2U);
    VR_CHECK(HasEffectiveQueueBatchWithPass(service.LastDiagnostics(),
                                           vr::render_graph::QueueClass::transfer,
                                           "upload_payload"));
    VR_CHECK(HasEffectiveQueueBatchWithPass(service.LastDiagnostics(),
                                           vr::render_graph::QueueClass::compute,
                                           "simulate_payload"));
    VR_CHECK(HasEffectiveQueueBatchWithPass(service.LastDiagnostics(),
                                           vr::render_graph::QueueClass::graphics,
                                           "prepare_present_target"));
    VR_CHECK(service.LastDiagnostics().graphics_submit_wait_count > 0U);
    VR_CHECK(service.LastDiagnostics().non_graphics_submit_batch_count == 2U);
    VR_CHECK(service.LastRecordStats().queue_transfer_batch_count ==
             ((distinct_transfer_queue || distinct_compute_queue) ? 2U : 0U));
    VR_CHECK(service.TransferSubmittedValue() > 0U);
    VR_CHECK(service.ComputeSubmittedValue() > 0U);
    VR_CHECK(last_tick.diagnostics.queues.transfer_submitted > 0U);
    VR_CHECK(last_tick.diagnostics.queues.compute_submitted > 0U);
    VR_CHECK(last_tick.diagnostics.queues.transfer_completed <=
             last_tick.diagnostics.queues.transfer_submitted);
    VR_CHECK(last_tick.diagnostics.queues.compute_completed <=
             last_tick.diagnostics.queues.compute_submitted);

    host.Shutdown();
}

VR_TEST_CASE(RenderGraphRuntimeService_submits_multi_graphics_compute_graph_during_runtime_tick,
             "integration;render_graph;runtime;queue;submit;vulkan") {
    Host host{};
    try {
        auto create_info = MakeMinimalRenderTargetRuntimeCreateInfo();
        EnableRenderGraphRuntimeExecutionFeatures(create_info);
        create_info.diagnostics.level = vr::runtime::DiagnosticsLevel::Detailed;
        host.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    const bool graph_execution_supported =
        host.Context().EnabledVulkan13Features().synchronization2 == VK_TRUE &&
        host.Context().EnabledVulkan13Features().dynamicRendering == VK_TRUE;
    const bool distinct_compute_queue = HasDistinctComputeQueue(host.Context());
    if (!graph_execution_supported) {
        host.Shutdown();
        VR_SKIP("synchronization2 or dynamicRendering feature unavailable");
    }

    struct TickRecorder final {
        std::uint32_t legacy_record_count = 0U;

        void PrepareFrame(const vr::render::FrameComposerPrepareView&) noexcept {}
        void Record(const vr::render::FrameRecordContext&) noexcept {
            legacy_record_count += 1U;
        }
        void BuildRenderGraph(
            vr::render_graph::RenderGraphBuilder& builder_,
            const vr::render_graph::ResourceHandle present_target_,
            const vr::render_graph::Extent3D&,
            vr::render_graph::ResourceVersionHandle& present_ready_version_,
            const RenderGraphRuntimeService::ImportedTextureRegisterFn&) {
            const auto payload = builder_.CreateBuffer(
                "multi_graphics_payload",
                vr::render_graph::BufferDesc{
                    .size_bytes = 256U,
                    .usage = vr::render_graph::buffer_usage_storage_flag |
                             vr::render_graph::buffer_usage_uniform_flag,
                });
            const auto scene_prepare = builder_.AddPass("scene_prepare",
                                                        false,
                                                        vr::render_graph::QueueClass::graphics);
            const auto simulate = builder_.AddPass("simulate_payload",
                                                   false,
                                                   vr::render_graph::QueueClass::compute);
            const auto present_prepare = builder_.AddPass("prepare_present_target",
                                                          false,
                                                          vr::render_graph::QueueClass::graphics);
            builder_.SetExecuteCallback(scene_prepare,
                                        [](vr::render_graph::GraphCommandContext&) {});
            builder_.SetExecuteCallback(simulate,
                                        [](vr::render_graph::GraphCommandContext&) {});
            builder_.SetExecuteCallback(present_prepare,
                                        [](vr::render_graph::GraphCommandContext&) {});

            const auto prepared = builder_.Write(
                scene_prepare,
                payload,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::shader_storage_write,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            (void)builder_.Read(
                simulate,
                prepared,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::shader_storage_read,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            const auto simulated = builder_.Write(
                simulate,
                payload,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::shader_storage_write,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            (void)builder_.Read(
                present_prepare,
                simulated,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::uniform_read,
                    .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
                });
            present_ready_version_ = builder_.Write(
                present_prepare,
                present_target_,
                vr::render_graph::AccessDesc{
                    .access = vr::render_graph::AccessKind::transfer_write,
                    .subresource_range = {
                        .base_mip_level = 0U,
                        .level_count = 1U,
                        .base_array_layer = 0U,
                        .layer_count = 1U,
                    },
                });
        }
    } recorder{};

    auto& service = host.Services().Get<RenderGraphRuntimeService>();
    Host::RuntimeTickResult last_tick{};
    std::uint32_t submitted_frames = 0U;
    constexpr std::uint32_t max_ticks = 4U;
    for (std::uint32_t tick_index = 0U; tick_index < max_ticks && host.IsRunning(); ++tick_index) {
        last_tick = host.Tick(recorder);
        if (last_tick.render.code == vr::render::TickCode::Submitted ||
            last_tick.render.code == vr::render::TickCode::RecreateRequested) {
            ++submitted_frames;
        }
        SDL_Delay(1U);
    }

    VR_CHECK(submitted_frames > 0U);
    VR_CHECK(recorder.legacy_record_count == 0U);
    VR_CHECK(service.LastDiagnostics().multi_queue_enabled);
    VR_CHECK(!service.LastDiagnostics().graphics_fallback_active);
    VR_CHECK(!vr::runtime::BuildRenderGraphQueueTimelineDebugString(
                  service.LastDiagnostics())
                  .empty());
    VR_CHECK(!vr::runtime::BuildRenderGraphQueueTimelineJson(
                  service.LastDiagnostics())
                  .empty());
    VR_CHECK(service.LastDiagnostics().effective_queue_batch_count == 3U);
    VR_CHECK(service.LastDiagnostics().effective_queue_dependency_count == 2U);
    VR_CHECK(HasEffectiveQueueBatchWithPass(service.LastDiagnostics(),
                                           vr::render_graph::QueueClass::graphics,
                                           "scene_prepare"));
    VR_CHECK(HasEffectiveQueueBatchWithPass(service.LastDiagnostics(),
                                           vr::render_graph::QueueClass::compute,
                                           "simulate_payload"));
    VR_CHECK(HasEffectiveQueueBatchWithPass(service.LastDiagnostics(),
                                           vr::render_graph::QueueClass::graphics,
                                           "prepare_present_target"));
    VR_CHECK(service.LastDiagnostics().graphics_submit_wait_count > 0U);
    VR_CHECK(service.LastDiagnostics().non_graphics_submit_batch_count == 2U);
    VR_CHECK(service.LastRecordStats().queue_transfer_batch_count ==
             (distinct_compute_queue ? 2U : 0U));
    VR_CHECK(service.ComputeSubmittedValue() > 0U);
    VR_CHECK(last_tick.diagnostics.queues.compute_submitted > 0U);
    VR_CHECK(last_tick.diagnostics.queues.compute_completed <=
             last_tick.diagnostics.queues.compute_submitted);

    const auto owned_graphics_batch = std::find_if(
        service.LastDiagnostics().effective_queue_batches.begin(),
        service.LastDiagnostics().effective_queue_batches.end(),
        [](const vr::runtime::RenderGraphQueueBatchDiagnostics& batch_) {
            return batch_.queue == vr::render_graph::QueueClass::graphics &&
                   batch_.submitted_on_owned_queue &&
                   std::find(batch_.pass_debug_names.begin(),
                             batch_.pass_debug_names.end(),
                             "scene_prepare") != batch_.pass_debug_names.end();
        });
    VR_CHECK(owned_graphics_batch != service.LastDiagnostics().effective_queue_batches.end());

    host.Shutdown();
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
    VR_CHECK(executed_graph_frames > 0U);
    VR_CHECK(recorder.legacy_record_count == 0U);

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

    auto compiled = builder.Compile();

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
    auto compiled = builder.Compile();
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
    auto compiled = builder.Compile();

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
    auto compiled = builder.Compile();

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
    auto compiled = builder.Compile();

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
