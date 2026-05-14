#include "vr/platform/render_host.hpp"
#include "vr/runtime/crash_tracer_support.hpp"
#include "vr/render/render_loop_host.hpp"

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

    void Record(VkCommandBuffer command_buffer_,
                uint32_t frame_index_,
                uint32_t image_index_,
                VkExtent2D extent_,
                VkFormat format_,
                VkImage image_,
                VkImageView image_view_) {
        (void)frame_index_;
        (void)format_;
        (void)image_view_;

        if (image_index_ >= image_initialized.size()) {
            const uint32_t previous_size = static_cast<uint32_t>(image_initialized.size());
            image_initialized.resize(image_index_ + 1U);
            for (uint32_t i = previous_size; i < image_initialized.size(); ++i) {
                image_initialized[i] = 0U;
            }
        }

        const bool initialized = image_initialized[image_index_] != 0U;

        VkImageMemoryBarrier barrier_to_clear{};
        barrier_to_clear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_to_clear.srcAccessMask = initialized ? VK_ACCESS_MEMORY_READ_BIT : 0U;
        barrier_to_clear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_to_clear.oldLayout = initialized ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                 : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier_to_clear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_to_clear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_to_clear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_to_clear.image = image_;
        barrier_to_clear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier_to_clear.subresourceRange.baseMipLevel = 0U;
        barrier_to_clear.subresourceRange.levelCount = 1U;
        barrier_to_clear.subresourceRange.baseArrayLayer = 0U;
        barrier_to_clear.subresourceRange.layerCount = 1U;

        vkCmdPipelineBarrier(command_buffer_,
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
        clear_color.float32[0] = 0.05F;
        clear_color.float32[1] = 0.10F;
        clear_color.float32[2] = 0.20F;
        clear_color.float32[3] = 1.00F;

        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0U;
        range.levelCount = 1U;
        range.baseArrayLayer = 0U;
        range.layerCount = 1U;

        vkCmdClearColorImage(command_buffer_,
                             image_,
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
        barrier_to_present.image = image_;
        barrier_to_present.subresourceRange = range;

        vkCmdPipelineBarrier(command_buffer_,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0U,
                             0U,
                             nullptr,
                             0U,
                             nullptr,
                             1U,
                             &barrier_to_present);

        image_initialized[image_index_] = 1U;
        (void)extent_;
    }

private:
    vr::McVector<uint8_t> image_initialized{};
};

} // namespace

int main(int argc_, char** argv_) {
    vr::runtime::InstallProcessCrashTracer(argc_, argv_);
    try {
        vr::platform::RenderHostCreateInfo host_info;
        host_info.window.title = "Vulkan SDL3 RenderLoop Demo";
        host_info.window.width = 1280;
        host_info.window.height = 720;
        host_info.window.resizable = true;
        host_info.window.high_pixel_density = true;
        host_info.instance.enable_validation = true;

        vr::platform::DefaultRenderHost host;
        host.Initialize(host_info);

        vr::render::SwapchainHost<vr::platform::DefaultWindowSurface> swapchain;
        vr::render::RenderLoopHost<vr::platform::DefaultWindowSurface,
                                   decltype(swapchain),
                                   2U> render_loop;
        vr::render::RenderLoopCreateInfo loop_info;
        loop_info.swapchain.enable_vsync = true;
        loop_info.swapchain.preferred_image_count = 3U;
        loop_info.commands.initial_primary_per_frame = 2U;
        loop_info.commands.primary_growth_chunk = 2U;

        render_loop.Initialize(host.Context(), host.SurfaceHost(), swapchain, loop_info);

        ClearToPresentRecorder recorder;

        std::cout << "RenderLoop initialized. Close window to exit (~5s auto exit).\n";

        int loop_count = 0;
        constexpr int max_loops = 500;
        while (host.IsRunning() && loop_count < max_loops) {
            (void)host.PollAndHandleCloseEvents();

            const auto tick_result = render_loop.Tick(host.Context(),
                                                      host.SurfaceHost(),
                                                      swapchain,
                                                      recorder);
            if (tick_result.code == vr::render::TickCode::SkippedWindowHidden) {
                SDL_Delay(8);
                ++loop_count;
                continue;
            }

            SDL_Delay(8);
            ++loop_count;
        }

        render_loop.Shutdown(host.Context(), swapchain);
        host.Shutdown();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "sdl_render_loop_demo failed: " << ex.what() << '\n';
        return 1;
    }
}

