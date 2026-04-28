module;
// Global module fragment
#include "vr/detail/vr_module_fwd.hpp"
#include "Center/Memory/Vulkan/Types.hpp"
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

export module vr.render;
import vr.types;
import vr.context;
import vr.resource;

export {
namespace vr::render {

// Centralized McVector alias (replaces local DescriptorMcVector)
template<typename T>
using DescriptorMcVector = vr::McVector<T>;

// Forward declare FrameRetireHost (defined below)
class FrameRetireHost;

// --- frame_retire_host.hpp --------------------------------------------------

struct FrameRetireStats {
    uint32_t destroyed_image_views = 0U;
    uint32_t destroyed_framebuffers = 0U;
    uint32_t destroyed_swapchains = 0U;
    uint32_t destroyed_command_pools = 0U;
};

class FrameRetireHost final {
public:
    FrameRetireHost() = default;
    ~FrameRetireHost() = default;

    FrameRetireHost(const FrameRetireHost&) = delete;
    FrameRetireHost& operator=(const FrameRetireHost&) = delete;

    FrameRetireHost(FrameRetireHost&&) = delete;
    FrameRetireHost& operator=(FrameRetireHost&&) = delete;

    void RetireImageView(VkImageView image_view_, uint64_t retire_value_) {
        if (image_view_ == VK_NULL_HANDLE) {
            return;
        }
        image_views.push_back({.retire_value = retire_value_, .handle = image_view_});
    }

    void RetireFramebuffer(VkFramebuffer framebuffer_, uint64_t retire_value_) {
        if (framebuffer_ == VK_NULL_HANDLE) {
            return;
        }
        framebuffers.push_back({.retire_value = retire_value_, .handle = framebuffer_});
    }

    void RetireSwapchain(VkSwapchainKHR swapchain_, uint64_t retire_value_) {
        if (swapchain_ == VK_NULL_HANDLE) {
            return;
        }
        swapchains.push_back({.retire_value = retire_value_, .handle = swapchain_});
    }

    void RetireCommandPool(VkCommandPool command_pool_, uint64_t retire_value_) {
        if (command_pool_ == VK_NULL_HANDLE) {
            return;
        }
        command_pools.push_back({.retire_value = retire_value_, .handle = command_pool_});
    }

    [[nodiscard]] FrameRetireStats Collect(VkDevice device_,
                                           uint64_t completed_value_,
                                           uint32_t max_destroy_per_type_ = std::numeric_limits<uint32_t>::max()) {
        if (device_ == VK_NULL_HANDLE) {
            return {};
        }

        FrameRetireStats stats{};
        stats.destroyed_image_views = CollectTyped<RetiredImageView>(
            image_views,
            completed_value_,
            max_destroy_per_type_,
            [&](VkImageView handle_) { vkDestroyImageView(device_, handle_, nullptr); });

        stats.destroyed_framebuffers = CollectTyped<RetiredFramebuffer>(
            framebuffers,
            completed_value_,
            max_destroy_per_type_,
            [&](VkFramebuffer handle_) { vkDestroyFramebuffer(device_, handle_, nullptr); });

        stats.destroyed_swapchains = CollectTyped<RetiredSwapchain>(
            swapchains,
            completed_value_,
            max_destroy_per_type_,
            [&](VkSwapchainKHR handle_) { vkDestroySwapchainKHR(device_, handle_, nullptr); });

        stats.destroyed_command_pools = CollectTyped<RetiredCommandPool>(
            command_pools,
            completed_value_,
            max_destroy_per_type_,
            [&](VkCommandPool handle_) { vkDestroyCommandPool(device_, handle_, nullptr); });

        return stats;
    }

    [[nodiscard]] FrameRetireStats Flush(VkDevice device_) {
        if (device_ == VK_NULL_HANDLE) {
            image_views.clear();
            framebuffers.clear();
            swapchains.clear();
            command_pools.clear();
            return {};
        }

        FrameRetireStats stats{};

        for (const auto& item : image_views) {
            vkDestroyImageView(device_, item.handle, nullptr);
            ++stats.destroyed_image_views;
        }
        image_views.clear();

        for (const auto& item : framebuffers) {
            vkDestroyFramebuffer(device_, item.handle, nullptr);
            ++stats.destroyed_framebuffers;
        }
        framebuffers.clear();

        for (const auto& item : swapchains) {
            vkDestroySwapchainKHR(device_, item.handle, nullptr);
            ++stats.destroyed_swapchains;
        }
        swapchains.clear();

        for (const auto& item : command_pools) {
            vkDestroyCommandPool(device_, item.handle, nullptr);
            ++stats.destroyed_command_pools;
        }
        command_pools.clear();

        return stats;
    }

    [[nodiscard]] bool Empty() const noexcept {
        return image_views.empty() &&
               framebuffers.empty() &&
               swapchains.empty() &&
               command_pools.empty();
    }

    [[nodiscard]] uint32_t PendingImageViews() const noexcept {
        return static_cast<uint32_t>(image_views.size());
    }

    [[nodiscard]] uint32_t PendingFramebuffers() const noexcept {
        return static_cast<uint32_t>(framebuffers.size());
    }

    [[nodiscard]] uint32_t PendingSwapchains() const noexcept {
        return static_cast<uint32_t>(swapchains.size());
    }

    [[nodiscard]] uint32_t PendingCommandPools() const noexcept {
        return static_cast<uint32_t>(command_pools.size());
    }

private:
    template<typename HandleT>
    struct RetiredHandle {
        uint64_t retire_value = 0U;
        HandleT handle = VK_NULL_HANDLE;
    };

    using RetiredImageView = RetiredHandle<VkImageView>;
    using RetiredFramebuffer = RetiredHandle<VkFramebuffer>;
    using RetiredSwapchain = RetiredHandle<VkSwapchainKHR>;
    using RetiredCommandPool = RetiredHandle<VkCommandPool>;

    template<typename NodeT, typename DestroyFn>
    static uint32_t CollectTyped(McVector<NodeT>& items_,
                                 uint64_t completed_value_,
                                 uint32_t max_destroy_,
                                 DestroyFn destroy_fn_) {
        if (items_.empty()) {
            return 0U;
        }

        uint32_t destroyed = 0U;
        uint32_t write_index = 0U;
        for (uint32_t read_index = 0U; read_index < items_.size(); ++read_index) {
            NodeT item = items_[read_index];
            const bool can_destroy = item.retire_value <= completed_value_ && destroyed < max_destroy_;
            if (can_destroy) {
                destroy_fn_(item.handle);
                ++destroyed;
            } else {
                if (write_index != read_index) {
                    items_[write_index] = item;
                }
                ++write_index;
            }
        }
        items_.resize(write_index);
        return destroyed;
    }

private:
    McVector<RetiredImageView> image_views{};
    McVector<RetiredFramebuffer> framebuffers{};
    McVector<RetiredSwapchain> swapchains{};
    McVector<RetiredCommandPool> command_pools{};
};

// --- swapchain_host.hpp -----------------------------------------------------

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
    bool wait_queue_idle_before_destroy = false;
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
                    uint64_t retire_value_);

    void Shutdown(VulkanContext& context_) {
        Shutdown(context_, nullptr, 0U);
    }

    void Shutdown(VulkanContext& context_,
                  FrameRetireHost* retire_host_,
                  uint64_t retire_value_);

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
                                   uint64_t retire_value_);

    [[nodiscard]] AcquireResult AcquireNextImage(VulkanContext& context_,
                                                 VkSemaphore image_available_semaphore_,
                                                 VkFence fence_ = VK_NULL_HANDLE,
                                                 uint64_t timeout_ns_ = std::numeric_limits<uint64_t>::max());

    [[nodiscard]] PresentResult Present(VulkanContext& context_,
                                        uint32_t image_index_,
                                        VkSemaphore render_finished_semaphore_ = VK_NULL_HANDLE);

    void MarkDirty() noexcept { dirty = true; }

    [[nodiscard]] bool IsValid() const noexcept { return swapchain != VK_NULL_HANDLE && !images.empty(); }
    [[nodiscard]] bool NeedsRecreate() const noexcept { return dirty || suboptimal_seen; }
    [[nodiscard]] VkSwapchainKHR Handle() const noexcept { return swapchain; }
    [[nodiscard]] VkFormat Format() const noexcept { return format; }
    [[nodiscard]] VkColorSpaceKHR ColorSpace() const noexcept { return color_space; }
    [[nodiscard]] VkExtent2D Extent() const noexcept { return extent; }
    [[nodiscard]] VkPresentModeKHR PresentMode() const noexcept { return present_mode; }
    [[nodiscard]] uint32_t ImageCount() const noexcept { return static_cast<uint32_t>(images.size()); }
    [[nodiscard]] uint64_t Generation() const noexcept { return generation; }
    [[nodiscard]] VkImage Image(uint32_t image_index_) const noexcept { return images[image_index_].image; }
    [[nodiscard]] VkImageView ImageView(uint32_t image_index_) const noexcept { return images[image_index_].view; }
    [[nodiscard]] VkFramebuffer Framebuffer(uint32_t image_index_) const noexcept { return images[image_index_].framebuffer; }

