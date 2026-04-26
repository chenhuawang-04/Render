#pragma once

#include <cstdint>
#include <optional>

#include "Center/Memory/Container/Vector/McVector.hpp"
#include <vulkan/vulkan.h>

namespace vr {

template<typename T>
using McVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct VulkanInstanceCreateInfo {
    VulkanInstanceCreateInfo();

    const char* app_name = "VulkanRender_New";
    const char* engine_name = "Melosyne";
    uint32_t app_version = VK_MAKE_VERSION(0, 1, 0);
    uint32_t engine_version = VK_MAKE_VERSION(0, 1, 0);
    uint32_t api_version = VK_API_VERSION_1_3;
    bool enable_validation = true;
    McVector<const char*> required_extensions{};
    McVector<const char*> validation_layers{};
};

struct VulkanDeviceCreateInfo {
    McVector<const char*> required_extensions{};
    VkPhysicalDeviceFeatures required_features{};
    VkPhysicalDeviceVulkan12Features required_vulkan12_features{};
    VkPhysicalDeviceVulkan13Features required_vulkan13_features{};
    const void* required_features_pnext = nullptr;
    bool require_dedicated_transfer_queue = false;
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics{};
    std::optional<uint32_t> present{};
    std::optional<uint32_t> compute{};
    std::optional<uint32_t> transfer{};

    [[nodiscard]] bool Complete(bool needs_present_) const noexcept;
};

class VulkanContext final {
public:
    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VulkanContext(VulkanContext&& other_) noexcept;
    VulkanContext& operator=(VulkanContext&& other_) noexcept;

    // surface 由上层窗口系统创建与管理；Context 不拥有也不销毁该 surface。
    void Initialize(const VulkanInstanceCreateInfo& instance_info_ = {},
                    const VulkanDeviceCreateInfo& device_info_ = {},
                    VkSurfaceKHR surface_ = VK_NULL_HANDLE);

    void InitializeInstance(const VulkanInstanceCreateInfo& instance_info_ = {});
    void InitializeDevice(const VulkanDeviceCreateInfo& device_info_ = {},
                          VkSurfaceKHR surface_ = VK_NULL_HANDLE);

    void ShutdownDevice();
    void Shutdown();

    [[nodiscard]] bool IsInstanceInitialized() const noexcept;
    [[nodiscard]] bool IsDeviceInitialized() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    [[nodiscard]] VkInstance Instance() const noexcept;
    [[nodiscard]] VkPhysicalDevice PhysicalDevice() const noexcept;
    [[nodiscard]] VkDevice Device() const noexcept;
    [[nodiscard]] VkSurfaceKHR Surface() const noexcept;
    [[nodiscard]] VkQueue GraphicsQueue() const noexcept;
    [[nodiscard]] VkQueue PresentQueue() const noexcept;
    [[nodiscard]] VkQueue ComputeQueue() const noexcept;
    [[nodiscard]] VkQueue TransferQueue() const noexcept;
    [[nodiscard]] const QueueFamilyIndices& QueueFamilies() const noexcept;
    [[nodiscard]] VkCommandPool GraphicsCommandPool() const noexcept;
    [[nodiscard]] VkCommandPool TransferCommandPool() const noexcept;
    [[nodiscard]] const VkPhysicalDeviceFeatures& EnabledFeatures() const noexcept;
    [[nodiscard]] const VkPhysicalDeviceVulkan12Features& EnabledVulkan12Features() const noexcept;
    [[nodiscard]] const VkPhysicalDeviceVulkan13Features& EnabledVulkan13Features() const noexcept;

    [[nodiscard]] VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer command_buffer_) const;

private:
    void CreateInstance(const VulkanInstanceCreateInfo& create_info_);
    void SetupDebugMessenger();
    void PickPhysicalDevice(const VulkanDeviceCreateInfo& create_info_);
    void CreateLogicalDevice(const VulkanDeviceCreateInfo& create_info_);
    void CreateDefaultCommandPools();

private:
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE; // not owned

    QueueFamilyIndices queue_family_indices{};
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    VkQueue compute_queue = VK_NULL_HANDLE;
    VkQueue transfer_queue = VK_NULL_HANDLE;

    VkCommandPool graphics_command_pool = VK_NULL_HANDLE;
    VkCommandPool transfer_command_pool = VK_NULL_HANDLE;

    VkPhysicalDeviceFeatures enabled_features{};
    VkPhysicalDeviceVulkan12Features enabled_vulkan12_features{};
    VkPhysicalDeviceVulkan13Features enabled_vulkan13_features{};

    bool validation_enabled = false;
    McVector<const char*> enabled_validation_layers{};
    McVector<const char*> enabled_instance_extensions{};
    McVector<const char*> enabled_device_extensions{};
};

} // namespace vr


