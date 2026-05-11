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

enum class VulkanFeatureChainPolicy : std::uint8_t {
    minimal_required = 0,
    explicit_vulkan12_vulkan13 = 1
};

struct VulkanDeviceCreateInfo {
    McVector<const char*> required_extensions{};
    VkPhysicalDeviceFeatures required_features{};
    VkPhysicalDeviceVulkan12Features required_vulkan12_features{};
    VkPhysicalDeviceVulkan13Features required_vulkan13_features{};
    VkPhysicalDeviceVulkan12Features optional_vulkan12_features{};
    VkPhysicalDeviceVulkan13Features optional_vulkan13_features{};
    // `minimal_required`：仅当对应 struct 中存在至少一个 VK_TRUE feature bit 时，
    // 才把 Vulkan 1.2 / 1.3 feature struct 接入 vkCreateDevice 的 pNext 链。
    // 这是默认策略，可保持 pNext 链最小化。
    //
    // `explicit_vulkan12_vulkan13`：即使所有 Vulkan 1.2 / 1.3 feature bit 都为 VK_FALSE，
    // 也显式把两个 struct 接入 pNext 链。主要用于驱动兼容排查或需要稳定/可观测
    // feature chain 结构的调试场景；不会改变 feature enable 结果，只改变链的显式性。
    VulkanFeatureChainPolicy feature_chain_policy =
        VulkanFeatureChainPolicy::minimal_required;
    const void* required_features_pnext = nullptr;
    bool require_dedicated_transfer_queue = false;
};

void EnableRecommendedBindlessOptionalFeatures(VulkanDeviceCreateInfo& create_info_) noexcept;

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics{};
    std::optional<uint32_t> present{};
    std::optional<uint32_t> compute{};
    std::optional<uint32_t> transfer{};

    [[nodiscard]] bool Complete(bool needs_present_) const noexcept;
};

struct DescriptorIndexingCaps {
    bool supported = false;
    bool enabled = false;

    bool sampled_image_array_dynamic_indexing = false;
    bool runtime_descriptor_array = false;
    bool descriptor_binding_partially_bound = false;
    bool descriptor_binding_variable_descriptor_count = false;

    bool sampled_image_array_non_uniform_indexing = false;
    bool sampler_array_non_uniform_indexing = false;

    bool sampled_image_update_after_bind = false;
    bool sampler_update_after_bind = false;
    bool update_unused_while_pending = false;
    bool null_descriptor = false;

    std::uint32_t max_sampled_image_slots = 0U;
    std::uint32_t max_sampler_slots = 0U;
    std::uint32_t max_variable_descriptor_count = 0U;
    std::uint32_t max_update_after_bind_sampled_images = 0U;
    std::uint32_t max_update_after_bind_samplers = 0U;
    std::uint32_t max_per_stage_resources = 0U;
    std::uint32_t max_update_after_bind_resources = 0U;
};

struct DescriptorSetLayoutSupportInfo {
    bool supported = false;
    std::uint32_t max_variable_descriptor_count = 0U;
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
    [[nodiscard]] const DescriptorIndexingCaps& DescriptorIndexingCapsInfo() const noexcept;

    [[nodiscard]] DescriptorSetLayoutSupportInfo QueryDescriptorSetLayoutSupport(
        const VkDescriptorSetLayoutCreateInfo& create_info_) const;

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
    DescriptorIndexingCaps descriptor_indexing_caps{};

    bool validation_enabled = false;
    McVector<const char*> enabled_validation_layers{};
    McVector<const char*> enabled_instance_extensions{};
    McVector<const char*> enabled_device_extensions{};
};

} // namespace vr


