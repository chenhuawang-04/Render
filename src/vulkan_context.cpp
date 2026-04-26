#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace vr {

namespace {

using PropertiesVector = McVector<VkExtensionProperties>;
using LayerVector = McVector<VkLayerProperties>;

[[nodiscard]] const char* VkResultToString(VkResult result_) noexcept {
    switch (result_) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
        default: return "VK_ERROR_UNKNOWN";
    }
}

void CheckVk(VkResult result_, const char* message_) {
    if (result_ == VK_SUCCESS) {
        return;
    }

    std::ostringstream oss;
    oss << message_ << " failed: " << VkResultToString(result_) << " (" << static_cast<int>(result_) << ")";
    throw std::runtime_error(oss.str());
}

[[nodiscard]] bool ContainsCString(const McVector<const char*>& values_, const char* candidate_) {
    for (const char* value : values_) {
        if (std::strcmp(value, candidate_) == 0) {
            return true;
        }
    }
    return false;
}

void PushUniqueCString(McVector<const char*>& values_, const char* candidate_) {
    if (!ContainsCString(values_, candidate_)) {
        values_.push_back(candidate_);
    }
}

[[nodiscard]] LayerVector EnumerateInstanceLayers() {
    uint32_t count = 0U;
    CheckVk(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties(count)");

    LayerVector layers;
    layers.resize(count);
    if (count > 0U) {
        CheckVk(vkEnumerateInstanceLayerProperties(&count, layers.data()), "vkEnumerateInstanceLayerProperties(data)");
    }
    return layers;
}

[[nodiscard]] PropertiesVector EnumerateInstanceExtensions() {
    uint32_t count = 0U;
    CheckVk(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "vkEnumerateInstanceExtensionProperties(count)");

    PropertiesVector extensions;
    extensions.resize(count);
    if (count > 0U) {
        CheckVk(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()), "vkEnumerateInstanceExtensionProperties(data)");
    }
    return extensions;
}

[[nodiscard]] PropertiesVector EnumerateDeviceExtensions(VkPhysicalDevice physical_device_) {
    uint32_t count = 0U;
    CheckVk(vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &count, nullptr), "vkEnumerateDeviceExtensionProperties(count)");

    PropertiesVector extensions;
    extensions.resize(count);
    if (count > 0U) {
        CheckVk(vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &count, extensions.data()), "vkEnumerateDeviceExtensionProperties(data)");
    }
    return extensions;
}