private:
    struct SwapchainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities{};
        McVector<VkSurfaceFormatKHR> formats{};
        McVector<VkPresentModeKHR> present_modes{};
    };

    [[nodiscard]] static const char* VkResultName(VkResult result_) noexcept;
    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);

    [[nodiscard]] static SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice physical_device_,
                                                                       VkSurfaceKHR surface_);
    [[nodiscard]] static VkSurfaceFormatKHR ChooseSurfaceFormat(const McVector<VkSurfaceFormatKHR>& available_formats_,
                                                                const SwapchainCreateInfo& create_info_);
    [[nodiscard]] static VkPresentModeKHR ChoosePresentMode(const McVector<VkPresentModeKHR>& available_modes_,
                                                            const SwapchainCreateInfo& create_info_);
    [[nodiscard]] static VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities_,
                                                 const WindowSurfaceT& window_surface_);
    [[nodiscard]] static uint32_t ChooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities_,
                                                   const SwapchainCreateInfo& create_info_);
    [[nodiscard]] static VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(const VkSurfaceCapabilitiesKHR& capabilities_,
                                                                           VkCompositeAlphaFlagBitsKHR preferred_);

    static void DestroyImageResources(VkDevice device_,
                                      McVector<SwapchainImage>& images_);

    static void RetireOrDestroyResources(VulkanContext& context_,
                                         McVector<SwapchainImage>& images_,
                                         VkSwapchainKHR swapchain_,
                                         const SwapchainCreateInfo& create_info_,
                                         FrameRetireHost* retire_host_,
                                         uint64_t retire_value_);

    void Recreate(VulkanContext& context_,
                  const WindowSurfaceT& window_surface_,
                  const SwapchainCreateInfo& create_info_,
                  FrameRetireHost* retire_host_,
                  uint64_t retire_value_);

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

// --- frame_sync_host.hpp ----------------------------------------------------

template<typename SwapchainHostT>
concept SwapchainBridge = requires(SwapchainHostT& swapchain_,
                                   VulkanContext& context_,
                                   VkSemaphore semaphore_,
                                   VkFence fence_,
                                   uint32_t image_index_) {
    { swapchain_.ImageCount() } -> std::convertible_to<uint32_t>;
    { swapchain_.AcquireNextImage(context_, semaphore_, fence_) };
    { swapchain_.Present(context_, image_index_, semaphore_) };
};

enum class FrameBeginCode : std::uint8_t {
    Ready = 0,
    RecreateSwapchain = 1
};

struct FrameSlot {
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkSemaphore render_finished = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;
};

struct FrameSubmitWait {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkPipelineStageFlags stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
};

struct FrameToken {
    uint32_t frame_index = 0U;
    uint32_t image_index = 0U;
    VkSemaphore wait_semaphore = VK_NULL_HANDLE;
    VkSemaphore signal_semaphore = VK_NULL_HANDLE;
    VkFence submit_fence = VK_NULL_HANDLE;
    uint64_t submit_value = 0U;
    bool recommend_recreate = false;
};

struct FrameBeginResult {
    FrameBeginCode code = FrameBeginCode::Ready;
    FrameToken token{};
};

template<uint32_t frames_in_flight_v = 2U>
class FrameSyncHost final {
    static_assert(frames_in_flight_v > 0U, "frames_in_flight_v must be >= 1");

public:
    FrameSyncHost() = default;
    ~FrameSyncHost() = default;

    FrameSyncHost(const FrameSyncHost&) = delete;
    FrameSyncHost& operator=(const FrameSyncHost&) = delete;

    FrameSyncHost(FrameSyncHost&&) = delete;
    FrameSyncHost& operator=(FrameSyncHost&&) = delete;

    void Initialize(VulkanContext& context_, uint32_t swapchain_image_count_);
    void Shutdown(VulkanContext& context_);

    void OnSwapchainRecreated(uint32_t swapchain_image_count_) {
        image_owner_fence.resize(swapchain_image_count_);
        for (auto& owner_fence : image_owner_fence) {
            owner_fence = VK_NULL_HANDLE;
        }
    }

    void PrepareCurrentFrame(VulkanContext& context_);

    template<SwapchainBridge SwapchainHostT>
    [[nodiscard]] FrameBeginResult BeginFrame(VulkanContext& context_, SwapchainHostT& swapchain_);

    VkResult Submit(VulkanContext& context_,
                    const FrameToken& token_,
                    VkCommandBuffer command_buffer_,
                    VkPipelineStageFlags wait_stage_mask_ = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) {
        return Submit(context_,
                      token_,
                      command_buffer_,
                      wait_stage_mask_,
                      nullptr,
                      0U);
    }

