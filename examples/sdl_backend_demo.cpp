#include "vr/platform/render_host.hpp"
#include "vr/render/swapchain_host.hpp"

#include <iostream>

int main() {
    try {
        vr::platform::RenderHostCreateInfo create_info;
        create_info.window.title = "Vulkan SDL3 Backend Demo";
        create_info.window.width = 1280;
        create_info.window.height = 720;
        create_info.window.resizable = true;
        create_info.window.high_pixel_density = true;

        create_info.instance.enable_validation = true;
        create_info.device.required_features.samplerAnisotropy = VK_TRUE;

        vr::platform::DefaultRenderHost host;
        host.Initialize(create_info);

        vr::render::SwapchainHost<vr::platform::DefaultWindowSurface> swapchain;
        vr::render::SwapchainCreateInfo swapchain_info;
        swapchain_info.enable_vsync = true;
        swapchain_info.preferred_image_count = 3U;
        swapchain.Initialize(host.Context(), host.SurfaceHost(), swapchain_info);

        const auto& queues = host.Context().QueueFamilies();
        std::cout << "SDL3 Vulkan host initialized.\n";
        std::cout << "Graphics queue family: " << queues.graphics.value() << '\n';
        std::cout << "Present  queue family: " << queues.present.value() << '\n';
        std::cout << "Compute  queue family: " << queues.compute.value() << '\n';
        std::cout << "Transfer queue family: " << queues.transfer.value() << '\n';
        std::cout << "Swapchain images      : " << swapchain.ImageCount() << '\n';
        std::cout << "Swapchain extent      : "
                  << swapchain.Extent().width << " x " << swapchain.Extent().height << '\n';
        std::cout << "Close the window to exit (auto exit after ~5s in demo).\n";

        int loop_count = 0;
        constexpr int max_loops = 500;
        while (host.IsRunning() && loop_count < max_loops) {
            (void)host.PollAndHandleCloseEvents();
            if (!swapchain.EnsureValid(host.Context(), host.SurfaceHost(), swapchain_info)) {
                SDL_Delay(10);
                ++loop_count;
                continue;
            }
            SDL_Delay(10);
            ++loop_count;
        }

        swapchain.Shutdown(host.Context());
        host.Shutdown();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "SDL backend demo failed: " << ex.what() << '\n';
        return 1;
    }
}
