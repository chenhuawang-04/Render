#include "support/test_framework.hpp"
#include "vr/render/render_target_desc.hpp"
#include "vr/render/render_runtime_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
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
    constexpr std::array<std::string_view, 13U> patterns{
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
        "vkgetphysicaldevicesurfacepresentmodeskhr"
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

class TransientPoolRecorder final {
public:
    void PrepareFrame(const vr::render::RuntimePrepareContext& prepare_context_) {
        if (prepare_context_.render_target_pool == nullptr ||
            prepare_context_.render_target_host == nullptr ||
            prepare_context_.context == nullptr) {
            return;
        }

        if (selected_format == VK_FORMAT_UNDEFINED) {
            selected_format = SelectTransientColorFormat(*prepare_context_.context);
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

        const auto acquire_result = prepare_context_.render_target_pool->AcquireTransientTarget(
            *prepare_context_.context,
            *prepare_context_.render_target_host,
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

} // namespace