[[nodiscard]] bool HasRequiredLayers(const McVector<const char*>& required_layers_) {
    const auto available_layers = EnumerateInstanceLayers();
    for (const char* required_layer : required_layers_) {
        bool found = false;
        for (const auto& available : available_layers) {
            if (std::strcmp(required_layer, available.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool HasRequiredExtensions(const McVector<const char*>& required_extensions_,
                                         const PropertiesVector& available_extensions_) {
    for (const char* required_ext : required_extensions_) {
        bool found = false;
        for (const auto& available_ext : available_extensions_) {
            if (std::strcmp(required_ext, available_ext.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SupportsRequiredFeatures(const VkPhysicalDeviceFeatures& supported_features_,
                                            const VkPhysicalDeviceFeatures& required_features_) {
    constexpr std::size_t feature_count = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
    const auto* supported = reinterpret_cast<const VkBool32*>(&supported_features_);
    const auto* required = reinterpret_cast<const VkBool32*>(&required_features_);

    for (std::size_t i = 0; i < feature_count; ++i) {
        if (required[i] && !supported[i]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SupportsRequiredVulkan12Features(const VkPhysicalDeviceVulkan12Features& supported_features_,
                                                    const VkPhysicalDeviceVulkan12Features& required_features_) {
    constexpr std::size_t first_feature_offset =
        offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge);
    constexpr std::size_t feature_count =
        (sizeof(VkPhysicalDeviceVulkan12Features) - first_feature_offset) / sizeof(VkBool32);

    const auto* supported = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&supported_features_) + first_feature_offset);
    const auto* required = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&required_features_) + first_feature_offset);

    for (std::size_t i = 0; i < feature_count; ++i) {
        if (required[i] && !supported[i]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SupportsRequiredVulkan13Features(const VkPhysicalDeviceVulkan13Features& supported_features_,
                                                    const VkPhysicalDeviceVulkan13Features& required_features_) {
    constexpr std::size_t first_feature_offset =
        offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess);
    constexpr std::size_t feature_count =
        (sizeof(VkPhysicalDeviceVulkan13Features) - first_feature_offset) / sizeof(VkBool32);

    const auto* supported = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&supported_features_) + first_feature_offset);
    const auto* required = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&required_features_) + first_feature_offset);

    for (std::size_t i = 0; i < feature_count; ++i) {
        if (required[i] && !supported[i]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool HasRequiredVulkan12Features(const VkPhysicalDeviceVulkan12Features& required_features_) {
    constexpr std::size_t first_feature_offset =
        offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge);
    constexpr std::size_t feature_count =
        (sizeof(VkPhysicalDeviceVulkan12Features) - first_feature_offset) / sizeof(VkBool32);

    const auto* required = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&required_features_) + first_feature_offset);
    for (std::size_t i = 0; i < feature_count; ++i) {
        if (required[i]) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool HasRequiredVulkan13Features(const VkPhysicalDeviceVulkan13Features& required_features_) {
    constexpr std::size_t first_feature_offset =
        offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess);
    constexpr std::size_t feature_count =
        (sizeof(VkPhysicalDeviceVulkan13Features) - first_feature_offset) / sizeof(VkBool32);

    const auto* required = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&required_features_) + first_feature_offset);
    for (std::size_t i = 0; i < feature_count; ++i) {
        if (required[i]) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice physical_device_, VkSurfaceKHR surface_) {
    uint32_t family_count = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &family_count, nullptr);

    McVector<VkQueueFamilyProperties> families;
    families.resize(family_count);
    if (family_count > 0U) {
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &family_count, families.data());
    }

    QueueFamilyIndices indices;

    for (uint32_t i = 0U; i < family_count; ++i) {
        const auto& family = families[i];
        if (family.queueCount == 0U) {
            continue;
        }

        if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) && !indices.graphics.has_value()) {
            indices.graphics = i;
        }

        if ((family.queueFlags & VK_QUEUE_COMPUTE_BIT) && !indices.compute.has_value()) {
            indices.compute = i;
        }

        if ((family.queueFlags & VK_QUEUE_TRANSFER_BIT) && !indices.transfer.has_value()) {
            indices.transfer = i;
        }

        if (surface_ != VK_NULL_HANDLE && !indices.present.has_value()) {
            VkBool32 present_support = VK_FALSE;
            CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, i, surface_, &present_support),
                    "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (present_support == VK_TRUE) {
                indices.present = i;
            }
        }
    }

    for (uint32_t i = 0U; i < family_count; ++i) {
        const auto& family = families[i];
        if (family.queueCount == 0U) {
            continue;
        }

        const bool is_graphics = (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U;
        const bool is_compute = (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U;
        const bool is_transfer = (family.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0U;

        if (is_compute && !is_graphics) {
            indices.compute = i;
            break;
        }

        if (is_transfer && !is_graphics && !is_compute) {
            indices.transfer = i;
        }
    }

    if (!indices.compute.has_value()) {
        indices.compute = indices.graphics;
    }

    if (!indices.transfer.has_value()) {
        for (uint32_t i = 0U; i < family_count; ++i) {
            const auto& family = families[i];
            if (family.queueCount == 0U) {
                continue;
            }
            const bool is_graphics = (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U;
            const bool is_transfer = (family.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0U;
            if (is_transfer && !is_graphics) {
                indices.transfer = i;
                break;
            }
        }
    }

    if (!indices.transfer.has_value()) {
        indices.transfer = indices.compute.has_value() ? indices.compute : indices.graphics;
    }

    if (surface_ == VK_NULL_HANDLE) {
        indices.present = indices.graphics;
    }

    return indices;
}

[[nodiscard]] bool CheckSwapchainAdequate(VkPhysicalDevice physical_device_, VkSurfaceKHR surface_) {
    if (surface_ == VK_NULL_HANDLE) {
        return true;
    }

    uint32_t format_count = 0U;
    uint32_t mode_count = 0U;
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &mode_count, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");

    return format_count > 0U && mode_count > 0U;
}

[[nodiscard]] int ScorePhysicalDevice(VkPhysicalDevice physical_device_) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_device_, &props);

    int score = 0;
    switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score += 10000; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score += 5000; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score += 2000; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: score += 500; break;
        default: break;
    }

    score += static_cast<int>(props.limits.maxImageDimension2D);
    return score;
}

void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& create_info_) {
    create_info_ = {};
    create_info_.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info_.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info_.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info_.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT message_severity_,
                                      VkDebugUtilsMessageTypeFlagsEXT message_type_,
                                      const VkDebugUtilsMessengerCallbackDataEXT* callback_data_,
                                      void* user_data_) -> VkBool32 {
        (void)message_type_;
        (void)user_data_;
        const char* level =
            (message_severity_ & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" :
            (message_severity_ & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "WARN" :
                                                                                     "INFO";
        std::cerr << "[Vulkan][" << level << "] " << callback_data_->pMessage << '\n';
        return VK_FALSE;
    };
    create_info_.pUserData = nullptr;
}

void DestroyDebugMessenger(VkInstance instance_, VkDebugUtilsMessengerEXT messenger_) {
    if (instance_ == VK_NULL_HANDLE || messenger_ == VK_NULL_HANDLE) {
        return;
    }
    const auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy_fn != nullptr) {
        destroy_fn(instance_, messenger_, nullptr);
    }
}

void PushUniqueUint(McVector<uint32_t>& values_, uint32_t value_) {
    for (const uint32_t v : values_) {
        if (v == value_) {
            return;
        }
    }
    values_.push_back(value_);
}

} // namespace

