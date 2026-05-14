#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#endif

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

[[nodiscard]] bool IsEnabledEnvFlag(const char* env_name_) noexcept {
    if (env_name_ == nullptr || env_name_[0] == '\0') {
        return false;
    }

    std::string normalized{};
#if defined(_WIN32)
    const DWORD required_length = ::GetEnvironmentVariableA(env_name_, nullptr, 0U);
    if (required_length == 0U) {
        return false;
    }

    McVector<char> value_buffer{};
    value_buffer.resize(static_cast<std::size_t>(required_length) + 1U);
    const DWORD copied_length = ::GetEnvironmentVariableA(
        env_name_,
        value_buffer.data(),
        static_cast<DWORD>(value_buffer.size()));
    if (copied_length == 0U ||
        copied_length >= static_cast<DWORD>(value_buffer.size())) {
        return false;
    }
    normalized.assign(value_buffer.data(), static_cast<std::size_t>(copied_length));
#else
    const char* value = std::getenv(env_name_);
    if (value == nullptr) {
        return false;
    }
    normalized.assign(value);
#endif

    if (normalized.empty()) {
        return false;
    }
    for (char& ch : normalized) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    return normalized == "1" ||
           normalized == "true" ||
           normalized == "yes" ||
           normalized == "on";
}

[[nodiscard]] bool DeviceSelectionVerboseLogEnabled() noexcept {
    static const bool enabled = IsEnabledEnvFlag("VR_VK_LOG_DEVICE_SELECTION");
    return enabled;
}

[[nodiscard]] const char* PhysicalDeviceTypeName(VkPhysicalDeviceType device_type_) noexcept {
    switch (device_type_) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
            return "other";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "cpu";
        default:
            return "unknown";
    }
}