    VkResult Submit(VulkanContext& context_,
                    const FrameToken& token_,
                    VkCommandBuffer command_buffer_,
                    VkPipelineStageFlags wait_stage_mask_,
                    const FrameSubmitWait* extra_waits_,
                    uint32_t extra_wait_count_);

    template<SwapchainBridge SwapchainHostT>
    [[nodiscard]] bool Present(VulkanContext& context_,
                               SwapchainHostT& swapchain_,
                               const FrameToken& token_) {
        auto present_result = swapchain_.Present(context_, token_.image_index, token_.signal_semaphore);
        return present_result.need_recreate || token_.recommend_recreate;
    }

    void AdvanceFrame() noexcept {
        current_frame = (current_frame + 1U) % frames_in_flight_v;
    }

    [[nodiscard]] bool IsInitialized() const noexcept { return initialized; }
    [[nodiscard]] uint32_t FramesInFlight() const noexcept { return frames_in_flight_v; }
    [[nodiscard]] uint32_t CurrentFrameIndex() const noexcept { return current_frame; }
    [[nodiscard]] uint64_t LastSubmittedValue() const noexcept { return last_submitted_value; }
    [[nodiscard]] uint64_t CompletedSubmitValue() const noexcept { return completed_submit_value; }

private:
    [[nodiscard]] static const char* VkResultName(VkResult result_) noexcept;
    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);

    void WaitCurrentFrameFenceIfNeeded(VulkanContext& context_);

private:
    std::array<FrameSlot, frames_in_flight_v> slots{};
    McVector<VkFence> image_owner_fence{};
    std::array<uint64_t, frames_in_flight_v> slot_submit_value{};
    std::array<bool, frames_in_flight_v> frame_reuse_waited{};
    uint64_t next_submit_value = 1U;
    uint64_t last_submitted_value = 0U;
    uint64_t completed_submit_value = 0U;
    uint32_t current_frame = 0U;
    bool initialized = false;
};

// --- frame_command_host.hpp -------------------------------------------------

struct FrameCommandCreateInfo {
    uint32_t frames_in_flight = 2U;
    uint32_t initial_primary_per_frame = 2U;
    uint32_t primary_growth_chunk = 2U;
    VkCommandPoolCreateFlags command_pool_flags =
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
};

struct FrameCommandSlot {
    VkCommandPool pool = VK_NULL_HANDLE;
    McVector<VkCommandBuffer> primary_buffers{};
    uint32_t used_primary_count = 0U;
};

class FrameCommandHost final {
public:
    FrameCommandHost() = default;
    ~FrameCommandHost() = default;

    FrameCommandHost(const FrameCommandHost&) = delete;
    FrameCommandHost& operator=(const FrameCommandHost&) = delete;

    FrameCommandHost(FrameCommandHost&&) = delete;
    FrameCommandHost& operator=(FrameCommandHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const FrameCommandCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void ResetFrame(VulkanContext& context_, uint32_t frame_index_);

    [[nodiscard]] VkCommandBuffer AcquirePrimary(VulkanContext& context_, uint32_t frame_index_);

    [[nodiscard]] VkCommandBuffer BeginPrimary(VulkanContext& context_,
                                               uint32_t frame_index_,
                                               VkCommandBufferUsageFlags usage_flags_ = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    void EndCommandBuffer(VkCommandBuffer command_buffer_);

    void ReservePrimary(VulkanContext& context_, uint32_t frame_index_, uint32_t primary_count_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] uint32_t UsedPrimaryCount(uint32_t frame_index_) const;
    [[nodiscard]] VkCommandPool CommandPool(uint32_t frame_index_) const;

private:
    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);

    FrameCommandSlot& SlotAt(uint32_t frame_index_);
    const FrameCommandSlot& SlotAt(uint32_t frame_index_) const;

    void AllocatePrimaryBuffers(VulkanContext& context_,
                                FrameCommandSlot& slot_,
                                uint32_t alloc_count_);

private:
    McVector<FrameCommandSlot> slots{};
    FrameCommandCreateInfo create_info_cache{};
    bool initialized = false;
};

// --- render_loop_host.hpp ---------------------------------------------------

struct FrameRecordContext {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    uint32_t frame_index = 0U;
    uint32_t image_index = 0U;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
};

template<typename RecorderT>
concept FrameContextRecorder = requires(RecorderT& recorder_,
                                        const FrameRecordContext& context_) {
    { recorder_.Record(context_) };
};

template<typename RecorderT>
concept FrameRecorder = requires(RecorderT& recorder_,
                                 VkCommandBuffer command_buffer_,
                                 uint32_t frame_index_,
                                 uint32_t image_index_,
                                 VkExtent2D extent_,
                                 VkFormat format_,
                                 VkImage image_,
                                 VkImageView image_view_) {
    { recorder_.Record(command_buffer_,
                       frame_index_,
                       image_index_,
                       extent_,
                       format_,
                       image_,
                       image_view_) };
};

template<typename SwapchainHostT, typename WindowSurfaceT>
concept SwapchainLoopBridge = requires(SwapchainHostT& swapchain_,
                                       VulkanContext& context_,
                                       const WindowSurfaceT& window_surface_,
                                       const SwapchainCreateInfo& swapchain_create_info_,
                                       uint32_t image_index_,
                                       FrameRetireHost* retire_host_,
                                       uint64_t retire_value_) {
    { swapchain_.IsValid() } -> std::convertible_to<bool>;
    { swapchain_.Initialize(context_, window_surface_, swapchain_create_info_) };
    { swapchain_.Initialize(context_, window_surface_, swapchain_create_info_, retire_host_, retire_value_) };
    { swapchain_.Shutdown(context_) };
    { swapchain_.Shutdown(context_, retire_host_, retire_value_) };
    { swapchain_.EnsureValid(context_, window_surface_, swapchain_create_info_) } -> std::convertible_to<bool>;
    { swapchain_.EnsureValid(context_, window_surface_, swapchain_create_info_, retire_host_, retire_value_) } -> std::convertible_to<bool>;
    { swapchain_.MarkDirty() };
    { swapchain_.ImageCount() } -> std::convertible_to<uint32_t>;
    { swapchain_.Generation() } -> std::convertible_to<uint64_t>;
    { swapchain_.Extent() } -> std::same_as<VkExtent2D>;
    { swapchain_.Format() } -> std::same_as<VkFormat>;
    { swapchain_.Image(image_index_) } -> std::same_as<VkImage>;
    { swapchain_.ImageView(image_index_) } -> std::same_as<VkImageView>;
};

enum class TickCode : std::uint8_t {
    Submitted = 0,
    SkippedWindowHidden = 1,
    RecreateRequested = 2
};

struct TickResult {
    TickCode code = TickCode::Submitted;
    uint32_t frame_index = 0U;
    uint32_t image_index = 0U;
};

struct RenderLoopCreateInfo {
    SwapchainCreateInfo swapchain{};
    FrameCommandCreateInfo commands{};
    VkCommandBufferUsageFlags command_usage_flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkPipelineStageFlags submit_wait_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    uint32_t retire_collect_budget_per_type = 128U;
};

template<typename WindowSurfaceT,
         typename SwapchainHostT,
         uint32_t frames_in_flight_v = 2U>
requires SwapchainLoopBridge<SwapchainHostT, WindowSurfaceT>
class RenderLoopHost final {
public:
    RenderLoopHost() = default;
    ~RenderLoopHost() = default;

