#include "vr/runtime/crash_tracer_support.hpp"
#include "vr/vulkan_context.hpp"

#include <iostream>

int main(int argc_, char** argv_) {
    vr::runtime::InstallProcessCrashTracer(argc_, argv_);
    try {
        vr::VulkanContext context;

        vr::VulkanInstanceCreateInfo instance_info;
        instance_info.enable_validation = true;

        vr::VulkanDeviceCreateInfo device_info;
        device_info.required_features.samplerAnisotropy = VK_TRUE;

        context.Initialize(instance_info, device_info);

        std::cout << "Vulkan initialized successfully.\n";
        std::cout << "Graphics queue family: " << context.QueueFamilies().graphics.value() << '\n';
        std::cout << "Compute queue family : " << context.QueueFamilies().compute.value() << '\n';
        std::cout << "Transfer queue family: " << context.QueueFamilies().transfer.value() << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Initialization failed: " << ex.what() << '\n';
        return 1;
    }
}