void AppendMissingExtensions(std::ostringstream& stream_,
                             const McVector<const char*>& required_extensions_,
                             const PropertiesVector& available_extensions_) {
    bool has_missing = false;
    for (const char* required_extension : required_extensions_) {
        bool found = false;
        for (const auto& available_extension : available_extensions_) {
            if (std::strcmp(required_extension, available_extension.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            stream_ << (has_missing ? ", " : "");
            stream_ << required_extension;
            has_missing = true;
        }
    }
    if (!has_missing) {
        stream_ << "<none>";
    }
}

void AppendMissingFeatureBits(std::ostringstream& stream_,
                              const char* label_,
                              const VkBool32* required_bits_,
                              const VkBool32* supported_bits_,
                              std::size_t bit_count_,
                              std::size_t max_print_count_ = 8U) {
    std::size_t missing_count = 0U;
    for (std::size_t i = 0U; i < bit_count_; ++i) {
        if (required_bits_[i] && !supported_bits_[i]) {
            if (missing_count < max_print_count_) {
                stream_ << (missing_count == 0U ? "" : ", ")
                        << label_ << "[" << i << "]";
            }
            ++missing_count;
        }
    }

    if (missing_count == 0U) {
        stream_ << "<none>";
        return;
    }

    if (missing_count > max_print_count_) {
        stream_ << ", ... (+" << (missing_count - max_print_count_) << " more)";
    }
}

[[nodiscard]] std::size_t CountEnabledFeatureBits(const VkBool32* bits_,
                                                  std::size_t bit_count_) noexcept {
    std::size_t count = 0U;
    for (std::size_t i = 0U; i < bit_count_; ++i) {
        if (bits_[i] != VK_FALSE) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] constexpr std::size_t Vulkan12FirstFeatureOffset() noexcept {
    return offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge);
}

[[nodiscard]] constexpr std::size_t Vulkan12FeatureCount() noexcept {
    return ((offsetof(VkPhysicalDeviceVulkan12Features, subgroupBroadcastDynamicId) -
             Vulkan12FirstFeatureOffset()) / sizeof(VkBool32)) + 1U;
}

[[nodiscard]] constexpr std::size_t Vulkan13FirstFeatureOffset() noexcept {
    return offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess);
}

[[nodiscard]] constexpr std::size_t Vulkan13FeatureCount() noexcept {
    return ((offsetof(VkPhysicalDeviceVulkan13Features, maintenance4) -
             Vulkan13FirstFeatureOffset()) / sizeof(VkBool32)) + 1U;
}

void NormalizeVulkan10FeatureBits(VkPhysicalDeviceFeatures& features_) noexcept {
    constexpr std::size_t feature_count =
        sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
    auto* bits = reinterpret_cast<VkBool32*>(&features_);
    for (std::size_t i = 0U; i < feature_count; ++i) {
        bits[i] = (bits[i] == VK_TRUE) ? VK_TRUE : VK_FALSE;
    }
}

void NormalizeVulkan12FeatureBits(VkPhysicalDeviceVulkan12Features& features_) noexcept {
    constexpr std::size_t first_feature_offset = Vulkan12FirstFeatureOffset();
    constexpr std::size_t feature_count = Vulkan12FeatureCount();
    auto* bits = reinterpret_cast<VkBool32*>(
        reinterpret_cast<char*>(&features_) + first_feature_offset);
    for (std::size_t i = 0U; i < feature_count; ++i) {
        bits[i] = (bits[i] == VK_TRUE) ? VK_TRUE : VK_FALSE;
    }
}

void NormalizeVulkan13FeatureBits(VkPhysicalDeviceVulkan13Features& features_) noexcept {
    constexpr std::size_t first_feature_offset = Vulkan13FirstFeatureOffset();
    constexpr std::size_t feature_count = Vulkan13FeatureCount();
    auto* bits = reinterpret_cast<VkBool32*>(
        reinterpret_cast<char*>(&features_) + first_feature_offset);
    for (std::size_t i = 0U; i < feature_count; ++i) {
        bits[i] = (bits[i] == VK_TRUE) ? VK_TRUE : VK_FALSE;
    }
}

void AppendMissingVulkan13NamedFeatures(std::ostringstream& stream_,
                                        const VkPhysicalDeviceVulkan13Features& required_features_,
                                        const VkPhysicalDeviceVulkan13Features& supported_features_) {
    bool first = true;
    auto emit = [&](const char* name_) {
        stream_ << (first ? "" : ", ") << name_;
        first = false;
    };

    if (required_features_.dynamicRendering == VK_TRUE &&
        supported_features_.dynamicRendering != VK_TRUE) {
        emit("dynamicRendering");
    }
    if (required_features_.synchronization2 == VK_TRUE &&
        supported_features_.synchronization2 != VK_TRUE) {
        emit("synchronization2");
    }
    if (required_features_.maintenance4 == VK_TRUE &&
        supported_features_.maintenance4 != VK_TRUE) {
        emit("maintenance4");
    }
    if (required_features_.shaderDemoteToHelperInvocation == VK_TRUE &&
        supported_features_.shaderDemoteToHelperInvocation != VK_TRUE) {
        emit("shaderDemoteToHelperInvocation");
    }
    if (required_features_.subgroupSizeControl == VK_TRUE &&
        supported_features_.subgroupSizeControl != VK_TRUE) {
        emit("subgroupSizeControl");
    }

    if (first) {
        stream_ << "<none>";
    }
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
    constexpr std::size_t first_feature_offset = Vulkan12FirstFeatureOffset();
    constexpr std::size_t feature_count = Vulkan12FeatureCount();

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
    constexpr std::size_t first_feature_offset = Vulkan13FirstFeatureOffset();
    constexpr std::size_t feature_count = Vulkan13FeatureCount();

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
    constexpr std::size_t first_feature_offset = Vulkan12FirstFeatureOffset();
    constexpr std::size_t feature_count = Vulkan12FeatureCount();

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
    constexpr std::size_t first_feature_offset = Vulkan13FirstFeatureOffset();
    constexpr std::size_t feature_count = Vulkan13FeatureCount();

    const auto* required = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&required_features_) + first_feature_offset);
    for (std::size_t i = 0; i < feature_count; ++i) {
        if (required[i]) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ShouldAppendVulkan12Features(
    const VkPhysicalDeviceVulkan12Features& required_features_,
    VulkanFeatureChainPolicy policy_) noexcept {
    return policy_ == VulkanFeatureChainPolicy::explicit_vulkan12_vulkan13 ||
           HasRequiredVulkan12Features(required_features_);
}

[[nodiscard]] bool ShouldAppendVulkan13Features(
    const VkPhysicalDeviceVulkan13Features& required_features_,
    VulkanFeatureChainPolicy policy_) noexcept {
    return policy_ == VulkanFeatureChainPolicy::explicit_vulkan12_vulkan13 ||
           HasRequiredVulkan13Features(required_features_);
}

void MergeSupportedOptionalVulkan12Features(
    VkPhysicalDeviceVulkan12Features& enabled_features_,
    const VkPhysicalDeviceVulkan12Features& optional_features_,
    const VkPhysicalDeviceVulkan12Features& supported_features_) noexcept {
    constexpr std::size_t first_feature_offset = Vulkan12FirstFeatureOffset();
    constexpr std::size_t feature_count = Vulkan12FeatureCount();

    auto* enabled = reinterpret_cast<VkBool32*>(
        reinterpret_cast<char*>(&enabled_features_) + first_feature_offset);
    const auto* optional_bits = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&optional_features_) + first_feature_offset);
    const auto* supported_bits = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&supported_features_) + first_feature_offset);

    for (std::size_t i = 0U; i < feature_count; ++i) {
        if (enabled[i] == VK_FALSE &&
            optional_bits[i] == VK_TRUE &&
            supported_bits[i] == VK_TRUE) {
            enabled[i] = VK_TRUE;
        }
    }
}