    RenderLoopHost(const RenderLoopHost&) = delete;
    RenderLoopHost& operator=(const RenderLoopHost&) = delete;

    RenderLoopHost(RenderLoopHost&&) = delete;
    RenderLoopHost& operator=(RenderLoopHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const WindowSurfaceT& window_surface_,
                    SwapchainHostT& swapchain_,
                    const RenderLoopCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_, SwapchainHostT& swapchain_);

    template<typename RecorderT>
    requires (FrameRecorder<RecorderT> || FrameContextRecorder<RecorderT>)
    [[nodiscard]] TickResult Tick(VulkanContext& context_,
                                  const WindowSurfaceT& window_surface_,
                                  SwapchainHostT& swapchain_,
                                  RecorderT& recorder_) {
        return Tick(context_,
                    window_surface_,
                    swapchain_,
                    recorder_,
                    nullptr,
                    0U);
    }

    template<typename RecorderT>
    requires (FrameRecorder<RecorderT> || FrameContextRecorder<RecorderT>)
    [[nodiscard]] TickResult Tick(VulkanContext& context_,
                                  const WindowSurfaceT& window_surface_,
                                  SwapchainHostT& swapchain_,
                                  RecorderT& recorder_,
                                  const FrameSubmitWait* extra_submit_waits_,
                                  uint32_t extra_submit_wait_count_);

    [[nodiscard]] bool IsInitialized() const noexcept { return initialized; }
    [[nodiscard]] const FrameSyncHost<frames_in_flight_v>& Sync() const noexcept { return frame_sync; }
    [[nodiscard]] FrameSyncHost<frames_in_flight_v>& Sync() noexcept { return frame_sync; }
    [[nodiscard]] const FrameCommandHost& Commands() const noexcept { return frame_commands; }
    [[nodiscard]] FrameCommandHost& Commands() noexcept { return frame_commands; }
    [[nodiscard]] const FrameRetireHost& Retire() const noexcept { return frame_retire; }
    [[nodiscard]] FrameRetireHost& Retire() noexcept { return frame_retire; }
    [[nodiscard]] const RenderLoopCreateInfo& Config() const noexcept { return create_info_cache; }

private:
    template<typename RecorderT>
    static void RecordFrame(RecorderT& recorder_,
                            VkCommandBuffer command_buffer_,
                            uint32_t frame_index_,
                            uint32_t image_index_,
                            VkExtent2D extent_,
                            VkFormat format_,
                            VkImage image_,
                            VkImageView image_view_) {
        if constexpr (FrameContextRecorder<RecorderT>) {
            FrameRecordContext context{};
            context.command_buffer = command_buffer_;
            context.frame_index = frame_index_;
            context.image_index = image_index_;
            context.extent = extent_;
            context.format = format_;
            context.image = image_;
            context.image_view = image_view_;
            recorder_.Record(context);
        } else {
            recorder_.Record(command_buffer_,
                             frame_index_,
                             image_index_,
                             extent_,
                             format_,
                             image_,
                             image_view_);
        }
    }

    template<typename RecorderT>
    static void NotifySwapchainRecreatedIfSupported(RecorderT& recorder_,
                                                    uint32_t image_count_,
                                                    VkExtent2D extent_,
                                                    VkFormat format_,
                                                    uint64_t last_submitted_value_,
                                                    uint64_t completed_submit_value_);

    FrameSyncHost<frames_in_flight_v> frame_sync{};
    FrameCommandHost frame_commands{};
    FrameRetireHost frame_retire{};
    RenderLoopCreateInfo create_info_cache{};
    uint64_t last_known_swapchain_generation = 0U;
    uint32_t last_known_swapchain_image_count = 0U;
    VkExtent2D last_known_swapchain_extent{};
    VkFormat last_known_swapchain_format = VK_FORMAT_UNDEFINED;
    bool recorder_swapchain_notified = false;
    bool initialized = false;
};

// --- descriptor_host.hpp ----------------------------------------------------

struct DescriptorPoolSizeRatio {
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    float ratio = 1.0F;
};

struct DescriptorHostCreateInfo {
    uint32_t frames_in_flight = 2U;
    uint32_t max_sets_per_pool = 512U;
    VkDescriptorPoolCreateFlags pool_flags = 0U;
    McVector<DescriptorPoolSizeRatio> pool_size_ratios{};
    bool preallocate_first_pool_per_frame = true;
    uint32_t reserve_layout_count = 128U;
};

struct DescriptorSetLayoutDesc {
    McVector<VkDescriptorSetLayoutBinding> bindings{};
    McVector<VkDescriptorBindingFlags> binding_flags{};
    VkDescriptorSetLayoutCreateFlags flags = 0U;
};

struct DescriptorBufferWrite {
    uint32_t binding = 0U;
    uint32_t array_element = 0U;
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0U;
    VkDeviceSize range = VK_WHOLE_SIZE;
};

struct DescriptorImageWrite {
    uint32_t binding = 0U;
    uint32_t array_element = 0U;
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

struct DescriptorTexelBufferWrite {
    uint32_t binding = 0U;
    uint32_t array_element = 0U;
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    VkBufferView buffer_view = VK_NULL_HANDLE;
};

struct DescriptorSetLayoutId {
    uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

class DescriptorHost final {
public:
    DescriptorHost() = default;
    ~DescriptorHost() = default;

    DescriptorHost(const DescriptorHost&) = delete;
    DescriptorHost& operator=(const DescriptorHost&) = delete;

    DescriptorHost(DescriptorHost&&) = delete;
    DescriptorHost& operator=(DescriptorHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const DescriptorHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_, uint32_t frame_index_);

    [[nodiscard]] DescriptorSetLayoutId RegisterLayout(VulkanContext& context_,
                                                       const DescriptorSetLayoutDesc& layout_desc_);

    [[nodiscard]] VkDescriptorSetLayout AcquireLayout(VulkanContext& context_,
                                                      const DescriptorSetLayoutDesc& layout_desc_);

    [[nodiscard]] VkDescriptorSetLayout GetLayout(DescriptorSetLayoutId layout_id_) const;

    [[nodiscard]] VkDescriptorSet AllocateSet(VulkanContext& context_,
                                              uint32_t frame_index_,
                                              VkDescriptorSetLayout layout_);

