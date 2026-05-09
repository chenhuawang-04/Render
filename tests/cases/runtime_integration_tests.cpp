#include "support/test_framework.hpp"
#include "vr/render/render_target_desc.hpp"
#include "vr/render/render_target_format_utils.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;

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

class ClearToPresentRecorder final {
public:
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_) {
        (void)extent_;
        (void)format_;

        image_initialized.resize(image_count_);
        for (auto& flag : image_initialized) {
            flag = 0U;
        }
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        const std::uint32_t previous_size = static_cast<std::uint32_t>(image_initialized.size());
        if (record_context_.image_index >= previous_size) {
            image_initialized.resize(record_context_.image_index + 1U);
            for (std::uint32_t i = previous_size; i < image_initialized.size(); ++i) {
                image_initialized[i] = 0U;
            }
        }

        const bool initialized = image_initialized[record_context_.image_index] != 0U;

        VkImageMemoryBarrier barrier_to_transfer{};
        barrier_to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_to_transfer.srcAccessMask = initialized ? VK_ACCESS_MEMORY_READ_BIT : 0U;
        barrier_to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_to_transfer.oldLayout = initialized
            ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier_to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_to_transfer.image = record_context_.image;
        barrier_to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier_to_transfer.subresourceRange.baseMipLevel = 0U;
        barrier_to_transfer.subresourceRange.levelCount = 1U;
        barrier_to_transfer.subresourceRange.baseArrayLayer = 0U;
        barrier_to_transfer.subresourceRange.layerCount = 1U;

        vkCmdPipelineBarrier(record_context_.command_buffer,
                             initialized
                                 ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                                 : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0U,
                             0U,
                             nullptr,
                             0U,
                             nullptr,
                             1U,
                             &barrier_to_transfer);

        VkClearColorValue clear_color{};
        clear_color.float32[0] = 0.12F;
        clear_color.float32[1] = 0.18F;
        clear_color.float32[2] = 0.27F;
        clear_color.float32[3] = 1.0F;

        VkImageSubresourceRange clear_range{};
        clear_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clear_range.baseMipLevel = 0U;
        clear_range.levelCount = 1U;
        clear_range.baseArrayLayer = 0U;
        clear_range.layerCount = 1U;

        vkCmdClearColorImage(record_context_.command_buffer,
                             record_context_.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear_color,
                             1U,
                             &clear_range);

        VkImageMemoryBarrier barrier_to_present{};
        barrier_to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_to_present.dstAccessMask = 0U;
        barrier_to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier_to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_to_present.image = record_context_.image;
        barrier_to_present.subresourceRange = clear_range;

        vkCmdPipelineBarrier(record_context_.command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0U,
                             0U,
                             nullptr,
                             0U,
                             nullptr,
                             1U,
                             &barrier_to_present);

        image_initialized[record_context_.image_index] = 1U;
    }

private:
    vr::McVector<std::uint8_t> image_initialized{};
};

[[nodiscard]] VkFormat SelectTransientColorFormat(vr::VulkanContext& context_) {
    constexpr std::array<VkFormat, 3U> candidates{
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT
    };
    for (const VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), candidate, &properties);
        constexpr VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) == required) {
            return candidate;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkFormat ResolveDepthTargetFormat(vr::VulkanContext& context_) {
    constexpr std::array<VkFormat, 3U> candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT
    };
    return vr::render::ResolveFirstSupportedDepthStencilFormat(context_, candidates);
}