void MergeSupportedOptionalVulkan13Features(
    VkPhysicalDeviceVulkan13Features& enabled_features_,
    const VkPhysicalDeviceVulkan13Features& optional_features_,
    const VkPhysicalDeviceVulkan13Features& supported_features_) noexcept {
    constexpr std::size_t first_feature_offset = Vulkan13FirstFeatureOffset();
    constexpr std::size_t feature_count = Vulkan13FeatureCount();

    auto* enabled = reinterpret_cast<VkBool32*>(
        reinterpret_cast<char*>(&enabled_features_) + first_feature_offset);
    const auto* optional_bits = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&optional_features_) + first_feature_offset);
    const auto* supported_bits = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&supported_features_) + first_feature_offset);

    for (std::size_t i = 0U; i < feature_count; ++i) {
        if (enabled[i] == VK_FALSE &&
            optional_bits[i] == VK_TRUE &&
            supported_bits[i] == VK_TRUE) {
            enabled[i] = VK_TRUE;
        }
    }
}

[[nodiscard]] uint32_t MinNonZero(uint32_t lhs_, uint32_t rhs_) noexcept {
    if (lhs_ == 0U) {
        return rhs_;
    }
    if (rhs_ == 0U) {
        return lhs_;
    }
    return std::min(lhs_, rhs_);
}

[[nodiscard]] DescriptorSetLayoutSupportInfo QueryDescriptorSetLayoutSupportInfo(
    VkDevice device_,
    const VkDescriptorSetLayoutCreateInfo& create_info_) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "Descriptor set layout support query requires initialized Vulkan device");
    }

    DescriptorSetLayoutSupportInfo result{};
    VkDescriptorSetVariableDescriptorCountLayoutSupport variable_count_support{};
    variable_count_support.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT;

    VkDescriptorSetLayoutSupport support{};
    support.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
    support.pNext = &variable_count_support;

    vkGetDescriptorSetLayoutSupport(device_, &create_info_, &support);
    result.supported = support.supported == VK_TRUE;
    result.max_variable_descriptor_count = variable_count_support.maxVariableDescriptorCount;
    return result;
}

