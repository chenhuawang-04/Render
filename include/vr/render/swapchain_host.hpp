#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/frame_retire_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vr::render {

template<typename T>
using McVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

template<typename WindowSurfaceT>
concept WindowSurfaceBridge = requires(const WindowSurfaceT& window_surface_, int& width_, int& height_) {
    { window_surface_.Surface() } -> std::same_as<VkSurfaceKHR>;
    { window_surface_.QueryFramebufferSize(width_, height_) };
};

struct SwapchainCreateInfo {
    bool enable_vsync = true;
    bool prefer_mailbox = true;
    uint32_t preferred_image_count = 3U;
    VkFormat preferred_format = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR preferred_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkImageUsageFlags image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // 在销毁旧 swapchain 前显式等待设备空闲（低频保守路径；重建/关闭时优先保证正确性）。
    bool wait_queue_idle_before_destroy = false;
    // 启用延迟回收：若提供 FrameRetireHost，则旧 swapchain / image view / framebuffer 延迟销毁。
    bool enable_deferred_retire = true;
    bool create_framebuffers = false;
    VkRenderPass render_pass = VK_NULL_HANDLE;
};

struct SwapchainImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

struct AcquireResult {
    VkResult result = VK_SUCCESS;
    uint32_t image_index = 0U;
    bool need_recreate = false;
};

struct PresentResult {
    VkResult result = VK_SUCCESS;
    bool need_recreate = false;
};

template<WindowSurfaceBridge WindowSurfaceT>
class SwapchainHost final {
public:
    SwapchainHost() = default;
    ~SwapchainHost() = default;

    SwapchainHost(const SwapchainHost&) = delete;
    SwapchainHost& operator=(const SwapchainHost&) = delete;

    SwapchainHost(SwapchainHost&&) = delete;
    SwapchainHost& operator=(SwapchainHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const WindowSurfaceT& window_surface_,
                    const SwapchainCreateInfo& create_info_ = {}) {
        Initialize(context_, window_surface_, create_info_, nullptr, 0U);
    }

    void Initialize(VulkanContext& context_,
                    const WindowSurfaceT& window_surface_,
                    const SwapchainCreateInfo& create_info_,
                    FrameRetireHost* retire_host_,
                    uint64_t retire_value_) {
        if (!context_.IsDeviceInitialized()) {
            throw std::runtime_error("SwapchainHost::Initialize requires initialized Vulkan device");
        }
        if (window_surface_.Surface() == VK_NULL_HANDLE) {
            throw std::runtime_error("SwapchainHost::Initialize requires valid VkSurfaceKHR");
        }

        create_info_cache = create_info_;
        Recreate(context_, window_surface_, create_info_, retire_host_, retire_value_);
        dirty = false;
        suboptimal_seen = false;
    }

    void Shutdown(VulkanContext& context_) {
        Shutdown(context_, nullptr, 0U);
    }

    void Shutdown(VulkanContext& context_,
                  FrameRetireHost* retire_host_,
                  uint64_t retire_value_) {
        const VkDevice device_ = context_.Device();
        if (device_ == VK_NULL_HANDLE) {
            images.clear();
            swapchain = VK_NULL_HANDLE;
            format = VK_FORMAT_UNDEFINED;
            color_space = VK_COLOR_SPACE_MAX_ENUM_KHR;
            extent = {};
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
            dirty = true;
            suboptimal_seen = false;
            return;
        }

        RetireOrDestroyResources(context_,
                                 images,
                                 swapchain,
                                 create_info_cache,
                                 retire_host_,
                                 retire_value_);
        images.clear();
        swapchain = VK_NULL_HANDLE;

        format = VK_FORMAT_UNDEFINED;
        color_space = VK_COLOR_SPACE_MAX_ENUM_KHR;
        extent = {};
        present_mode = VK_PRESENT_MODE_FIFO_KHR;
        dirty = true;
        suboptimal_seen = false;
    }