VulkanInstanceCreateInfo::VulkanInstanceCreateInfo() {
    validation_layers.push_back("VK_LAYER_KHRONOS_validation");
}

bool QueueFamilyIndices::Complete(bool needs_present_) const noexcept {
    if (!graphics.has_value()) {
        return false;
    }
    if (needs_present_ && !present.has_value()) {
        return false;
    }
    if (!compute.has_value()) {
        return false;
    }
    if (!transfer.has_value()) {
        return false;
    }
    return true;
}

VulkanContext::~VulkanContext() {
    Shutdown();
}

VulkanContext::VulkanContext(VulkanContext&& other_) noexcept {
    *this = std::move(other_);
}

VulkanContext& VulkanContext::operator=(VulkanContext&& other_) noexcept {
    if (this == &other_) {
        return *this;
    }

    Shutdown();

    instance = std::exchange(other_.instance, VK_NULL_HANDLE);
    debug_messenger = std::exchange(other_.debug_messenger, VK_NULL_HANDLE);
    physical_device = std::exchange(other_.physical_device, VK_NULL_HANDLE);
    device = std::exchange(other_.device, VK_NULL_HANDLE);
    surface = std::exchange(other_.surface, VK_NULL_HANDLE);

    queue_family_indices = std::move(other_.queue_family_indices);
    graphics_queue = std::exchange(other_.graphics_queue, VK_NULL_HANDLE);
    present_queue = std::exchange(other_.present_queue, VK_NULL_HANDLE);
    compute_queue = std::exchange(other_.compute_queue, VK_NULL_HANDLE);
    transfer_queue = std::exchange(other_.transfer_queue, VK_NULL_HANDLE);

    graphics_command_pool = std::exchange(other_.graphics_command_pool, VK_NULL_HANDLE);
    transfer_command_pool = std::exchange(other_.transfer_command_pool, VK_NULL_HANDLE);

    enabled_features = std::exchange(other_.enabled_features, {});
    enabled_vulkan12_features = std::exchange(other_.enabled_vulkan12_features, {});
    enabled_vulkan13_features = std::exchange(other_.enabled_vulkan13_features, {});

    validation_enabled = std::exchange(other_.validation_enabled, false);
    enabled_validation_layers = std::move(other_.enabled_validation_layers);
    enabled_instance_extensions = std::move(other_.enabled_instance_extensions);
    enabled_device_extensions = std::move(other_.enabled_device_extensions);
    return *this;
}

void VulkanContext::Initialize(const VulkanInstanceCreateInfo& instance_info_,
                               const VulkanDeviceCreateInfo& device_info_,
                               VkSurfaceKHR surface_) {
    InitializeInstance(instance_info_);
    InitializeDevice(device_info_, surface_);
}

void VulkanContext::InitializeInstance(const VulkanInstanceCreateInfo& instance_info_) {
    Shutdown();
    CreateInstance(instance_info_);
    SetupDebugMessenger();
}