[[nodiscard]] DescriptorIndexingCaps QueryDescriptorIndexingCaps(
    VkInstance instance_,
    VkPhysicalDevice physical_device_) {
    DescriptorIndexingCaps caps{};
    if (instance_ == VK_NULL_HANDLE || physical_device_ == VK_NULL_HANDLE) {
        return caps;
    }

    const auto get_features2_fn =
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceFeatures2"));
    if (get_features2_fn == nullptr) {
        return caps;
    }

    VkPhysicalDeviceFeatures2 supported_features2{};
    supported_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features{};
    descriptor_indexing_features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

    VkPhysicalDeviceVulkan12Features supported_vulkan12_features{};
    supported_vulkan12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    supported_features2.pNext = &descriptor_indexing_features;
    descriptor_indexing_features.pNext = &supported_vulkan12_features;

    get_features2_fn(physical_device_, &supported_features2);

    caps.sampled_image_array_dynamic_indexing =
        supported_features2.features.shaderSampledImageArrayDynamicIndexing == VK_TRUE;
    caps.runtime_descriptor_array =
        descriptor_indexing_features.runtimeDescriptorArray == VK_TRUE;
    caps.descriptor_binding_partially_bound =
        descriptor_indexing_features.descriptorBindingPartiallyBound == VK_TRUE;
    caps.descriptor_binding_variable_descriptor_count =
        descriptor_indexing_features.descriptorBindingVariableDescriptorCount == VK_TRUE;
    caps.sampled_image_array_non_uniform_indexing =
        descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    caps.sampler_array_non_uniform_indexing =
        descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    caps.sampled_image_update_after_bind =
        descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE;
    caps.update_unused_while_pending =
        descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending == VK_TRUE;
    caps.null_descriptor = false;

    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

    VkPhysicalDeviceDescriptorIndexingProperties descriptor_indexing_properties{};
    descriptor_indexing_properties.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
    properties2.pNext = &descriptor_indexing_properties;

    vkGetPhysicalDeviceProperties2(physical_device_, &properties2);

    caps.max_sampled_image_slots = MinNonZero(
        properties2.properties.limits.maxPerStageDescriptorSampledImages,
        properties2.properties.limits.maxDescriptorSetSampledImages);
    caps.max_sampler_slots = MinNonZero(
        properties2.properties.limits.maxPerStageDescriptorSamplers,
        properties2.properties.limits.maxDescriptorSetSamplers);
    caps.max_update_after_bind_sampled_images = MinNonZero(
        descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindSampledImages,
        descriptor_indexing_properties.maxDescriptorSetUpdateAfterBindSampledImages);
    caps.max_update_after_bind_samplers = MinNonZero(
        descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindSamplers,
        descriptor_indexing_properties.maxDescriptorSetUpdateAfterBindSamplers);
    caps.max_per_stage_resources = properties2.properties.limits.maxPerStageResources;
    caps.max_update_after_bind_resources =
        descriptor_indexing_properties.maxPerStageUpdateAfterBindResources;

    caps.supported =
        caps.sampled_image_array_dynamic_indexing &&
        caps.runtime_descriptor_array &&
        caps.descriptor_binding_partially_bound &&
        caps.descriptor_binding_variable_descriptor_count &&
        caps.sampled_image_array_non_uniform_indexing;
    caps.sampler_update_after_bind =
        caps.max_update_after_bind_samplers > 0U &&
        caps.runtime_descriptor_array &&
        caps.descriptor_binding_partially_bound &&
        caps.descriptor_binding_variable_descriptor_count;
    caps.max_variable_descriptor_count = caps.max_sampled_image_slots;

    return caps;
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

void EnableRecommendedBindlessOptionalFeatures(VulkanDeviceCreateInfo& create_info_) noexcept {
    create_info_.required_features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    create_info_.optional_vulkan12_features.runtimeDescriptorArray = VK_TRUE;
    create_info_.optional_vulkan12_features.descriptorBindingPartiallyBound = VK_TRUE;
    create_info_.optional_vulkan12_features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    create_info_.optional_vulkan12_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    create_info_.optional_vulkan12_features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    create_info_.optional_vulkan12_features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
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
    descriptor_indexing_caps = std::exchange(other_.descriptor_indexing_caps, {});

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
    descriptor_indexing_caps = {};
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

const DescriptorIndexingCaps& VulkanContext::DescriptorIndexingCapsInfo() const noexcept {
    return descriptor_indexing_caps;
}

DescriptorSetLayoutSupportInfo VulkanContext::QueryDescriptorSetLayoutSupport(
    const VkDescriptorSetLayoutCreateInfo& create_info_) const {
    return QueryDescriptorSetLayoutSupportInfo(device, create_info_);
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
    VkPhysicalDeviceProperties best_device_properties{};

    VkPhysicalDeviceFeatures required_features = create_info_.required_features;
    NormalizeVulkan10FeatureBits(required_features);

    VkPhysicalDeviceVulkan12Features required_vulkan12_features = create_info_.required_vulkan12_features;
    required_vulkan12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    required_vulkan12_features.pNext = nullptr;
    NormalizeVulkan12FeatureBits(required_vulkan12_features);

    VkPhysicalDeviceVulkan13Features required_vulkan13_features = create_info_.required_vulkan13_features;
    required_vulkan13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    required_vulkan13_features.pNext = nullptr;
    NormalizeVulkan13FeatureBits(required_vulkan13_features);

    const bool needs_vulkan12_features =
        ShouldAppendVulkan12Features(required_vulkan12_features,
                                     create_info_.feature_chain_policy);
    const bool needs_vulkan13_features =
        ShouldAppendVulkan13Features(required_vulkan13_features,
                                     create_info_.feature_chain_policy);
    const auto get_features2_fn = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    if (get_features2_fn == nullptr &&
        (needs_vulkan12_features || needs_vulkan13_features || create_info_.required_features_pnext != nullptr)) {
        throw std::runtime_error(
            "Required Vulkan12/13 feature query unavailable: vkGetPhysicalDeviceFeatures2 is not supported");
    }

    std::ostringstream diagnostics_stream;
    diagnostics_stream << "[Vulkan] device selection diagnostics\n";
    diagnostics_stream << "  candidate_count=" << devices.size() << '\n';
    diagnostics_stream << "  surface_required=" << (surface != VK_NULL_HANDLE ? "true" : "false") << '\n';
    diagnostics_stream << "  require_dedicated_transfer_queue="
                       << (create_info_.require_dedicated_transfer_queue ? "true" : "false") << '\n';
    diagnostics_stream << "  required_device_extensions=";
    if (enabled_device_extensions.empty()) {
        diagnostics_stream << "<none>";
    } else {
        for (std::size_t i = 0U; i < enabled_device_extensions.size(); ++i) {
            diagnostics_stream << (i == 0U ? "" : ", ")
                               << enabled_device_extensions[i];
        }
    }
    diagnostics_stream << '\n';

    constexpr std::size_t vk10_feature_count =
        sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
    constexpr std::size_t vk12_first_feature_offset = Vulkan12FirstFeatureOffset();
    constexpr std::size_t vk12_feature_count = Vulkan12FeatureCount();
    constexpr std::size_t vk13_first_feature_offset = Vulkan13FirstFeatureOffset();
    constexpr std::size_t vk13_feature_count = Vulkan13FeatureCount();

    const auto* required_vk10_bits =
        reinterpret_cast<const VkBool32*>(&required_features);
    const auto* required_vk12_bits = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&required_vulkan12_features) + vk12_first_feature_offset);
    const auto* required_vk13_bits = reinterpret_cast<const VkBool32*>(
        reinterpret_cast<const char*>(&required_vulkan13_features) + vk13_first_feature_offset);

    diagnostics_stream << "  required_feature_bit_counts={vk10="
                       << CountEnabledFeatureBits(required_vk10_bits, vk10_feature_count)
                       << ", vk12="
                       << CountEnabledFeatureBits(required_vk12_bits, vk12_feature_count)
                       << ", vk13="
                       << CountEnabledFeatureBits(required_vk13_bits, vk13_feature_count)
                       << "}\n";
    diagnostics_stream << "  required_vulkan13_named={dynamicRendering="
                       << (required_vulkan13_features.dynamicRendering == VK_TRUE ? "1" : "0")
                       << ", synchronization2="
                       << (required_vulkan13_features.synchronization2 == VK_TRUE ? "1" : "0")
                       << "}\n";
    diagnostics_stream << "  feature_chain_policy="
                       << (create_info_.feature_chain_policy ==
                                   VulkanFeatureChainPolicy::explicit_vulkan12_vulkan13
                               ? "explicit_vulkan12_vulkan13"
                               : "minimal_required")
                       << '\n';

    auto queue_family_to_string = [](const std::optional<uint32_t>& value_) -> std::string {
        return value_.has_value() ? std::to_string(value_.value()) : std::string("none");
    };

    for (std::size_t candidate_index = 0U; candidate_index < devices.size(); ++candidate_index) {
        const VkPhysicalDevice candidate = devices[candidate_index];
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);

        diagnostics_stream << "  candidate[" << candidate_index << "] "
                           << properties.deviceName
                           << " (type=" << PhysicalDeviceTypeName(properties.deviceType)
                           << ", api=" << VK_VERSION_MAJOR(properties.apiVersion)
                           << "." << VK_VERSION_MINOR(properties.apiVersion)
                           << "." << VK_VERSION_PATCH(properties.apiVersion)
                           << ", vendor=0x" << std::hex << properties.vendorID
                           << ", device=0x" << properties.deviceID << std::dec << ")";

        std::ostringstream reject_reason_stream;
        bool candidate_supported = true;

        const QueueFamilyIndices indices = FindQueueFamilies(candidate, surface);
        const bool needs_present = surface != VK_NULL_HANDLE;
        diagnostics_stream << " queues{g=" << queue_family_to_string(indices.graphics)
                           << ", p=" << queue_family_to_string(indices.present)
                           << ", c=" << queue_family_to_string(indices.compute)
                           << ", t=" << queue_family_to_string(indices.transfer)
                           << "}";

        if (!indices.Complete(needs_present)) {
            candidate_supported = false;
            reject_reason_stream << "incomplete queue families for requested surface";
        }

        if (create_info_.require_dedicated_transfer_queue &&
            indices.transfer.has_value() &&
            indices.graphics.has_value() &&
            indices.transfer.value() == indices.graphics.value()) {
            if (!candidate_supported) {
                reject_reason_stream << "; ";
            }
            candidate_supported = false;
            reject_reason_stream << "dedicated transfer queue requested but transfer queue equals graphics queue";
        }

        const auto available_extensions = EnumerateDeviceExtensions(candidate);
        if (!HasRequiredExtensions(enabled_device_extensions, available_extensions)) {
            if (!candidate_supported) {
                reject_reason_stream << "; ";
            }
            candidate_supported = false;
            reject_reason_stream << "missing required device extensions: ";
            AppendMissingExtensions(reject_reason_stream,
                                    enabled_device_extensions,
                                    available_extensions);
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
            if (!SupportsRequiredFeatures(supported_features2.features, required_features)) {
                if (!candidate_supported) {
                    reject_reason_stream << "; ";
                }
                candidate_supported = false;
                reject_reason_stream << "missing required Vulkan 1.0 features: ";
                constexpr std::size_t feature_count =
                    sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
                const auto* required_bits =
                    reinterpret_cast<const VkBool32*>(&required_features);
                const auto* supported_bits =
                    reinterpret_cast<const VkBool32*>(&supported_features2.features);
                AppendMissingFeatureBits(reject_reason_stream,
                                         "VkPhysicalDeviceFeatures",
                                         required_bits,
                                         supported_bits,
                                         feature_count);
            }
            if (!SupportsRequiredVulkan12Features(supported_vulkan12_features,
                                                  required_vulkan12_features)) {
                if (!candidate_supported) {
                    reject_reason_stream << "; ";
                }
                candidate_supported = false;
                reject_reason_stream << "missing required Vulkan 1.2 features: ";
                constexpr std::size_t first_feature_offset = Vulkan12FirstFeatureOffset();
                constexpr std::size_t feature_count = Vulkan12FeatureCount();
                const auto* required_bits = reinterpret_cast<const VkBool32*>(
                    reinterpret_cast<const char*>(&required_vulkan12_features) + first_feature_offset);
                const auto* supported_bits = reinterpret_cast<const VkBool32*>(
                    reinterpret_cast<const char*>(&supported_vulkan12_features) + first_feature_offset);
                AppendMissingFeatureBits(reject_reason_stream,
                                         "VkPhysicalDeviceVulkan12Features",
                                         required_bits,
                                         supported_bits,
                                         feature_count);
            }
            if (!SupportsRequiredVulkan13Features(supported_vulkan13_features,
                                                  required_vulkan13_features)) {
                if (!candidate_supported) {
                    reject_reason_stream << "; ";
                }
                candidate_supported = false;
                reject_reason_stream << "missing required Vulkan 1.3 features: ";
                constexpr std::size_t first_feature_offset = Vulkan13FirstFeatureOffset();
                constexpr std::size_t feature_count = Vulkan13FeatureCount();
                const auto* required_bits = reinterpret_cast<const VkBool32*>(
                    reinterpret_cast<const char*>(&required_vulkan13_features) + first_feature_offset);
                const auto* supported_bits = reinterpret_cast<const VkBool32*>(
                    reinterpret_cast<const char*>(&supported_vulkan13_features) + first_feature_offset);
                AppendMissingFeatureBits(reject_reason_stream,
                                         "VkPhysicalDeviceVulkan13Features",
                                         required_bits,
                                         supported_bits,
                                         feature_count);
                reject_reason_stream << " | named: ";
                AppendMissingVulkan13NamedFeatures(reject_reason_stream,
                                                   required_vulkan13_features,
                                                   supported_vulkan13_features);
            }
        } else {
            VkPhysicalDeviceFeatures supported_features{};
            vkGetPhysicalDeviceFeatures(candidate, &supported_features);
            if (!SupportsRequiredFeatures(supported_features, required_features)) {
                if (!candidate_supported) {
                    reject_reason_stream << "; ";
                }
                candidate_supported = false;
                reject_reason_stream << "missing required Vulkan 1.0 features";
            }
        }

        if (!CheckSwapchainAdequate(candidate, surface)) {
            if (!candidate_supported) {
                reject_reason_stream << "; ";
            }
            candidate_supported = false;
            reject_reason_stream << "swapchain support inadequate (surface formats or present modes unavailable)";
        }

        if (!candidate_supported) {
            diagnostics_stream << " -> rejected: " << reject_reason_stream.str() << '\n';
            continue;
        }

        const int score = ScorePhysicalDevice(candidate);
        diagnostics_stream << " -> accepted(score=" << score << ")\n";
        if (score > best_score) {
            best_score = score;
            best_device = candidate;
            best_indices = indices;
            best_device_properties = properties;
        }
    }

    if (best_device == VK_NULL_HANDLE) {
        std::ostringstream oss;
        oss << "No suitable Vulkan physical device found\n"
            << diagnostics_stream.str()
            << "\nHint: enable 'VR_VK_LOG_DEVICE_SELECTION=1' for persistent startup logs.";
        throw std::runtime_error(oss.str());
    }

    physical_device = best_device;
    queue_family_indices = best_indices;
    descriptor_indexing_caps = QueryDescriptorIndexingCaps(instance, physical_device);

    if (DeviceSelectionVerboseLogEnabled()) {
        std::cerr << diagnostics_stream.str();
        std::cerr << "[Vulkan] selected physical device: " << best_device_properties.deviceName
                  << " (score=" << best_score
                  << ", graphics_queue=" << queue_family_to_string(best_indices.graphics)
                  << ", present_queue=" << queue_family_to_string(best_indices.present)
                  << ", compute_queue=" << queue_family_to_string(best_indices.compute)
                  << ", transfer_queue=" << queue_family_to_string(best_indices.transfer)
                  << ")\n";
    }
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

    VkPhysicalDeviceFeatures normalized_required_features = create_info_.required_features;
    NormalizeVulkan10FeatureBits(normalized_required_features);

    VkPhysicalDeviceFeatures2 enabled_features2{};
    enabled_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enabled_features2.features = normalized_required_features;
    enabled_features2.pNext = nullptr;

    VkPhysicalDeviceVulkan12Features enabled_vulkan12_features_local = create_info_.required_vulkan12_features;
    enabled_vulkan12_features_local.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enabled_vulkan12_features_local.pNext = nullptr;
    NormalizeVulkan12FeatureBits(enabled_vulkan12_features_local);

    VkPhysicalDeviceVulkan12Features optional_vulkan12_features_local = create_info_.optional_vulkan12_features;
    optional_vulkan12_features_local.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    optional_vulkan12_features_local.pNext = nullptr;
    NormalizeVulkan12FeatureBits(optional_vulkan12_features_local);

    VkPhysicalDeviceVulkan13Features enabled_vulkan13_features_local = create_info_.required_vulkan13_features;
    enabled_vulkan13_features_local.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enabled_vulkan13_features_local.pNext = nullptr;
    NormalizeVulkan13FeatureBits(enabled_vulkan13_features_local);

    VkPhysicalDeviceVulkan13Features optional_vulkan13_features_local = create_info_.optional_vulkan13_features;
    optional_vulkan13_features_local.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    optional_vulkan13_features_local.pNext = nullptr;
    NormalizeVulkan13FeatureBits(optional_vulkan13_features_local);

    const bool needs_required_vulkan12_features =
        HasRequiredVulkan12Features(enabled_vulkan12_features_local);
    const bool needs_required_vulkan13_features =
        HasRequiredVulkan13Features(enabled_vulkan13_features_local);
    const bool wants_optional_vulkan12_features =
        HasRequiredVulkan12Features(optional_vulkan12_features_local);
    const bool wants_optional_vulkan13_features =
        HasRequiredVulkan13Features(optional_vulkan13_features_local);
    const auto get_features2_fn =
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    if (get_features2_fn == nullptr &&
        (needs_required_vulkan12_features ||
         needs_required_vulkan13_features ||
         wants_optional_vulkan12_features ||
         wants_optional_vulkan13_features ||
         create_info_.required_features_pnext != nullptr)) {
        throw std::runtime_error(
            "Required Vulkan12/13 feature query unavailable: vkGetPhysicalDeviceFeatures2 is not supported");
    }

    if (get_features2_fn != nullptr &&
        (needs_required_vulkan12_features ||
         needs_required_vulkan13_features ||
         wants_optional_vulkan12_features ||
         wants_optional_vulkan13_features)) {
        VkPhysicalDeviceFeatures2 supported_features2{};
        supported_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

        VkPhysicalDeviceVulkan12Features supported_vulkan12_features{};
        supported_vulkan12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

        VkPhysicalDeviceVulkan13Features supported_vulkan13_features{};
        supported_vulkan13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        supported_features2.pNext = &supported_vulkan12_features;
        supported_vulkan12_features.pNext = &supported_vulkan13_features;
        supported_vulkan13_features.pNext = nullptr;
        get_features2_fn(physical_device, &supported_features2);

        MergeSupportedOptionalVulkan12Features(enabled_vulkan12_features_local,
                                               optional_vulkan12_features_local,
                                               supported_vulkan12_features);
        MergeSupportedOptionalVulkan13Features(enabled_vulkan13_features_local,
                                               optional_vulkan13_features_local,
                                               supported_vulkan13_features);
    }

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

    if (ShouldAppendVulkan12Features(enabled_vulkan12_features_local,
                                     create_info_.feature_chain_policy)) {
        append_feature_chain(reinterpret_cast<VkBaseOutStructure*>(&enabled_vulkan12_features_local));
    }
    if (ShouldAppendVulkan13Features(enabled_vulkan13_features_local,
                                     create_info_.feature_chain_policy)) {
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

    enabled_features = normalized_required_features;
    enabled_vulkan12_features = enabled_vulkan12_features_local;
    enabled_vulkan12_features.pNext = nullptr;
    enabled_vulkan13_features = enabled_vulkan13_features_local;
    enabled_vulkan13_features.pNext = nullptr;
    descriptor_indexing_caps.runtime_descriptor_array =
        enabled_vulkan12_features.runtimeDescriptorArray == VK_TRUE;
    descriptor_indexing_caps.descriptor_binding_partially_bound =
        enabled_vulkan12_features.descriptorBindingPartiallyBound == VK_TRUE;
    descriptor_indexing_caps.descriptor_binding_variable_descriptor_count =
        enabled_vulkan12_features.descriptorBindingVariableDescriptorCount == VK_TRUE;
    descriptor_indexing_caps.sampled_image_array_non_uniform_indexing =
        enabled_vulkan12_features.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    descriptor_indexing_caps.sampler_array_non_uniform_indexing =
        enabled_vulkan12_features.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    descriptor_indexing_caps.sampled_image_update_after_bind =
        enabled_vulkan12_features.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE;
    descriptor_indexing_caps.sampled_image_array_dynamic_indexing =
        enabled_features.shaderSampledImageArrayDynamicIndexing == VK_TRUE;
    descriptor_indexing_caps.update_unused_while_pending =
        enabled_vulkan12_features.descriptorBindingUpdateUnusedWhilePending == VK_TRUE;
    descriptor_indexing_caps.enabled =
        enabled_features.shaderSampledImageArrayDynamicIndexing == VK_TRUE &&
        enabled_vulkan12_features.runtimeDescriptorArray == VK_TRUE &&
        enabled_vulkan12_features.descriptorBindingPartiallyBound == VK_TRUE &&
        enabled_vulkan12_features.descriptorBindingVariableDescriptorCount == VK_TRUE &&
        enabled_vulkan12_features.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    descriptor_indexing_caps.sampler_update_after_bind =
        descriptor_indexing_caps.max_update_after_bind_samplers > 0U &&
        descriptor_indexing_caps.enabled;
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