    [[nodiscard]] bool EnsureValid(VulkanContext& context_,
                                   const WindowSurfaceT& window_surface_) {
        return EnsureValid(context_, window_surface_, create_info_cache, nullptr, 0U);
    }

    [[nodiscard]] bool EnsureValid(VulkanContext& context_,
                                   const WindowSurfaceT& window_surface_,
                                   const SwapchainCreateInfo& create_info_) {
        return EnsureValid(context_, window_surface_, create_info_, nullptr, 0U);
    }

    [[nodiscard]] bool EnsureValid(VulkanContext& context_,
                                   const WindowSurfaceT& window_surface_,
                                   const SwapchainCreateInfo& create_info_,
                                   FrameRetireHost* retire_host_,
                                   uint64_t retire_value_) {
        if (!IsValid()) {
            create_info_cache = create_info_;
            Recreate(context_, window_surface_, create_info_, retire_host_, retire_value_);
            dirty = false;
            suboptimal_seen = false;
            return true;
        }

        if (!NeedsRecreate()) {
            return true;
        }

        int width = 0;
        int height = 0;
        window_surface_.QueryFramebufferSize(width, height);
        if (width <= 0 || height <= 0) {
            return false;
        }

        create_info_cache = create_info_;
        Recreate(context_, window_surface_, create_info_, retire_host_, retire_value_);
        dirty = false;
        suboptimal_seen = false;
        return true;
    }

    [[nodiscard]] AcquireResult AcquireNextImage(VulkanContext& context_,
                                                 VkSemaphore image_available_semaphore_,
                                                 VkFence fence_ = VK_NULL_HANDLE,
                                                 uint64_t timeout_ns_ = std::numeric_limits<uint64_t>::max()) {
        AcquireResult result{};
        if (swapchain == VK_NULL_HANDLE) {
            result.result = VK_ERROR_INITIALIZATION_FAILED;
            result.need_recreate = true;
            return result;
        }

        result.result = vkAcquireNextImageKHR(context_.Device(),
                                              swapchain,
                                              timeout_ns_,
                                              image_available_semaphore_,
                                              fence_,
                                              &result.image_index);
        if (result.result == VK_SUCCESS) {
            return result;
        }
        if (result.result == VK_SUBOPTIMAL_KHR) {
            suboptimal_seen = true;
            dirty = true;
            result.need_recreate = true;
            return result;
        }
        if (result.result == VK_ERROR_OUT_OF_DATE_KHR) {
            dirty = true;
            result.need_recreate = true;
            return result;
        }

        ThrowVk("vkAcquireNextImageKHR", result.result);
        return result;
    }

    [[nodiscard]] PresentResult Present(VulkanContext& context_,
                                        uint32_t image_index_,
                                        VkSemaphore render_finished_semaphore_ = VK_NULL_HANDLE) {
        PresentResult result{};
        if (swapchain == VK_NULL_HANDLE) {
            result.result = VK_ERROR_INITIALIZATION_FAILED;
            result.need_recreate = true;
            return result;
        }

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = (render_finished_semaphore_ != VK_NULL_HANDLE) ? 1U : 0U;
        present_info.pWaitSemaphores = (render_finished_semaphore_ != VK_NULL_HANDLE)
            ? &render_finished_semaphore_
            : nullptr;
        present_info.swapchainCount = 1U;
        present_info.pSwapchains = &swapchain;
        present_info.pImageIndices = &image_index_;
        present_info.pResults = nullptr;

        result.result = vkQueuePresentKHR(context_.PresentQueue(), &present_info);
        if (result.result == VK_SUCCESS) {
            return result;
        }
        if (result.result == VK_SUBOPTIMAL_KHR) {
            suboptimal_seen = true;
            dirty = true;
            result.need_recreate = true;
            return result;
        }
        if (result.result == VK_ERROR_OUT_OF_DATE_KHR) {
            dirty = true;
            result.need_recreate = true;
            return result;
        }

        ThrowVk("vkQueuePresentKHR", result.result);
        return result;
    }

