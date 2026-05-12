#include "vr/runtime/crash_tracer_support.hpp"
#include "vr/render/render_runtime_host.hpp"

#include <iostream>

namespace {

class ClearToPresentRecorder final {
public:
    void OnSwapchainRecreated(uint32_t image_count_, VkExtent2D extent_, VkFormat format_) {
        (void)extent_;
        (void)format_;

        image_initialized.resize(image_count_);
        for (auto& initialized_flag : image_initialized) {
            initialized_flag = 0U;
        }
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        (void)record_context_.frame_index;
        (void)record_context_.format;
        (void)record_context_.image_view;
        (void)record_context_.extent;

        const uint32_t previous_size = static_cast<uint32_t>(image_initialized.size());
        if (record_context_.image_index >= previous_size) {
            image_initialized.resize(record_context_.image_index + 1U);
            for (uint32_t i = previous_size; i < image_initialized.size(); ++i) {
                image_initialized[i] = 0U;
            }
        }

        const bool initialized = image_initialized[record_context_.image_index] != 0U;

        VkImageMemoryBarrier barrier_to_clear{};
        barrier_to_clear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_to_clear.srcAccessMask = initialized ? VK_ACCESS_MEMORY_READ_BIT : 0U;
        barrier_to_clear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_to_clear.oldLayout = initialized ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                 : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier_to_clear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_to_clear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_to_clear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_to_clear.image = record_context_.image;
        barrier_to_clear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier_to_clear.subresourceRange.baseMipLevel = 0U;
        barrier_to_clear.subresourceRange.levelCount = 1U;
        barrier_to_clear.subresourceRange.baseArrayLayer = 0U;
        barrier_to_clear.subresourceRange.layerCount = 1U;

        vkCmdPipelineBarrier(record_context_.command_buffer,
                             initialized ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                                         : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0U,
                             0U,
                             nullptr,
                             0U,
                             nullptr,
                             1U,
                             &barrier_to_clear);

        VkClearColorValue clear_color{};
        clear_color.float32[0] = 0.09F;
        clear_color.float32[1] = 0.05F;
        clear_color.float32[2] = 0.18F;
        clear_color.float32[3] = 1.00F;

        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0U;
        range.levelCount = 1U;
        range.baseArrayLayer = 0U;
        range.layerCount = 1U;

        vkCmdClearColorImage(record_context_.command_buffer,
                             record_context_.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear_color,
                             1U,
                             &range);

        VkImageMemoryBarrier barrier_to_present{};
        barrier_to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_to_present.dstAccessMask = 0U;
        barrier_to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier_to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_to_present.image = record_context_.image;
        barrier_to_present.subresourceRange = range;

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
    vr::McVector<uint8_t> image_initialized{};
};

} // namespace

int main(int argc_, char** argv_) {
    vr::runtime::InstallProcessCrashTracer(argc_, argv_);
    try {
        using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;

        Runtime::CreateInfo create_info;
        create_info.platform.window.title = "Vulkan SDL3 RuntimeHost Demo";
        create_info.platform.window.width = 1280;
        create_info.platform.window.height = 720;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = true;

        create_info.render_loop.swapchain.enable_vsync = true;
        create_info.render_loop.swapchain.preferred_image_count = 3U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;

        Runtime runtime;
        runtime.Initialize(create_info);

        ClearToPresentRecorder recorder;

        std::cout << "RuntimeHost initialized. Close window to exit (~5s auto exit).\n";

        int loop_count = 0;
        constexpr int max_loops = 500;
        while (runtime.IsRunning() && loop_count < max_loops) {
            const auto tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::SkippedWindowHidden) {
                SDL_Delay(8);
                ++loop_count;
                continue;
            }

            SDL_Delay(8);
            ++loop_count;
        }

        runtime.Shutdown();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "sdl_runtime_demo failed: " << ex.what() << '\n';
        return 1;
    }
}