class TransientPoolRecorder final {
public:
    void PrepareFrame(const vr::render::SceneRecorder2DPrepareView& prepare_view_) {
        if (prepare_view_.render_target_pool == nullptr) {
            return;
        }

        if (selected_format == VK_FORMAT_UNDEFINED) {
            selected_format = SelectTransientColorFormat(prepare_view_.device);
            if (selected_format == VK_FORMAT_UNDEFINED) {
                return;
            }
        }

        vr::render::RenderTargetDesc desc{};
        desc.debug_name = "TransientPoolRecorderColor";
        desc.dimension = vr::render::RenderTargetDimension::image_2d;
        desc.lifetime = vr::render::RenderTargetLifetime::transient;
        desc.scale_mode = vr::render::RenderTargetScaleMode::absolute;
        desc.width = 96U;
        desc.height = 64U;
        desc.depth = 1U;
        desc.format = selected_format;
        desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        desc.color_encoding = vr::render::RenderTargetColorEncoding::linear;

        const auto acquire_result = prepare_view_.render_target_pool->AcquireTransientTarget(
            prepare_view_.device,
            prepare_view_.render_target,
            desc);
        acquired_handles.push_back(acquire_result.handle);
        reused_count += acquire_result.reused ? 1U : 0U;
        created_count += acquire_result.created ? 1U : 0U;
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        clear_recorder.Record(record_context_);
    }

    [[nodiscard]] std::uint32_t ReusedCount() const noexcept {
        return reused_count;
    }

    [[nodiscard]] std::uint32_t CreatedCount() const noexcept {
        return created_count;
    }

    [[nodiscard]] std::uint32_t AcquiredCount() const noexcept {
        return static_cast<std::uint32_t>(acquired_handles.size());
    }

private:
    ClearToPresentRecorder clear_recorder{};
    vr::McVector<vr::render::RenderTargetHandle> acquired_handles{};
    VkFormat selected_format = VK_FORMAT_UNDEFINED;
    std::uint32_t reused_count = 0U;
    std::uint32_t created_count = 0U;
};

class SwapchainLifecycleRecorder final {
public:
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t,
                              std::uint64_t) noexcept {
        recreate_count += 1U;
        last_image_count = image_count_;
        last_extent = extent_;
        last_format = format_;
        clear_recorder.OnSwapchainRecreated(image_count_, extent_, format_);
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        clear_recorder.Record(record_context_);
    }

    std::uint32_t recreate_count = 0U;
    std::uint32_t last_image_count = 0U;
    VkExtent2D last_extent{};
    VkFormat last_format = VK_FORMAT_UNDEFINED;

private:
    ClearToPresentRecorder clear_recorder{};
};

class UploadCopyRecorder final {
public:
    explicit UploadCopyRecorder(std::uint32_t payload_bytes_ = 4096U)
        : payload_bytes(payload_bytes_) {
        payload.resize(payload_bytes);
        for (std::uint32_t i = 0U; i < payload.size(); ++i) {
            payload[i] = static_cast<std::uint8_t>(i & 0xFFU);
        }
    }

    void SetDestinationBuffer(VkBuffer destination_buffer_) noexcept {
        destination_buffer = destination_buffer_;
    }

    void PrepareFrame(const vr::render::SceneRecorder2DPrepareView& prepare_view_) {
        prepare_count += 1U;
        if (prepare_view_.upload == nullptr || destination_buffer == VK_NULL_HANDLE) {
            return;
        }

        prepare_view_.upload->StageAndRecordCopyBuffer(prepare_view_.frame.frame_index,
                                                       destination_buffer,
                                                       0U,
                                                       payload.data(),
                                                       payload.size(),
                                                       16U);
        upload_record_count += 1U;
        last_frame_index = prepare_view_.frame.frame_index;
    }

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_) {
        clear_recorder.OnSwapchainRecreated(image_count_, extent_, format_);
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        clear_recorder.Record(record_context_);
    }

    [[nodiscard]] std::uint32_t PayloadBytes() const noexcept {
        return payload_bytes;
    }

    std::uint32_t prepare_count = 0U;
    std::uint32_t upload_record_count = 0U;
    std::uint32_t last_frame_index = 0U;

private:
    ClearToPresentRecorder clear_recorder{};
    vr::McVector<std::uint8_t> payload{};
    VkBuffer destination_buffer = VK_NULL_HANDLE;
    std::uint32_t payload_bytes = 0U;
};