void VulkanContext::InitializeDevice(const VulkanDeviceCreateInfo& device_info_,
                                     VkSurfaceKHR surface_) {
    if (instance == VK_NULL_HANDLE) {
        throw std::runtime_error("InitializeDevice called before InitializeInstance");
    }

    ShutdownDevice();
    surface = surface_;
    PickPhysicalDevice(device_info_);
    CreateLogicalDevice(device_info_);
    CreateDefaultCommandPools();
}

void VulkanContext::ShutdownDevice() {
    if (device != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(device);

        if (graphics_command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, graphics_command_pool, nullptr);
            graphics_command_pool = VK_NULL_HANDLE;
        }
        if (transfer_command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, transfer_command_pool, nullptr);
            transfer_command_pool = VK_NULL_HANDLE;
        }

        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    physical_device = VK_NULL_HANDLE;
    surface = VK_NULL_HANDLE;
    queue_family_indices = {};

    graphics_queue = VK_NULL_HANDLE;
    present_queue = VK_NULL_HANDLE;
    compute_queue = VK_NULL_HANDLE;
    transfer_queue = VK_NULL_HANDLE;

    enabled_features = {};
    enabled_vulkan12_features = {};
    enabled_vulkan12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enabled_vulkan13_features = {};
    enabled_vulkan13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
}

void VulkanContext::Shutdown() {
    ShutdownDevice();

    DestroyDebugMessenger(instance, debug_messenger);
    debug_messenger = VK_NULL_HANDLE;

    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

    validation_enabled = false;
    enabled_validation_layers.clear();
    enabled_instance_extensions.clear();
    enabled_device_extensions.clear();
}

bool VulkanContext::IsInstanceInitialized() const noexcept {
    return instance != VK_NULL_HANDLE;
}

bool VulkanContext::IsDeviceInitialized() const noexcept {
    return device != VK_NULL_HANDLE;
}

bool VulkanContext::IsInitialized() const noexcept {
    return IsInstanceInitialized() && IsDeviceInitialized();
}

VkInstance VulkanContext::Instance() const noexcept {
    return instance;
}

VkPhysicalDevice VulkanContext::PhysicalDevice() const noexcept {
    return physical_device;
}

VkDevice VulkanContext::Device() const noexcept {
    return device;
}

VkSurfaceKHR VulkanContext::Surface() const noexcept {
    return surface;
}

VkQueue VulkanContext::GraphicsQueue() const noexcept {
    return graphics_queue;
}

VkQueue VulkanContext::PresentQueue() const noexcept {
    return present_queue;
}

VkQueue VulkanContext::ComputeQueue() const noexcept {
    return compute_queue;
}

VkQueue VulkanContext::TransferQueue() const noexcept {
    return transfer_queue;
}

const QueueFamilyIndices& VulkanContext::QueueFamilies() const noexcept {
    return queue_family_indices;
}

VkCommandPool VulkanContext::GraphicsCommandPool() const noexcept {
    return graphics_command_pool;
}

VkCommandPool VulkanContext::TransferCommandPool() const noexcept {
    return transfer_command_pool;
}

const VkPhysicalDeviceFeatures& VulkanContext::EnabledFeatures() const noexcept {
    return enabled_features;
}

const VkPhysicalDeviceVulkan12Features& VulkanContext::EnabledVulkan12Features() const noexcept {
    return enabled_vulkan12_features;
}

const VkPhysicalDeviceVulkan13Features& VulkanContext::EnabledVulkan13Features() const noexcept {
    return enabled_vulkan13_features;
}