    [[nodiscard]] VkDescriptorSet AllocateSet(VulkanContext& context_,
                                              uint32_t frame_index_,
                                              DescriptorSetLayoutId layout_id_);

    void AllocateSets(VulkanContext& context_,
                      uint32_t frame_index_,
                      VkDescriptorSetLayout layout_,
                      uint32_t set_count_,
                      VkDescriptorSet* out_sets_);

    void AllocateSets(VulkanContext& context_,
                      uint32_t frame_index_,
                      DescriptorSetLayoutId layout_id_,
                      uint32_t set_count_,
                      VkDescriptorSet* out_sets_);

    void UpdateSet(VkDevice device_,
                   VkDescriptorSet set_,
                   const McVector<DescriptorBufferWrite>& buffer_writes_,
                   const McVector<DescriptorImageWrite>& image_writes_,
                   const McVector<DescriptorTexelBufferWrite>& texel_writes_ = {});

    void UpdateSet(VulkanContext& context_,
                   VkDescriptorSet set_,
                   const McVector<DescriptorBufferWrite>& buffer_writes_,
                   const McVector<DescriptorImageWrite>& image_writes_,
                   const McVector<DescriptorTexelBufferWrite>& texel_writes_ = {});

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] uint32_t MaxSetsPerPool() const noexcept;
    [[nodiscard]] uint32_t CachedLayoutCount() const noexcept;
    [[nodiscard]] uint32_t TotalPoolCount() const noexcept;
    [[nodiscard]] uint32_t FramePoolCount(uint32_t frame_index_) const;

private:
    struct LayoutBindingKey {
        uint32_t binding = 0U;
        VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        uint32_t descriptor_count = 0U;
        VkShaderStageFlags stage_flags = 0U;
        uint32_t immutable_sampler_offset = 0U;
    };

    struct LayoutKey {
        VkDescriptorSetLayoutCreateFlags flags = 0U;
        McVector<LayoutBindingKey> bindings{};
        McVector<VkDescriptorBindingFlags> binding_flags{};
        McVector<VkSampler> immutable_samplers{};
    };

    struct LayoutCacheEntry {
        uint64_t hash = 0U;
        LayoutKey key{};
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    };

    struct HashLookupNode {
        uint64_t hash = 0U;
        uint32_t entry_index = 0U;
    };

    struct DescriptorPoolPage {
        VkDescriptorPool pool = VK_NULL_HANDLE;
        uint32_t allocated_sets = 0U;
    };

    struct FramePoolArena {
        McVector<DescriptorPoolPage> pools{};
        uint32_t active_pool_index = 0U;
    };

    struct UpdateScratch {
        McVector<VkWriteDescriptorSet> writes{};
        McVector<VkDescriptorBufferInfo> buffer_infos{};
        McVector<VkDescriptorImageInfo> image_infos{};
        McVector<VkBufferView> texel_views{};
    };

    static constexpr uint32_t kInvalidSamplerOffset = 0xFFFFFFFFU;

    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);
    static void HashCombine(uint64_t& hash_, uint64_t value_) noexcept;
    static uint64_t HashLayoutKey(const LayoutKey& key_) noexcept;
    static bool LayoutKeyEquals(const LayoutKey& lhs_, const LayoutKey& rhs_) noexcept;
    static LayoutKey CanonicalizeLayoutDesc(const DescriptorSetLayoutDesc& layout_desc_);
    static McVector<DescriptorPoolSizeRatio> DefaultPoolRatios();
    [[nodiscard]] static uint32_t IdToIndex(uint32_t id_value_);
    [[nodiscard]] static DescriptorSetLayoutId MakeLayoutId(uint32_t entry_index_);

    FramePoolArena& ArenaAt(uint32_t frame_index_);
    const FramePoolArena& ArenaAt(uint32_t frame_index_) const;

    VkDescriptorPool CreatePool(VulkanContext& context_) const;
    VkDescriptorPool AcquirePoolForFrame(VulkanContext& context_,
                                         FramePoolArena& arena_);

private:
    DescriptorHostCreateInfo create_info_cache{};
    McVector<VkDescriptorPoolSize> pool_sizes_cache{};
    McVector<FramePoolArena> frame_arenas{};
    McVector<LayoutCacheEntry> layout_cache{};
    McVector<HashLookupNode> layout_lookup{};
    UpdateScratch update_scratch{};
    bool initialized = false;
};

// --- upload_host.hpp --------------------------------------------------------

struct UploadHostCreateInfo {
    uint32_t frames_in_flight = 2U;
    VkDeviceSize staging_buffer_size = 64U * 1024U * 1024U;
    VkBufferUsageFlags staging_buffer_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlags staging_memory_properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkCommandPoolCreateFlags command_pool_flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    bool prefer_transfer_queue = true;
    bool fallback_to_graphics_queue = true;
};

struct UploadSubmitInfo {
    VkSemaphore wait_semaphore = VK_NULL_HANDLE;
    VkPipelineStageFlags wait_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSemaphore signal_semaphore = VK_NULL_HANDLE;
};

struct UploadEndFrameResult {
    VkResult result = VK_SUCCESS;
    bool submitted = false;
};

struct UploadAllocation {
    void* mapped_data = nullptr;
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceSize staging_offset = 0U;
    VkDeviceSize size = 0U;
};

struct UploadFrameStats {
    VkDeviceSize used_bytes = 0U;
    VkDeviceSize capacity_bytes = 0U;
    uint32_t buffer_copy_count = 0U;
    uint32_t image_copy_count = 0U;
    uint32_t barrier_count = 0U;
};

class UploadHost final {
public:
    UploadHost() = default;
    ~UploadHost() = default;

    UploadHost(const UploadHost&) = delete;
    UploadHost& operator=(const UploadHost&) = delete;

    UploadHost(UploadHost&&) = delete;
    UploadHost& operator=(UploadHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const UploadHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_, uint32_t frame_index_);

    [[nodiscard]] UploadAllocation Allocate(uint32_t frame_index_,
                                            VkDeviceSize size_,
                                            VkDeviceSize alignment_ = 16U);

    [[nodiscard]] UploadAllocation Write(uint32_t frame_index_,
                                         const void* src_data_,
                                         VkDeviceSize size_,
                                         VkDeviceSize alignment_ = 16U);

    void RecordCopyBuffer(uint32_t frame_index_,
                          VkBuffer dst_buffer_,
                          VkDeviceSize dst_offset_,
                          const UploadAllocation& allocation_);

    void StageAndRecordCopyBuffer(uint32_t frame_index_,
                                  VkBuffer dst_buffer_,
                                  VkDeviceSize dst_offset_,
                                  const void* src_data_,
                                  VkDeviceSize size_,
                                  VkDeviceSize alignment_ = 16U);

    void RecordCopyImage(uint32_t frame_index_,
                         VkImage dst_image_,
                         VkImageLayout dst_layout_,
                         const VkBufferImageCopy& copy_region_,
                         const UploadAllocation& allocation_);