class ColorDepthFinalStateRecorder final {
public:
    void PrepareFrame(const vr::render::SceneRecorder2DPrepareView& prepare_view_) {
        ++prepare_count;
        render_target_host = &prepare_view_.render_target;
        context = &prepare_view_.device;

        if (color_format == VK_FORMAT_UNDEFINED) {
            color_format = SelectTransientColorFormat(*context);
        }
        if (depth_format == VK_FORMAT_UNDEFINED) {
            depth_format = ResolveDepthTargetFormat(*context);
        }
        if (color_format == VK_FORMAT_UNDEFINED || depth_format == VK_FORMAT_UNDEFINED) {
            return;
        }

        if (!vr::render::IsValidRenderTargetHandle(color_target)) {
            vr::render::RenderTargetDesc color_desc{};
            color_desc.debug_name = "RuntimeIntegrationColorDepthFinalStateColor";
            color_desc.dimension = vr::render::RenderTargetDimension::image_2d;
            color_desc.lifetime = vr::render::RenderTargetLifetime::persistent;
            color_desc.scale_mode = vr::render::RenderTargetScaleMode::absolute;
            color_desc.width = 128U;
            color_desc.height = 96U;
            color_desc.depth = 1U;
            color_desc.format = color_format;
            color_desc.samples = VK_SAMPLE_COUNT_1_BIT;
            color_desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            color_desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            color_desc.color_encoding = vr::render::RenderTargetColorEncoding::linear;
            color_target = render_target_host->CreatePersistentTarget(*context, color_desc);
        }

        if (!vr::render::IsValidRenderTargetHandle(depth_target)) {
            vr::render::RenderTargetDesc depth_desc{};
            depth_desc.debug_name = "RuntimeIntegrationColorDepthFinalStateDepth";
            depth_desc.dimension = vr::render::RenderTargetDimension::image_2d;
            depth_desc.lifetime = vr::render::RenderTargetLifetime::persistent;
            depth_desc.scale_mode = vr::render::RenderTargetScaleMode::absolute;
            depth_desc.width = 128U;
            depth_desc.height = 96U;
            depth_desc.depth = 1U;
            depth_desc.format = depth_format;
            depth_desc.samples = VK_SAMPLE_COUNT_1_BIT;
            depth_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            depth_desc.aspect = vr::render::DepthStencilAspectMask(depth_format);
            depth_target = render_target_host->CreatePersistentTarget(*context, depth_desc);
        }
    }

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_) {
        clear_recorder.OnSwapchainRecreated(image_count_, extent_, format_);
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        if (render_target_host != nullptr &&
            vr::render::IsValidRenderTargetHandle(color_target) &&
            vr::render::IsValidRenderTargetHandle(depth_target)) {
            VkClearColorValue clear_color{};
            clear_color.float32[0] = 0.25F;
            clear_color.float32[1] = 0.10F;
            clear_color.float32[2] = 0.35F;
            clear_color.float32[3] = 1.0F;

            vr::render::RenderTargetColorOutputConfig color_output{};
            color_output.color_target = color_target;
            color_output.final_state = vr::render::RenderTargetStateKind::shader_read;
            color_output.use_explicit_load_op = true;
            color_output.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_output.store_op = VK_ATTACHMENT_STORE_OP_STORE;
            color_output.clear_color = clear_color;

            vr::render::RenderTargetDepthOutputConfig depth_output{};
            depth_output.depth_target = depth_target;
            depth_output.final_state = vr::render::RenderTargetStateKind::depth_read_only;
            depth_output.use_explicit_load_op = true;
            depth_output.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth_output.store_op = VK_ATTACHMENT_STORE_OP_STORE;
            depth_output.stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depth_output.stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth_output.clear_depth_stencil = VkClearDepthStencilValue{1.0F, 0U};

            vr::render::ResolvedColorRenderPass pass = vr::render::BuildColorDepthRenderPass(
                record_context_,
                color_output,
                depth_output,
                false,
                clear_color,
                false,
                false);
            vkCmdBeginRendering(record_context_.command_buffer, pass.rendering_info.VkInfoPtr());
            vkCmdEndRendering(record_context_.command_buffer);
            vr::render::RecordEndColorDepthPass(record_context_, color_output, depth_output);

            color_state_after_record = render_target_host->ResolveView(color_target).state;
            depth_state_after_record = render_target_host->ResolveView(depth_target).state;
            ++record_count;
        }

        clear_recorder.Record(record_context_);
    }

    vr::render::RenderTargetHandle color_target{};
    vr::render::RenderTargetHandle depth_target{};
    vr::render::RenderTargetStateKind color_state_after_record =
        vr::render::RenderTargetStateKind::undefined;
    vr::render::RenderTargetStateKind depth_state_after_record =
        vr::render::RenderTargetStateKind::undefined;
    std::uint32_t prepare_count = 0U;
    std::uint32_t record_count = 0U;

