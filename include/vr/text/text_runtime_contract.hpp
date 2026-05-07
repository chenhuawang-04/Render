#pragma once

#include "vr/render/runtime_prepare_context.hpp"
#include "vr/vulkan_context.hpp"

#include <concepts>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace vr::text {

inline void ApplyTextRuntimeFeatureContract(vr::VulkanDeviceCreateInfo& device_info_) noexcept {
    device_info_.required_vulkan13_features.dynamicRendering = VK_TRUE;
    device_info_.required_vulkan13_features.synchronization2 = VK_TRUE;
}

template<typename CreateInfoT>
concept TextRuntimeDeviceCreateInfoLike = requires(CreateInfoT& create_info_) {
    create_info_.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
    create_info_.device.required_vulkan13_features.synchronization2 = VK_TRUE;
};

template<typename CreateInfoT>
concept TextRuntimeCreateInfoLike = requires(CreateInfoT& create_info_) {
    create_info_.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
    create_info_.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
};

template<TextRuntimeDeviceCreateInfoLike CreateInfoT>
inline void ApplyTextRuntimeFeatureContract(CreateInfoT& create_info_) noexcept {
    ApplyTextRuntimeFeatureContract(create_info_.device);
}

template<TextRuntimeCreateInfoLike RuntimeCreateInfoT>
inline void ApplyTextRuntimeFeatureContract(RuntimeCreateInfoT& create_info_) noexcept {
    ApplyTextRuntimeFeatureContract(create_info_.platform.device);
}

template<typename RuntimeCreateInfoT>
requires std::default_initializable<RuntimeCreateInfoT> && TextRuntimeCreateInfoLike<RuntimeCreateInfoT>
[[nodiscard]] inline RuntimeCreateInfoT MakeDefaultTextRuntimeCreateInfo() noexcept {
    RuntimeCreateInfoT create_info{};
    ApplyTextRuntimeFeatureContract(create_info);
    return create_info;
}

inline void RequireTextRuntimeFeatures(const vr::VulkanContext& context_,
                                       std::string_view caller_) {
    const auto& enabled_vulkan13_features = context_.EnabledVulkan13Features();

    std::ostringstream missing_stream{};
    bool missing_any = false;
    auto append_missing = [&](std::string_view name_) {
        if (missing_any) {
            missing_stream << ", ";
        }
        missing_stream << name_;
        missing_any = true;
    };

    if (enabled_vulkan13_features.dynamicRendering != VK_TRUE) {
        append_missing("dynamicRendering");
    }
    if (enabled_vulkan13_features.synchronization2 != VK_TRUE) {
        append_missing("synchronization2");
    }

    if (!missing_any) {
        return;
    }

    std::ostringstream oss{};
    oss << caller_
        << " requires Vulkan 1.3 features {"
        << missing_stream.str()
        << "}; call vr::text::ApplyTextRuntimeFeatureContract(...) or enable both "
           "required_vulkan13_features.dynamicRendering and "
           "required_vulkan13_features.synchronization2";
    throw std::runtime_error(oss.str());
}

inline void ValidateTextRuntimePrepareContext(const vr::render::RuntimePrepareContext& prepare_context_,
                                              std::string_view caller_) {
    std::ostringstream missing_stream{};
    bool missing_any = false;
    auto append_missing = [&](std::string_view name_) {
        if (missing_any) {
            missing_stream << ", ";
        }
        missing_stream << name_;
        missing_any = true;
    };

    if (prepare_context_.context == nullptr) {
        append_missing("context");
    }
    if (prepare_context_.upload_host == nullptr) {
        append_missing("upload_host");
    }
    if (prepare_context_.descriptor_host == nullptr) {
        append_missing("descriptor_host");
    }
    if (prepare_context_.pipeline_host == nullptr) {
        append_missing("pipeline_host");
    }
    if (prepare_context_.gpu_memory_host == nullptr) {
        append_missing("gpu_memory_host");
    }
    if (prepare_context_.freetype_host == nullptr) {
        append_missing("freetype_host");
    }
    if (prepare_context_.glyph_atlas_host == nullptr) {
        append_missing("glyph_atlas_host");
    }
    if (prepare_context_.glyph_upload_host == nullptr) {
        append_missing("glyph_upload_host");
    }

    if (missing_any) {
        std::ostringstream oss{};
        oss << caller_ << " requires runtime text modules enabled: {" << missing_stream.str() << "}";
        throw std::runtime_error(oss.str());
    }

    RequireTextRuntimeFeatures(*prepare_context_.context, caller_);
}

} // namespace vr::text