    void StageAndRecordCopyImage(uint32_t frame_index_,
                                 VkImage dst_image_,
                                 VkImageLayout dst_layout_,
                                 const VkBufferImageCopy& copy_region_,
                                 const void* src_data_,
                                 VkDeviceSize size_,
                                 VkDeviceSize alignment_ = 16U);

    void RecordMemoryBarrier2(uint32_t frame_index_, const VkMemoryBarrier2& barrier_);
    void RecordBufferBarrier2(uint32_t frame_index_, const VkBufferMemoryBarrier2& barrier_);
    void RecordImageBarrier2(uint32_t frame_index_, const VkImageMemoryBarrier2& barrier_);

    [[nodiscard]] UploadEndFrameResult EndFrameAndSubmit(VulkanContext& context_,
                                                         uint32_t frame_index_,
                                                         const UploadSubmitInfo& submit_info_ = {});

    void WaitFrame(VulkanContext& context_,
                   uint32_t frame_index_,
                   uint64_t timeout_ns_ = std::numeric_limits<uint64_t>::max());

    void WaitIdle(VulkanContext& context_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] uint32_t QueueFamilyIndex() const noexcept;
    [[nodiscard]] VkQueue SubmitQueue() const noexcept;
    [[nodiscard]] const UploadFrameStats& FrameStats(uint32_t frame_index_) const;
    [[nodiscard]] VkDeviceSize CapacityBytes() const noexcept;

private:
    struct UploadFrameSlot {
        VkBuffer staging_buffer = VK_NULL_HANDLE;
        Center::Memory::Vulkan::Slice allocation_slice{};
        void* mapped_ptr = nullptr;
        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VkFence in_flight_fence = VK_NULL_HANDLE;
        VkDeviceSize write_head = 0U;
        bool recording_active = false;
        bool recorded_work = false;
        UploadFrameStats stats{};
    };

    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);
    static VkDeviceSize AlignUp(VkDeviceSize value_, VkDeviceSize alignment_);

    UploadFrameSlot& SlotAt(uint32_t frame_index_);
    const UploadFrameSlot& SlotAt(uint32_t frame_index_) const;

    void CreateSlotResources(VulkanContext& context_, UploadFrameSlot& slot_);
    void DestroySlotResources(VulkanContext& context_, UploadFrameSlot& slot_);
    void FlushAllocationIfNeeded(VulkanContext& context_,
                                 const UploadFrameSlot& slot_,
                                 VkDeviceSize offset_,
                                 VkDeviceSize size_) const;

private:
    McVector<UploadFrameSlot> slots{};
    UploadHostCreateInfo create_info_cache{};
    resource::GpuMemoryHost* memory_host = nullptr;
    VkQueue submit_queue = VK_NULL_HANDLE;
    uint32_t queue_family_index = 0U;
    bool synchronization2_enabled = false;
    bool initialized = false;
};

// --- pipeline_host.hpp ------------------------------------------------------

struct PipelineHostCreateInfo {
    bool enable_pipeline_cache = true;
    const void* initial_pipeline_cache_data = nullptr;
    std::size_t initial_pipeline_cache_size = 0U;
    uint32_t reserve_shader_module_count = 64U;
    uint32_t reserve_pipeline_layout_count = 64U;
    uint32_t reserve_graphics_pipeline_count = 128U;
    uint32_t reserve_compute_pipeline_count = 32U;
    bool fail_on_pipeline_compile_required = false;
    bool early_return_on_pipeline_failure = false;
};

struct ShaderModuleCreateInfo {
    VkShaderModuleCreateFlags flags = 0U;
    const uint32_t* code_words = nullptr;
    std::size_t word_count = 0U;
};

struct PushConstantRangeDesc {
    VkShaderStageFlags stage_flags = 0U;
    uint32_t offset = 0U;
    uint32_t size = 0U;
};

struct PipelineLayoutDesc {
    VkPipelineLayoutCreateFlags flags = 0U;
    McVector<VkDescriptorSetLayout> set_layouts{};
    McVector<PushConstantRangeDesc> push_constant_ranges{};
};

struct PipelineShaderSpecializationDesc {
    McVector<VkSpecializationMapEntry> map_entries{};
    McVector<uint8_t> data{};
};

struct PipelineShaderStageDesc {
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
    VkShaderModule module = VK_NULL_HANDLE;
    const char* entry_name = "main";
    VkPipelineShaderStageCreateFlags flags = 0U;
    PipelineShaderSpecializationDesc specialization{};
};

struct GraphicsVertexInputStateDesc {
    McVector<VkVertexInputBindingDescription> bindings{};
    McVector<VkVertexInputAttributeDescription> attributes{};
};

struct GraphicsInputAssemblyStateDesc {
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool primitive_restart_enable = false;
};

struct GraphicsTessellationStateDesc {
    uint32_t patch_control_points = 0U;
};

struct GraphicsViewportStateDesc {
    uint32_t viewport_count = 1U;
    uint32_t scissor_count = 1U;
    McVector<VkViewport> static_viewports{};
    McVector<VkRect2D> static_scissors{};
};

struct GraphicsRasterizationStateDesc {
    bool depth_clamp_enable = false;
    bool rasterizer_discard_enable = false;
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool depth_bias_enable = false;
    float depth_bias_constant_factor = 0.0F;
    float depth_bias_clamp = 0.0F;
    float depth_bias_slope_factor = 0.0F;
    float line_width = 1.0F;
};

struct GraphicsMultisampleStateDesc {
    VkSampleCountFlagBits rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    bool sample_shading_enable = false;
    float min_sample_shading = 0.0F;
    McVector<VkSampleMask> sample_masks{};
    bool alpha_to_coverage_enable = false;
    bool alpha_to_one_enable = false;
};

struct GraphicsDepthStencilStateDesc {
    bool depth_test_enable = false;
    bool depth_write_enable = false;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
    bool depth_bounds_test_enable = false;
    bool stencil_test_enable = false;
    VkStencilOpState front{};
    VkStencilOpState back{};
    float min_depth_bounds = 0.0F;
    float max_depth_bounds = 1.0F;
};

struct GraphicsColorBlendStateDesc {
    bool logic_op_enable = false;
    VkLogicOp logic_op = VK_LOGIC_OP_COPY;
    McVector<VkPipelineColorBlendAttachmentState> attachments{};
    float blend_constants[4] = {0.0F, 0.0F, 0.0F, 0.0F};
};

struct GraphicsDynamicStateDesc {
    McVector<VkDynamicState> states{};
};

struct GraphicsRenderingInfoDesc {
    uint32_t view_mask = 0U;
    McVector<VkFormat> color_attachment_formats{};
    VkFormat depth_attachment_format = VK_FORMAT_UNDEFINED;
    VkFormat stencil_attachment_format = VK_FORMAT_UNDEFINED;
};