private:
    ClearToPresentRecorder clear_recorder{};
    vr::render::RenderTargetHost* render_target_host = nullptr;
    vr::VulkanContext* context = nullptr;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
};

VR_TEST_CASE(RuntimeIntegration_initialize_tick_shutdown_smoke, "integration;gpu;sdl;runtime") {
    Runtime runtime{};
    Runtime::CreateInfo create_info{};
    create_info.platform.window.title = "vr_tests_runtime_smoke";
    create_info.platform.window.width = 320;
    create_info.platform.window.height = 240;
    create_info.platform.window.resizable = false;
    create_info.platform.window.high_pixel_density = false;

    create_info.platform.instance.enable_validation = false;
    create_info.render_loop.swapchain.enable_vsync = false;
    create_info.render_loop.swapchain.preferred_image_count = 2U;
    create_info.poll_events_each_tick = true;

    ClearToPresentRecorder recorder{};
    try {
        runtime.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    VR_REQUIRE(runtime.IsInitialized());
    VR_REQUIRE(runtime.IsRunning());

    std::uint32_t submitted_frames = 0U;
    constexpr std::uint32_t max_ticks = 8U;
    for (std::uint32_t tick_index = 0U;
         tick_index < max_ticks && runtime.IsRunning();
         ++tick_index) {
        const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
        if (tick_result.render.code == vr::render::TickCode::Submitted ||
            tick_result.render.code == vr::render::TickCode::RecreateRequested) {
            ++submitted_frames;
        }
        SDL_Delay(1U);
    }

    runtime.Shutdown();
    VR_CHECK(!runtime.IsInitialized());
    VR_CHECK(submitted_frames > 0U);
}

VR_TEST_CASE(RuntimeIntegration_render_target_pool_reuses_transient_targets, "integration;gpu;sdl;runtime;render_target") {
    Runtime runtime{};
    Runtime::CreateInfo create_info{};
    create_info.platform.window.title = "vr_tests_runtime_transient_pool";
    create_info.platform.window.width = 320;
    create_info.platform.window.height = 240;
    create_info.platform.window.resizable = false;
    create_info.platform.window.high_pixel_density = false;
    create_info.platform.instance.enable_validation = false;
    create_info.render_loop.swapchain.enable_vsync = false;
    create_info.render_loop.swapchain.preferred_image_count = 2U;
    create_info.poll_events_each_tick = true;

    TransientPoolRecorder recorder{};
    try {
        runtime.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    VR_REQUIRE(runtime.HasRenderTargetHost());
    VR_REQUIRE(runtime.HasRenderTargetPool());

    std::uint32_t submitted_frames = 0U;
    constexpr std::uint32_t max_ticks = 10U;
    for (std::uint32_t tick_index = 0U;
         tick_index < max_ticks && runtime.IsRunning();
         ++tick_index) {
        const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
        if (tick_result.render.code == vr::render::TickCode::Submitted ||
            tick_result.render.code == vr::render::TickCode::RecreateRequested) {
            ++submitted_frames;
        }
        SDL_Delay(1U);
    }

    const auto pool_stats = runtime.TargetPool().Stats();
    runtime.Shutdown();

    VR_CHECK(submitted_frames > 0U);
    VR_CHECK(recorder.AcquiredCount() > 0U);
    VR_CHECK(recorder.CreatedCount() > 0U);
    VR_CHECK(recorder.ReusedCount() > 0U);
    VR_CHECK(pool_stats.bucket_count >= 1U);
}

VR_TEST_CASE(RuntimeIntegration_render_target_pass_end_color_depth_transitions_apply_final_states,
             "integration;gpu;sdl;runtime;render_target") {
    Runtime runtime{};
    Runtime::CreateInfo create_info{};
    create_info.platform.window.title = "vr_tests_runtime_render_target_pass_end_depth_transition";
    create_info.platform.window.width = 320;
    create_info.platform.window.height = 240;
    create_info.platform.window.resizable = false;
    create_info.platform.window.high_pixel_density = false;
    create_info.platform.instance.enable_validation = false;
    create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
    create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
    create_info.render_loop.swapchain.enable_vsync = false;
    create_info.poll_events_each_tick = true;

    ColorDepthFinalStateRecorder recorder{};
    try {
        runtime.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    VR_REQUIRE(runtime.HasRenderTargetHost());
    const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);

    VR_CHECK(tick_result.running);
    VR_CHECK(recorder.prepare_count >= 1U);
    VR_CHECK(recorder.record_count >= 1U);
    VR_CHECK(vr::render::IsValidRenderTargetHandle(recorder.color_target));
    VR_CHECK(vr::render::IsValidRenderTargetHandle(recorder.depth_target));
    VR_CHECK(recorder.color_state_after_record == vr::render::RenderTargetStateKind::shader_read);
    VR_CHECK(recorder.depth_state_after_record == vr::render::RenderTargetStateKind::depth_read_only);
    VR_CHECK(runtime.RenderTarget().ResolveView(recorder.color_target).state ==
             vr::render::RenderTargetStateKind::shader_read);
    VR_CHECK(runtime.RenderTarget().ResolveView(recorder.depth_target).state ==
             vr::render::RenderTargetStateKind::depth_read_only);

    runtime.Shutdown();
}

VR_TEST_CASE(RuntimeIntegration_frame_diagnostics_capture_swapchain_state,
             "integration;gpu;sdl;runtime;diagnostics") {
    Runtime runtime{};
    Runtime::CreateInfo create_info{};
    create_info.platform.window.title = "vr_tests_runtime_diagnostics";
    create_info.platform.window.width = 320;
    create_info.platform.window.height = 240;
    create_info.platform.window.resizable = false;
    create_info.platform.window.high_pixel_density = false;
    create_info.platform.instance.enable_validation = false;
    create_info.render_loop.swapchain.enable_vsync = false;
    create_info.diagnostics.level = vr::runtime::DiagnosticsLevel::CountersOnly;

    ClearToPresentRecorder recorder{};
    try {
        runtime.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
    runtime.Shutdown();

    VR_REQUIRE(tick_result.diagnostics.collected);
    VR_CHECK(tick_result.diagnostics.level == vr::runtime::DiagnosticsLevel::CountersOnly);
    VR_CHECK(tick_result.diagnostics.frame.frame_id > 0U);
    VR_CHECK(tick_result.diagnostics.swapchain.valid);
    VR_CHECK(tick_result.diagnostics.swapchain.generation > 0U);
    VR_CHECK(tick_result.diagnostics.swapchain.image_count >= 2U);
    VR_CHECK(tick_result.diagnostics.swapchain.extent.width == 320U);
    VR_CHECK(tick_result.diagnostics.swapchain.extent.height == 240U);
    VR_CHECK(tick_result.diagnostics.frame.frame_index == tick_result.render.frame_index);
    VR_CHECK(tick_result.diagnostics.frame.image_index == tick_result.render.image_index);
    VR_CHECK(tick_result.diagnostics.commands.frame_slot_count == 2U);
    VR_CHECK(tick_result.diagnostics.queues.graphics_submitted >=
             tick_result.diagnostics.queues.graphics_completed);
}

VR_TEST_CASE(RuntimeIntegration_frame_diagnostics_off_skips_collection,
             "integration;gpu;sdl;runtime;diagnostics") {
    Runtime runtime{};
    Runtime::CreateInfo create_info{};
    create_info.platform.window.title = "vr_tests_runtime_diagnostics_off";
    create_info.platform.window.width = 320;
    create_info.platform.window.height = 240;
    create_info.platform.window.resizable = false;
    create_info.platform.window.high_pixel_density = false;
    create_info.platform.instance.enable_validation = false;
    create_info.render_loop.swapchain.enable_vsync = false;
    create_info.diagnostics.level = vr::runtime::DiagnosticsLevel::Off;

    ClearToPresentRecorder recorder{};
    try {
        runtime.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
    runtime.Shutdown();

    VR_CHECK(!tick_result.diagnostics.collected);
    VR_CHECK(tick_result.diagnostics.level == vr::runtime::DiagnosticsLevel::Off);
}

VR_TEST_CASE(RuntimeIntegration_swapchain_mark_dirty_notifies_recorder,
             "integration;gpu;sdl;runtime;swapchain") {
    Runtime runtime{};
    Runtime::CreateInfo create_info{};
    create_info.platform.window.title = "vr_tests_runtime_swapchain_recreate";
    create_info.platform.window.width = 320;
    create_info.platform.window.height = 240;
    create_info.platform.window.resizable = true;
    create_info.platform.window.high_pixel_density = false;
    create_info.platform.instance.enable_validation = false;
    create_info.render_loop.swapchain.enable_vsync = false;

    SwapchainLifecycleRecorder recorder{};
    try {
        runtime.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    (void)runtime.Tick(recorder);
    const std::uint32_t recreate_count_before_dirty = recorder.recreate_count;
    runtime.Swapchain().MarkDirty();
    (void)runtime.Tick(recorder);
    runtime.Shutdown();

    VR_CHECK(recorder.recreate_count >= recreate_count_before_dirty + 1U);
    VR_CHECK(recorder.last_image_count >= 2U);
    VR_CHECK(recorder.last_extent.width == 320U);
    VR_CHECK(recorder.last_extent.height == 240U);
    VR_CHECK(recorder.last_format != VK_FORMAT_UNDEFINED);
}

VR_TEST_CASE(RuntimeIntegration_cross_queue_upload_reports_extra_wait_when_available,
             "integration;gpu;sdl;runtime;upload") {
    Runtime runtime{};
    Runtime::CreateInfo create_info{};
    create_info.platform.window.title = "vr_tests_runtime_upload_cross_queue";
    create_info.platform.window.width = 320;
    create_info.platform.window.height = 240;
    create_info.platform.window.resizable = false;
    create_info.platform.window.high_pixel_density = false;
    create_info.platform.instance.enable_validation = false;
    create_info.render_loop.swapchain.enable_vsync = false;
    create_info.diagnostics.level = vr::runtime::DiagnosticsLevel::CountersOnly;

    UploadCopyRecorder recorder{};
    vr::resource::BufferResource destination_buffer{};

    try {
        runtime.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    if (!runtime.Upload().UsesCrossQueueSubmit()) {
        runtime.Shutdown();
        VR_SKIP("No dedicated transfer queue available for cross-queue upload test.");
    }

    vr::resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = recorder.PayloadBytes();
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    destination_buffer = runtime.CreateBuffer(buffer_create_info);
    recorder.SetDestinationBuffer(destination_buffer.buffer);

    const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
    const std::uint64_t upload_submitted_value = runtime.Upload().LastSubmittedValue();
    const std::uint64_t upload_completed_value = runtime.Upload().CompletedSubmitValue();
    runtime.DestroyBuffer(destination_buffer);
    runtime.Shutdown();

    VR_CHECK(tick_result.upload_submitted);
    VR_CHECK(tick_result.upload_cross_queue_wait);
    VR_REQUIRE(tick_result.diagnostics.collected);
    VR_CHECK(tick_result.diagnostics.frame.upload_submitted == tick_result.upload_submitted);
    VR_CHECK(tick_result.diagnostics.frame.upload_cross_queue_wait);
    VR_CHECK(tick_result.diagnostics.queues.transfer_submitted == upload_submitted_value);
    VR_CHECK(tick_result.diagnostics.queues.transfer_completed == upload_completed_value);
    VR_CHECK(tick_result.diagnostics.queues.transfer_submitted > 0U);
    VR_CHECK(tick_result.diagnostics.upload.buffer_copy_count > 0U);
}

VR_TEST_CASE(RuntimeIntegration_upload_staging_page_growth_handles_large_copy,
             "integration;gpu;sdl;runtime;upload") {
    Runtime runtime{};
    Runtime::CreateInfo create_info{};
    create_info.platform.window.title = "vr_tests_runtime_upload_growth";
    create_info.platform.window.width = 320;
    create_info.platform.window.height = 240;
    create_info.platform.window.resizable = false;
    create_info.platform.window.high_pixel_density = false;
    create_info.platform.instance.enable_validation = false;
    create_info.render_loop.swapchain.enable_vsync = false;
    create_info.diagnostics.level = vr::runtime::DiagnosticsLevel::Detailed;
    create_info.upload.staging_buffer_size = 256U;
    create_info.upload.max_staging_page_count = 4U;
    create_info.upload.allow_staging_page_growth = true;

    UploadCopyRecorder recorder{1024U};
    vr::resource::BufferResource destination_buffer{};

    try {
        runtime.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    vr::resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = recorder.PayloadBytes();
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    destination_buffer = runtime.CreateBuffer(buffer_create_info);
    recorder.SetDestinationBuffer(destination_buffer.buffer);

    const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
    runtime.DestroyBuffer(destination_buffer);
    runtime.Shutdown();

    VR_REQUIRE(tick_result.diagnostics.collected);
    VR_CHECK(tick_result.diagnostics.upload.buffer_copy_count > 0U);
    VR_CHECK(tick_result.diagnostics.upload.staging_page_count >= 2U);
    VR_CHECK(tick_result.diagnostics.upload.staging_page_growth_count >= 1U);
    VR_CHECK(tick_result.diagnostics.upload.capacity_bytes >= recorder.PayloadBytes());
    VR_CHECK(tick_result.diagnostics.allocations.upload_staging_page_growth_count >= 1U);
    VR_CHECK(tick_result.diagnostics.allocations.upload_capacity_bytes >= recorder.PayloadBytes());
}

VR_TEST_CASE(RuntimeIntegration_upload_staging_exhaustion_reports_capacity_details,
             "integration;gpu;sdl;runtime;upload") {
    Runtime runtime{};
    Runtime::CreateInfo create_info{};
    create_info.platform.window.title = "vr_tests_runtime_upload_exhausted";
    create_info.platform.window.width = 320;
    create_info.platform.window.height = 240;
    create_info.platform.window.resizable = false;
    create_info.platform.window.high_pixel_density = false;
    create_info.platform.instance.enable_validation = false;
    create_info.render_loop.swapchain.enable_vsync = false;
    create_info.upload.staging_buffer_size = 256U;
    create_info.upload.max_staging_page_count = 1U;
    create_info.upload.allow_staging_page_growth = false;

    UploadCopyRecorder recorder{1024U};
    vr::resource::BufferResource destination_buffer{};

    try {
        runtime.Initialize(create_info);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }

    vr::resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = recorder.PayloadBytes();
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    destination_buffer = runtime.CreateBuffer(buffer_create_info);
    recorder.SetDestinationBuffer(destination_buffer.buffer);

    bool saw_expected_exception = false;
    try {
        (void)runtime.Tick(recorder);
    } catch (const std::exception& exception_) {
        saw_expected_exception =
            ContainsCaseInsensitive(exception_.what(), "UploadHost staging capacity exhausted") &&
            ContainsCaseInsensitive(exception_.what(), "page_count=");
    }

    runtime.DestroyBuffer(destination_buffer);
    runtime.Shutdown();
    VR_CHECK(saw_expected_exception);
}

} // namespace