    void MarkDirty() noexcept {
        dirty = true;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return swapchain != VK_NULL_HANDLE && !images.empty();
    }

    [[nodiscard]] bool NeedsRecreate() const noexcept {
        return dirty || suboptimal_seen;
    }

    [[nodiscard]] VkSwapchainKHR Handle() const noexcept {
        return swapchain;
    }

    [[nodiscard]] VkFormat Format() const noexcept {
        return format;
    }

    [[nodiscard]] VkColorSpaceKHR ColorSpace() const noexcept {
        return color_space;
    }

    [[nodiscard]] VkExtent2D Extent() const noexcept {
        return extent;
    }

    [[nodiscard]] VkPresentModeKHR PresentMode() const noexcept {
        return present_mode;
    }

    [[nodiscard]] uint32_t ImageCount() const noexcept {
        return static_cast<uint32_t>(images.size());
    }

    [[nodiscard]] uint64_t Generation() const noexcept {
        return generation;
    }

    [[nodiscard]] VkImage Image(uint32_t image_index_) const noexcept {
        return images[image_index_].image;
    }

    [[nodiscard]] VkImageView ImageView(uint32_t image_index_) const noexcept {
        return images[image_index_].view;
    }

    [[nodiscard]] VkFramebuffer Framebuffer(uint32_t image_index_) const noexcept {
        return images[image_index_].framebuffer;
    }

private:
    struct SwapchainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities{};
        McVector<VkSurfaceFormatKHR> formats{};
        McVector<VkPresentModeKHR> present_modes{};
    };

    [[nodiscard]] static const char* VkResultName(VkResult result_) noexcept {
        switch (result_) {
            case VK_SUCCESS: return "VK_SUCCESS";
            case VK_NOT_READY: return "VK_NOT_READY";
            case VK_TIMEOUT: return "VK_TIMEOUT";
            case VK_EVENT_SET: return "VK_EVENT_SET";
            case VK_EVENT_RESET: return "VK_EVENT_RESET";
            case VK_INCOMPLETE: return "VK_INCOMPLETE";
            case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
            case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
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
            case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
            default: return "VK_ERROR_UNKNOWN";
        }
    }

    static void ThrowVk(const char* stage_, VkResult result_) {
        std::ostringstream oss;
        oss << stage_ << " failed: " << VkResultName(result_) << " (" << static_cast<int>(result_) << ")";
        throw std::runtime_error(oss.str());
    }

    static void CheckVk(const char* stage_, VkResult result_) {
        if (result_ != VK_SUCCESS) {
            ThrowVk(stage_, result_);
        }
    }

    [[nodiscard]] static SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice physical_device_,
                                                                       VkSurfaceKHR surface_) {
        SwapchainSupportDetails details{};
        CheckVk("vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &details.capabilities));

        uint32_t format_count = 0U;
        CheckVk("vkGetPhysicalDeviceSurfaceFormatsKHR(count)",
                vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr));
        details.formats.resize(format_count);
        if (format_count > 0U) {
            CheckVk("vkGetPhysicalDeviceSurfaceFormatsKHR(data)",
                    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_,
                                                         surface_,
                                                         &format_count,
                                                         details.formats.data()));
        }

        uint32_t mode_count = 0U;
        CheckVk("vkGetPhysicalDeviceSurfacePresentModesKHR(count)",
                vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &mode_count, nullptr));
        details.present_modes.resize(mode_count);
        if (mode_count > 0U) {
            CheckVk("vkGetPhysicalDeviceSurfacePresentModesKHR(data)",
                    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_,
                                                              surface_,
                                                              &mode_count,
                                                              details.present_modes.data()));
        }

        return details;
    }

    [[nodiscard]] static VkSurfaceFormatKHR ChooseSurfaceFormat(const McVector<VkSurfaceFormatKHR>& available_formats_,
                                                                const SwapchainCreateInfo& create_info_) {
        for (const auto& fmt : available_formats_) {
            if (fmt.format == create_info_.preferred_format &&
                fmt.colorSpace == create_info_.preferred_color_space) {
                return fmt;
            }
        }

        for (const auto& fmt : available_formats_) {
            if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM &&
                fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return fmt;
            }
        }

        for (const auto& fmt : available_formats_) {
            if (fmt.format == VK_FORMAT_R8G8B8A8_UNORM &&
                fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return fmt;
            }
        }

        if (available_formats_.empty()) {
            throw std::runtime_error("No surface formats available");
        }
        return available_formats_.front();
    }

    [[nodiscard]] static VkPresentModeKHR ChoosePresentMode(const McVector<VkPresentModeKHR>& available_modes_,
                                                            const SwapchainCreateInfo& create_info_) {
        if (!create_info_.enable_vsync) {
            if (create_info_.prefer_mailbox) {
                for (VkPresentModeKHR mode : available_modes_) {
                    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                        return mode;
                    }
                }
            }
            for (VkPresentModeKHR mode : available_modes_) {
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    return mode;
                }
            }
            for (VkPresentModeKHR mode : available_modes_) {
                if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                    return mode;
                }
            }
        }

        for (VkPresentModeKHR mode : available_modes_) {
            if (mode == VK_PRESENT_MODE_FIFO_KHR) {
                return mode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    [[nodiscard]] static VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities_,
                                                 const WindowSurfaceT& window_surface_) {
        if (capabilities_.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities_.currentExtent;
        }

        int width = 0;
        int height = 0;
        window_surface_.QueryFramebufferSize(width, height);
        if (width <= 0 || height <= 0) {
            throw std::runtime_error("Framebuffer size is zero while creating swapchain");
        }

        VkExtent2D actual{};
        actual.width = static_cast<uint32_t>(width);
        actual.height = static_cast<uint32_t>(height);

        actual.width = std::clamp(actual.width,
                                  capabilities_.minImageExtent.width,
                                  capabilities_.maxImageExtent.width);
        actual.height = std::clamp(actual.height,
                                   capabilities_.minImageExtent.height,
                                   capabilities_.maxImageExtent.height);
        return actual;
    }

    [[nodiscard]] static uint32_t ChooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities_,
                                                   const SwapchainCreateInfo& create_info_) {
        uint32_t target = std::max(create_info_.preferred_image_count, capabilities_.minImageCount);
        if (capabilities_.maxImageCount > 0U) {
            target = std::min(target, capabilities_.maxImageCount);
        }
        return target;
    }

    [[nodiscard]] static VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(const VkSurfaceCapabilitiesKHR& capabilities_,
                                                                           VkCompositeAlphaFlagBitsKHR preferred_) {
        if ((capabilities_.supportedCompositeAlpha & preferred_) != 0U) {
            return preferred_;
        }

        constexpr VkCompositeAlphaFlagBitsKHR fallbacks[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
        };
        for (VkCompositeAlphaFlagBitsKHR candidate : fallbacks) {
            if ((capabilities_.supportedCompositeAlpha & candidate) != 0U) {
                return candidate;
            }
        }
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    static void DestroyImageResources(VkDevice device_,
                                      McVector<SwapchainImage>& images_) {
        for (auto& img : images_) {
            if (img.framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device_, img.framebuffer, nullptr);
                img.framebuffer = VK_NULL_HANDLE;
            }
            if (img.view != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, img.view, nullptr);
                img.view = VK_NULL_HANDLE;
            }
            img.image = VK_NULL_HANDLE;
        }
    }

    static void WaitDeviceIdleForDestroy(VulkanContext& context_) {
        const VkDevice device_ = context_.Device();
        if (device_ == VK_NULL_HANDLE) {
            return;
        }
        CheckVk("vkDeviceWaitIdle(swapchain destroy fallback)", vkDeviceWaitIdle(device_));
    }

    static void RetireOrDestroyResources(VulkanContext& context_,
                                         McVector<SwapchainImage>& images_,
                                         VkSwapchainKHR swapchain_,
                                         const SwapchainCreateInfo& create_info_,
                                         FrameRetireHost* retire_host_,
                                         uint64_t retire_value_) {
        const VkDevice device_ = context_.Device();
        if (device_ == VK_NULL_HANDLE) {
            images_.clear();
            return;
        }

        const bool can_retire = create_info_.enable_deferred_retire &&
                                !create_info_.wait_queue_idle_before_destroy &&
                                retire_host_ != nullptr;

        if (can_retire) {
            for (auto& img : images_) {
                if (img.framebuffer != VK_NULL_HANDLE) {
                    retire_host_->RetireFramebuffer(img.framebuffer, retire_value_);
                    img.framebuffer = VK_NULL_HANDLE;
                }
                if (img.view != VK_NULL_HANDLE) {
                    retire_host_->RetireImageView(img.view, retire_value_);
                    img.view = VK_NULL_HANDLE;
                }
                img.image = VK_NULL_HANDLE;
            }
            if (swapchain_ != VK_NULL_HANDLE) {
                retire_host_->RetireSwapchain(swapchain_, retire_value_);
            }
            return;
        }

        if (!images_.empty() || swapchain_ != VK_NULL_HANDLE) {
            WaitDeviceIdleForDestroy(context_);
        }

        DestroyImageResources(device_, images_);
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        }
    }

    void Recreate(VulkanContext& context_,
                  const WindowSurfaceT& window_surface_,
                  const SwapchainCreateInfo& create_info_,
                  FrameRetireHost* retire_host_,
                  uint64_t retire_value_) {
        if (!context_.IsDeviceInitialized()) {
            throw std::runtime_error("SwapchainHost::Recreate requires initialized Vulkan device");
        }

        const VkPhysicalDevice physical_device_ = context_.PhysicalDevice();
        const VkDevice device_ = context_.Device();
        const VkSurfaceKHR surface_ = window_surface_.Surface();
        if (physical_device_ == VK_NULL_HANDLE || device_ == VK_NULL_HANDLE || surface_ == VK_NULL_HANDLE) {
            throw std::runtime_error("SwapchainHost::Recreate requires physical device, device and surface");
        }

        if (create_info_.create_framebuffers && create_info_.render_pass == VK_NULL_HANDLE) {
            throw std::runtime_error("SwapchainCreateInfo.create_framebuffers=true requires a valid render_pass");
        }

        const SwapchainSupportDetails support = QuerySwapchainSupport(physical_device_, surface_);
        if (support.formats.empty() || support.present_modes.empty()) {
            throw std::runtime_error("Swapchain support incomplete: no surface formats or present modes");
        }

        const VkSurfaceFormatKHR selected_format = ChooseSurfaceFormat(support.formats, create_info_);
        const VkPresentModeKHR selected_present_mode = ChoosePresentMode(support.present_modes, create_info_);
        const VkExtent2D selected_extent = ChooseExtent(support.capabilities, window_surface_);
        const uint32_t selected_image_count = ChooseImageCount(support.capabilities, create_info_);
        const VkCompositeAlphaFlagBitsKHR selected_composite_alpha =
            ChooseCompositeAlpha(support.capabilities, create_info_.composite_alpha);

        const auto& families = context_.QueueFamilies();
        uint32_t queue_family_indices[2] = {
            families.graphics.value(),
            families.present.value()
        };

        VkSwapchainCreateInfoKHR swapchain_create_info{};
        swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchain_create_info.surface = surface_;
        swapchain_create_info.minImageCount = selected_image_count;
        swapchain_create_info.imageFormat = selected_format.format;
        swapchain_create_info.imageColorSpace = selected_format.colorSpace;
        swapchain_create_info.imageExtent = selected_extent;
        swapchain_create_info.imageArrayLayers = 1U;
        swapchain_create_info.imageUsage = create_info_.image_usage;
        if (families.graphics.value() != families.present.value()) {
            swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchain_create_info.queueFamilyIndexCount = 2U;
            swapchain_create_info.pQueueFamilyIndices = queue_family_indices;
        } else {
            swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchain_create_info.queueFamilyIndexCount = 0U;
            swapchain_create_info.pQueueFamilyIndices = nullptr;
        }
        swapchain_create_info.preTransform = (support.capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
            : support.capabilities.currentTransform;
        swapchain_create_info.compositeAlpha = selected_composite_alpha;
        swapchain_create_info.presentMode = selected_present_mode;
        swapchain_create_info.clipped = VK_TRUE;
        swapchain_create_info.oldSwapchain = swapchain;

        VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
        CheckVk("vkCreateSwapchainKHR",
                vkCreateSwapchainKHR(device_, &swapchain_create_info, nullptr, &new_swapchain));

        uint32_t image_count = 0U;
        CheckVk("vkGetSwapchainImagesKHR(count)",
                vkGetSwapchainImagesKHR(device_, new_swapchain, &image_count, nullptr));
        if (image_count == 0U) {
            vkDestroySwapchainKHR(device_, new_swapchain, nullptr);
            throw std::runtime_error("Swapchain image count is zero");
        }

        McVector<VkImage> vk_images;
        vk_images.resize(image_count);
        CheckVk("vkGetSwapchainImagesKHR(data)",
                vkGetSwapchainImagesKHR(device_, new_swapchain, &image_count, vk_images.data()));

        McVector<SwapchainImage> new_images;
        new_images.resize(image_count);

        try {
            for (uint32_t i = 0U; i < image_count; ++i) {
                new_images[i].image = vk_images[i];

                VkImageViewCreateInfo view_create_info{};
                view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                view_create_info.image = vk_images[i];
                view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                view_create_info.format = selected_format.format;
                view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
                view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
                view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
                view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
                view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                view_create_info.subresourceRange.baseMipLevel = 0U;
                view_create_info.subresourceRange.levelCount = 1U;
                view_create_info.subresourceRange.baseArrayLayer = 0U;
                view_create_info.subresourceRange.layerCount = 1U;

                CheckVk("vkCreateImageView",
                        vkCreateImageView(device_, &view_create_info, nullptr, &new_images[i].view));

                if (create_info_.create_framebuffers) {
                    VkFramebufferCreateInfo framebuffer_create_info{};
                    framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                    framebuffer_create_info.renderPass = create_info_.render_pass;
                    framebuffer_create_info.attachmentCount = 1U;
                    framebuffer_create_info.pAttachments = &new_images[i].view;
                    framebuffer_create_info.width = selected_extent.width;
                    framebuffer_create_info.height = selected_extent.height;
                    framebuffer_create_info.layers = 1U;
                    CheckVk("vkCreateFramebuffer",
                            vkCreateFramebuffer(device_,
                                                &framebuffer_create_info,
                                                nullptr,
                                                &new_images[i].framebuffer));
                }
            }
        } catch (...) {
            DestroyImageResources(device_, new_images);
            vkDestroySwapchainKHR(device_, new_swapchain, nullptr);
            throw;
        }

        VkSwapchainKHR old_swapchain = swapchain;
        McVector<SwapchainImage> old_images = std::move(images);

        swapchain = new_swapchain;
        format = selected_format.format;
        color_space = selected_format.colorSpace;
        extent = selected_extent;
        present_mode = selected_present_mode;
        images = std::move(new_images);
        ++generation;

        RetireOrDestroyResources(context_,
                                 old_images,
                                 old_swapchain,
                                 create_info_,
                                 retire_host_,
                                 retire_value_);
    }

private:
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR color_space = VK_COLOR_SPACE_MAX_ENUM_KHR;
    VkExtent2D extent{};
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    McVector<SwapchainImage> images{};
    uint64_t generation = 0U;
    bool dirty = true;
    bool suboptimal_seen = false;
    SwapchainCreateInfo create_info_cache{};
};

} // namespace vr::render