struct GraphicsPipelineDesc {
    VkPipelineCreateFlags flags = 0U;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    McVector<PipelineShaderStageDesc> shader_stages{};

    GraphicsVertexInputStateDesc vertex_input{};
    GraphicsInputAssemblyStateDesc input_assembly{};
    GraphicsTessellationStateDesc tessellation{};
    GraphicsViewportStateDesc viewport{};
    GraphicsRasterizationStateDesc rasterization{};
    GraphicsMultisampleStateDesc multisample{};
    GraphicsDepthStencilStateDesc depth_stencil{};
    GraphicsColorBlendStateDesc color_blend{};
    GraphicsDynamicStateDesc dynamic{};

    bool use_dynamic_rendering = true;
    GraphicsRenderingInfoDesc rendering{};
    VkRenderPass render_pass = VK_NULL_HANDLE;
    uint32_t subpass = 0U;

    VkPipeline base_pipeline_handle = VK_NULL_HANDLE;
    int32_t base_pipeline_index = -1;
};

struct ComputePipelineDesc {
    VkPipelineCreateFlags flags = 0U;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    PipelineShaderStageDesc shader_stage{
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = VK_NULL_HANDLE,
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    };

    VkPipeline base_pipeline_handle = VK_NULL_HANDLE;
    int32_t base_pipeline_index = -1;
};

struct PipelineHostStats {
    uint32_t shader_module_count = 0U;
    uint32_t shader_module_cache_hits = 0U;
    uint32_t shader_module_cache_misses = 0U;

    uint32_t pipeline_layout_count = 0U;
    uint32_t pipeline_layout_cache_hits = 0U;
    uint32_t pipeline_layout_cache_misses = 0U;

    uint32_t graphics_pipeline_count = 0U;
    uint32_t graphics_pipeline_cache_hits = 0U;
    uint32_t graphics_pipeline_cache_misses = 0U;

    uint32_t compute_pipeline_count = 0U;
    uint32_t compute_pipeline_cache_hits = 0U;
    uint32_t compute_pipeline_cache_misses = 0U;
};

struct ShaderModuleId {
    uint32_t value = 0U;
    [[nodiscard]] bool IsValid() const noexcept { return value != 0U; }
};

struct PipelineLayoutId {
    uint32_t value = 0U;
    [[nodiscard]] bool IsValid() const noexcept { return value != 0U; }
};

struct GraphicsPipelineId {
    uint32_t value = 0U;
    [[nodiscard]] bool IsValid() const noexcept { return value != 0U; }
};

struct ComputePipelineId {
    uint32_t value = 0U;
    [[nodiscard]] bool IsValid() const noexcept { return value != 0U; }
};

class PipelineHost final {
public:
    PipelineHost() = default;
    ~PipelineHost() = default;

    PipelineHost(const PipelineHost&) = delete;
    PipelineHost& operator=(const PipelineHost&) = delete;