VkCommandBuffer VulkanContext::BeginSingleTimeCommands() const {
    if (device == VK_NULL_HANDLE || graphics_command_pool == VK_NULL_HANDLE) {
        throw std::runtime_error("BeginSingleTimeCommands called before VulkanContext initialization");
    }

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = graphics_command_pool;
    alloc_info.commandBufferCount = 1U;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    CheckVk(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    CheckVk(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");
    return command_buffer;
}

void VulkanContext::EndSingleTimeCommands(VkCommandBuffer command_buffer_) const {
    if (device == VK_NULL_HANDLE || command_buffer_ == VK_NULL_HANDLE) {
        return;
    }

    CheckVk(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer");

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1U;
    submit_info.pCommandBuffers = &command_buffer_;

    CheckVk(vkQueueSubmit(graphics_queue, 1U, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit");
    CheckVk(vkQueueWaitIdle(graphics_queue), "vkQueueWaitIdle");

    vkFreeCommandBuffers(device, graphics_command_pool, 1U, &command_buffer_);
}

void VulkanContext::CreateInstance(const VulkanInstanceCreateInfo& create_info_) {
    uint32_t supported_api_version = VK_API_VERSION_1_0;
    const auto enumerate_instance_version_fn = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerate_instance_version_fn != nullptr) {
        CheckVk(enumerate_instance_version_fn(&supported_api_version), "vkEnumerateInstanceVersion");
    }

    const uint32_t selected_api_version = std::min(create_info_.api_version, supported_api_version);

    validation_enabled = create_info_.enable_validation;
    enabled_validation_layers.clear();
    enabled_instance_extensions.clear();

    if (validation_enabled) {
        if (!HasRequiredLayers(create_info_.validation_layers)) {
            std::cerr << "[Vulkan] Validation layers unavailable, validation will be disabled.\n";
            validation_enabled = false;
        } else {
            enabled_validation_layers = create_info_.validation_layers;
        }
    }

    for (const char* ext : create_info_.required_extensions) {
        PushUniqueCString(enabled_instance_extensions, ext);
    }
    if (validation_enabled) {
        PushUniqueCString(enabled_instance_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    const auto available_extensions = EnumerateInstanceExtensions();
    if (!HasRequiredExtensions(enabled_instance_extensions, available_extensions)) {
        std::ostringstream oss;
        oss << "Missing required instance extension(s):";
        for (const char* required_ext : enabled_instance_extensions) {
            bool found = false;
            for (const auto& available_ext : available_extensions) {
                if (std::strcmp(required_ext, available_ext.extensionName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                oss << " " << required_ext;
            }
        }
        throw std::runtime_error(oss.str());
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = create_info_.app_name;
    app_info.applicationVersion = create_info_.app_version;
    app_info.pEngineName = create_info_.engine_name;
    app_info.engineVersion = create_info_.engine_version;
    app_info.apiVersion = selected_api_version;

    VkInstanceCreateInfo instance_create_info{};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pApplicationInfo = &app_info;
    instance_create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_instance_extensions.size());
    instance_create_info.ppEnabledExtensionNames = enabled_instance_extensions.empty() ? nullptr : enabled_instance_extensions.data();
    instance_create_info.enabledLayerCount = validation_enabled ? static_cast<uint32_t>(enabled_validation_layers.size()) : 0U;
    instance_create_info.ppEnabledLayerNames = validation_enabled ? enabled_validation_layers.data() : nullptr;

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
    if (validation_enabled) {
        PopulateDebugMessengerCreateInfo(debug_create_info);
        instance_create_info.pNext = &debug_create_info;
    }

    CheckVk(vkCreateInstance(&instance_create_info, nullptr, &instance), "vkCreateInstance");
}

void VulkanContext::SetupDebugMessenger() {
    if (!validation_enabled || instance == VK_NULL_HANDLE) {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT create_info{};
    PopulateDebugMessengerCreateInfo(create_info);

    const auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (create_fn == nullptr) {
        std::cerr << "[Vulkan] vkCreateDebugUtilsMessengerEXT unavailable.\n";
        return;
    }

    CheckVk(create_fn(instance, &create_info, nullptr, &debug_messenger), "vkCreateDebugUtilsMessengerEXT");
}

void VulkanContext::PickPhysicalDevice(const VulkanDeviceCreateInfo& create_info_) {
    enabled_device_extensions.clear();
    for (const char* ext : create_info_.required_extensions) {
        PushUniqueCString(enabled_device_extensions, ext);
    }
    if (surface != VK_NULL_HANDLE) {
        PushUniqueCString(enabled_device_extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    uint32_t device_count = 0U;
    CheckVk(vkEnumeratePhysicalDevices(instance, &device_count, nullptr), "vkEnumeratePhysicalDevices(count)");
    if (device_count == 0U) {
        throw std::runtime_error("No Vulkan physical devices found");
    }

    McVector<VkPhysicalDevice> devices;
    devices.resize(device_count);
    CheckVk(vkEnumeratePhysicalDevices(instance, &device_count, devices.data()), "vkEnumeratePhysicalDevices(data)");

    int best_score = std::numeric_limits<int>::min();
    VkPhysicalDevice best_device = VK_NULL_HANDLE;
    QueueFamilyIndices best_indices{};

    const bool needs_vulkan12_features = HasRequiredVulkan12Features(create_info_.required_vulkan12_features);
    const bool needs_vulkan13_features = HasRequiredVulkan13Features(create_info_.required_vulkan13_features);
    const auto get_features2_fn = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    if (get_features2_fn == nullptr &&
        (needs_vulkan12_features || needs_vulkan13_features || create_info_.required_features_pnext != nullptr)) {
        throw std::runtime_error(
            "Required Vulkan12/13 feature query unavailable: vkGetPhysicalDeviceFeatures2 is not supported");
    }

    for (const VkPhysicalDevice candidate : devices) {
        const QueueFamilyIndices indices = FindQueueFamilies(candidate, surface);
        const bool needs_present = surface != VK_NULL_HANDLE;
        if (!indices.Complete(needs_present)) {
            continue;
        }

        if (create_info_.require_dedicated_transfer_queue &&
            indices.transfer.has_value() &&
            indices.graphics.has_value() &&
            indices.transfer.value() == indices.graphics.value()) {
            continue;
        }

        const auto available_extensions = EnumerateDeviceExtensions(candidate);
        if (!HasRequiredExtensions(enabled_device_extensions, available_extensions)) {
            continue;
        }

        if (get_features2_fn != nullptr) {
            VkPhysicalDeviceFeatures2 supported_features2{};
            supported_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

            VkPhysicalDeviceVulkan12Features supported_vulkan12_features{};
            supported_vulkan12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

            VkPhysicalDeviceVulkan13Features supported_vulkan13_features{};
            supported_vulkan13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

            supported_features2.pNext = &supported_vulkan12_features;
            supported_vulkan12_features.pNext = &supported_vulkan13_features;
            supported_vulkan13_features.pNext = nullptr;

            get_features2_fn(candidate, &supported_features2);
            if (!SupportsRequiredFeatures(supported_features2.features, create_info_.required_features)) {
                continue;
            }
            if (!SupportsRequiredVulkan12Features(supported_vulkan12_features,
                                                  create_info_.required_vulkan12_features)) {
                continue;
            }
            if (!SupportsRequiredVulkan13Features(supported_vulkan13_features,
                                                  create_info_.required_vulkan13_features)) {
                continue;
            }
        } else {
            VkPhysicalDeviceFeatures supported_features{};
            vkGetPhysicalDeviceFeatures(candidate, &supported_features);
            if (!SupportsRequiredFeatures(supported_features, create_info_.required_features)) {
                continue;
            }
        }

        if (!CheckSwapchainAdequate(candidate, surface)) {
            continue;
        }

        const int score = ScorePhysicalDevice(candidate);
        if (score > best_score) {
            best_score = score;
            best_device = candidate;
            best_indices = indices;
        }
    }

    if (best_device == VK_NULL_HANDLE) {
        throw std::runtime_error("No suitable Vulkan physical device found");
    }

    physical_device = best_device;
    queue_family_indices = best_indices;
}

void VulkanContext::CreateLogicalDevice(const VulkanDeviceCreateInfo& create_info_) {
    McVector<uint32_t> unique_queue_families;
    unique_queue_families.reserve(4);
    PushUniqueUint(unique_queue_families, queue_family_indices.graphics.value());
    if (queue_family_indices.present.has_value()) {
        PushUniqueUint(unique_queue_families, queue_family_indices.present.value());
    }
    if (queue_family_indices.compute.has_value()) {
        PushUniqueUint(unique_queue_families, queue_family_indices.compute.value());
    }
    if (queue_family_indices.transfer.has_value()) {
        PushUniqueUint(unique_queue_families, queue_family_indices.transfer.value());
    }

    const float queue_priority = 1.0F;
    McVector<VkDeviceQueueCreateInfo> queue_infos;
    queue_infos.reserve(unique_queue_families.size());
    for (const uint32_t family_index : unique_queue_families) {
        VkDeviceQueueCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        info.queueFamilyIndex = family_index;
        info.queueCount = 1U;
        info.pQueuePriorities = &queue_priority;
        queue_infos.push_back(info);
    }

    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
    device_create_info.pQueueCreateInfos = queue_infos.data();
    device_create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_device_extensions.size());
    device_create_info.ppEnabledExtensionNames = enabled_device_extensions.empty() ? nullptr : enabled_device_extensions.data();
    device_create_info.enabledLayerCount = validation_enabled ? static_cast<uint32_t>(enabled_validation_layers.size()) : 0U;
    device_create_info.ppEnabledLayerNames = validation_enabled ? enabled_validation_layers.data() : nullptr;

    VkPhysicalDeviceFeatures2 enabled_features2{};
    enabled_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enabled_features2.features = create_info_.required_features;
    enabled_features2.pNext = nullptr;

    VkPhysicalDeviceVulkan12Features enabled_vulkan12_features_local = create_info_.required_vulkan12_features;
    enabled_vulkan12_features_local.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enabled_vulkan12_features_local.pNext = nullptr;

    VkPhysicalDeviceVulkan13Features enabled_vulkan13_features_local = create_info_.required_vulkan13_features;
    enabled_vulkan13_features_local.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enabled_vulkan13_features_local.pNext = nullptr;

    VkBaseOutStructure* feature_chain_head = nullptr;
    VkBaseOutStructure** feature_chain_next = &feature_chain_head;
    auto append_feature_chain = [&](VkBaseOutStructure* node_) {
        if (node_ == nullptr) {
            return;
        }
        node_->pNext = nullptr;
        *feature_chain_next = node_;
        feature_chain_next = &node_->pNext;
    };

    if (HasRequiredVulkan12Features(enabled_vulkan12_features_local)) {
        append_feature_chain(reinterpret_cast<VkBaseOutStructure*>(&enabled_vulkan12_features_local));
    }
    if (HasRequiredVulkan13Features(enabled_vulkan13_features_local)) {
        append_feature_chain(reinterpret_cast<VkBaseOutStructure*>(&enabled_vulkan13_features_local));
    }
    if (create_info_.required_features_pnext != nullptr) {
        *feature_chain_next = reinterpret_cast<VkBaseOutStructure*>(
            const_cast<void*>(create_info_.required_features_pnext));
    }

    enabled_features2.pNext = feature_chain_head;
    device_create_info.pNext = &enabled_features2;
    device_create_info.pEnabledFeatures = nullptr;

    CheckVk(vkCreateDevice(physical_device, &device_create_info, nullptr, &device), "vkCreateDevice");

    vkGetDeviceQueue(device, queue_family_indices.graphics.value(), 0U, &graphics_queue);
    if (queue_family_indices.present.has_value()) {
        vkGetDeviceQueue(device, queue_family_indices.present.value(), 0U, &present_queue);
    } else {
        present_queue = graphics_queue;
    }
    if (queue_family_indices.compute.has_value()) {
        vkGetDeviceQueue(device, queue_family_indices.compute.value(), 0U, &compute_queue);
    } else {
        compute_queue = graphics_queue;
    }
    if (queue_family_indices.transfer.has_value()) {
        vkGetDeviceQueue(device, queue_family_indices.transfer.value(), 0U, &transfer_queue);
    } else {
        transfer_queue = compute_queue;
    }

    enabled_features = create_info_.required_features;
    enabled_vulkan12_features = create_info_.required_vulkan12_features;
    enabled_vulkan12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enabled_vulkan12_features.pNext = nullptr;
    enabled_vulkan13_features = create_info_.required_vulkan13_features;
    enabled_vulkan13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enabled_vulkan13_features.pNext = nullptr;
}

void VulkanContext::CreateDefaultCommandPools() {
    VkCommandPoolCreateInfo graphics_pool_info{};
    graphics_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    graphics_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    graphics_pool_info.queueFamilyIndex = queue_family_indices.graphics.value();

    CheckVk(vkCreateCommandPool(device, &graphics_pool_info, nullptr, &graphics_command_pool), "vkCreateCommandPool(graphics)");

    VkCommandPoolCreateInfo transfer_pool_info{};
    transfer_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    transfer_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    transfer_pool_info.queueFamilyIndex = queue_family_indices.transfer.value_or(queue_family_indices.graphics.value());

    CheckVk(vkCreateCommandPool(device, &transfer_pool_info, nullptr, &transfer_command_pool), "vkCreateCommandPool(transfer)");
}

} // namespace vr