    PipelineHost(PipelineHost&&) = delete;
    PipelineHost& operator=(PipelineHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const PipelineHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    [[nodiscard]] ShaderModuleId RegisterShaderModule(VulkanContext& context_,
                                                      const ShaderModuleCreateInfo& create_info_);
    [[nodiscard]] PipelineLayoutId RegisterPipelineLayout(VulkanContext& context_,
                                                          const PipelineLayoutDesc& layout_desc_);
    [[nodiscard]] GraphicsPipelineId RegisterGraphicsPipeline(VulkanContext& context_,
                                                              const GraphicsPipelineDesc& pipeline_desc_);
    [[nodiscard]] ComputePipelineId RegisterComputePipeline(VulkanContext& context_,
                                                            const ComputePipelineDesc& pipeline_desc_);

    void RegisterGraphicsPipelines(VulkanContext& context_,
                                   const GraphicsPipelineDesc* pipeline_descs_,
                                   uint32_t pipeline_count_,
                                   GraphicsPipelineId* out_pipeline_ids_);

    void RegisterComputePipelines(VulkanContext& context_,
                                  const ComputePipelineDesc* pipeline_descs_,
                                  uint32_t pipeline_count_,
                                  ComputePipelineId* out_pipeline_ids_);

    [[nodiscard]] VkShaderModule AcquireShaderModule(VulkanContext& context_,
                                                     const ShaderModuleCreateInfo& create_info_);

    [[nodiscard]] VkPipelineLayout AcquirePipelineLayout(VulkanContext& context_,
                                                         const PipelineLayoutDesc& layout_desc_);

    [[nodiscard]] VkPipeline AcquireGraphicsPipeline(VulkanContext& context_,
                                                     const GraphicsPipelineDesc& pipeline_desc_);

    [[nodiscard]] VkPipeline AcquireComputePipeline(VulkanContext& context_,
                                                    const ComputePipelineDesc& pipeline_desc_);

    [[nodiscard]] VkShaderModule GetShaderModule(ShaderModuleId shader_module_id_) const;
    [[nodiscard]] VkPipelineLayout GetPipelineLayout(PipelineLayoutId pipeline_layout_id_) const;
    [[nodiscard]] VkPipeline GetGraphicsPipeline(GraphicsPipelineId graphics_pipeline_id_) const;
    [[nodiscard]] VkPipeline GetComputePipeline(ComputePipelineId compute_pipeline_id_) const;

    void EnqueueGraphicsPipeline(const GraphicsPipelineDesc& pipeline_desc_);
    void EnqueueComputePipeline(const ComputePipelineDesc& pipeline_desc_);
    [[nodiscard]] uint32_t ProcessPendingCompiles(
        VulkanContext& context_,
        uint32_t max_graphics_count_ = std::numeric_limits<uint32_t>::max(),
        uint32_t max_compute_count_ = std::numeric_limits<uint32_t>::max());
    void ClearPendingCompiles() noexcept;
    [[nodiscard]] uint32_t PendingGraphicsCompileCount() const noexcept;
    [[nodiscard]] uint32_t PendingComputeCompileCount() const noexcept;

    [[nodiscard]] bool LoadPipelineCacheFromFile(VulkanContext& context_,
                                                 const char* file_path_);

    [[nodiscard]] bool SavePipelineCacheToFile(VulkanContext& context_,
                                               const char* file_path_) const;

    [[nodiscard]] VkPipelineCache CacheHandle() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const PipelineHostStats& Stats() const noexcept;

private:
    struct ShaderModuleEntry {
        uint64_t hash = 0U;
        VkShaderModuleCreateFlags flags = 0U;
        McVector<uint32_t> code_words{};
        VkShaderModule module = VK_NULL_HANDLE;
    };

    struct PipelineLayoutEntry {
        uint64_t hash = 0U;
        VkPipelineLayoutCreateFlags flags = 0U;
        McVector<VkDescriptorSetLayout> set_layouts{};
        McVector<VkPushConstantRange> push_constant_ranges{};
        VkPipelineLayout layout = VK_NULL_HANDLE;
    };

    struct ShaderStageEntry {
        VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
        VkShaderModule module = VK_NULL_HANDLE;
        VkPipelineShaderStageCreateFlags flags = 0U;
        std::string entry_name{"main"};
        McVector<VkSpecializationMapEntry> specialization_map_entries{};
        McVector<uint8_t> specialization_data{};
    };

    struct GraphicsPipelineEntry {
        uint64_t hash = 0U;
        VkPipelineCreateFlags flags = 0U;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        McVector<ShaderStageEntry> shader_stages{};

        GraphicsVertexInputStateDesc vertex_input{};
        GraphicsInputAssemblyStateDesc input_assembly{};
        GraphicsTessellationStateDesc tessellation{};
        GraphicsViewportStateDesc viewport{};
        GraphicsRasterizationStateDesc rasterization{};
        GraphicsMultisampleStateDesc multisample{};
        GraphicsDepthStencilStateDesc depth_stencil{};
        GraphicsColorBlendStateDesc color_blend{};
        GraphicsDynamicStateDesc dynamic{};

        bool use_dynamic_rendering = true;
        GraphicsRenderingInfoDesc rendering{};
        VkRenderPass render_pass = VK_NULL_HANDLE;
        uint32_t subpass = 0U;

        VkPipeline base_pipeline_handle = VK_NULL_HANDLE;
        int32_t base_pipeline_index = -1;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    struct ComputePipelineEntry {
        uint64_t hash = 0U;
        VkPipelineCreateFlags flags = 0U;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        ShaderStageEntry shader_stage{};
        VkPipeline base_pipeline_handle = VK_NULL_HANDLE;
        int32_t base_pipeline_index = -1;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    struct HashLookupNode {
        uint64_t hash = 0U;
        uint32_t entry_index = 0U;
    };

    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);
    static uint64_t HashBytes(const void* data_, std::size_t size_) noexcept;
    static void HashCombine(uint64_t& hash_, uint64_t value_) noexcept;

    static bool EqualSpecialization(const ShaderStageEntry& lhs_,
                                    const ShaderStageEntry& rhs_) noexcept;
    static bool EqualShaderStage(const ShaderStageEntry& lhs_,
                                 const ShaderStageEntry& rhs_) noexcept;
    static bool EqualLayoutEntry(const PipelineLayoutEntry& lhs_,
                                 const PipelineLayoutEntry& rhs_) noexcept;
    static bool EqualGraphicsEntry(const GraphicsPipelineEntry& lhs_,
                                   const GraphicsPipelineEntry& rhs_) noexcept;
    static bool EqualComputeEntry(const ComputePipelineEntry& lhs_,
                                  const ComputePipelineEntry& rhs_) noexcept;

    static uint64_t HashShaderModuleEntry(const ShaderModuleEntry& entry_) noexcept;
    static uint64_t HashLayoutEntry(const PipelineLayoutEntry& entry_) noexcept;
    static uint64_t HashShaderStageEntry(const ShaderStageEntry& entry_) noexcept;
    static uint64_t HashGraphicsEntry(const GraphicsPipelineEntry& entry_) noexcept;
    static uint64_t HashComputeEntry(const ComputePipelineEntry& entry_) noexcept;

    static ShaderStageEntry NormalizeShaderStage(const PipelineShaderStageDesc& stage_desc_);
    static PipelineLayoutEntry NormalizeLayout(const PipelineLayoutDesc& layout_desc_);
    static GraphicsPipelineEntry NormalizeGraphics(const GraphicsPipelineDesc& pipeline_desc_);
    static ComputePipelineEntry NormalizeCompute(const ComputePipelineDesc& pipeline_desc_);

    void RegisterGraphicsEntriesBatch(VulkanContext& context_,
                                      const GraphicsPipelineEntry* entries_,
                                      uint32_t entry_count_,
                                      GraphicsPipelineId* out_ids_);
    void RegisterComputeEntriesBatch(VulkanContext& context_,
                                     const ComputePipelineEntry* entries_,
                                     uint32_t entry_count_,
                                     ComputePipelineId* out_ids_);

    [[nodiscard]] GraphicsPipelineId RegisterGraphicsEntry(VulkanContext& context_,
                                                           GraphicsPipelineEntry&& entry_);
    [[nodiscard]] ComputePipelineId RegisterComputeEntry(VulkanContext& context_,
                                                         ComputePipelineEntry&& entry_);

    void CreatePipelineCache(VulkanContext& context_,
                             const void* data_,
                             std::size_t size_);
    void CompactPendingCompilesIfNeeded();

    [[nodiscard]] static uint32_t IdToIndex(uint32_t id_value_);
    [[nodiscard]] static ShaderModuleId MakeShaderModuleId(uint32_t entry_index_);
    [[nodiscard]] static PipelineLayoutId MakePipelineLayoutId(uint32_t entry_index_);
    [[nodiscard]] static GraphicsPipelineId MakeGraphicsPipelineId(uint32_t entry_index_);
    [[nodiscard]] static ComputePipelineId MakeComputePipelineId(uint32_t entry_index_);

private:
    PipelineHostCreateInfo create_info_cache{};
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;

    McVector<ShaderModuleEntry> shader_modules{};
    McVector<HashLookupNode> shader_module_lookup{};
    McVector<PipelineLayoutEntry> pipeline_layouts{};
    McVector<HashLookupNode> pipeline_layout_lookup{};
    McVector<GraphicsPipelineEntry> graphics_pipelines{};
    McVector<HashLookupNode> graphics_pipeline_lookup{};
    McVector<ComputePipelineEntry> compute_pipelines{};
    McVector<HashLookupNode> compute_pipeline_lookup{};
    McVector<GraphicsPipelineEntry> pending_graphics_pipelines{};
    McVector<ComputePipelineEntry> pending_compute_pipelines{};
    uint32_t pending_graphics_head = 0U;
    uint32_t pending_compute_head = 0U;

    PipelineHostStats stats{};
    bool initialized = false;
};

// --- runtime_prepare_context.hpp ---------------------------------------------

// Forward-declare text types (owned by vr.text module) since RuntimePrepareContext
// only stores pointers; vr.render does not import vr.text to avoid circular deps.
namespace text { class FreeTypeHost; class GlyphAtlasHost; class GlyphUploadHost; }

struct RuntimePrepareContext {
    VulkanContext* context = nullptr;
    std::uint32_t frame_index = 0U;
    std::uint64_t last_submitted_value = 0U;
    std::uint64_t completed_submit_value = 0U;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    UploadHost* upload_host = nullptr;
    DescriptorHost* descriptor_host = nullptr;
    PipelineHost* pipeline_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;
    text::FreeTypeHost* freetype_host = nullptr;
    text::GlyphAtlasHost* glyph_atlas_host = nullptr;
    text::GlyphUploadHost* glyph_upload_host = nullptr;
};

} // namespace vr::render
} // export
