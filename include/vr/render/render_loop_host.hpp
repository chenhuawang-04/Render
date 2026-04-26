#pragma once

#include "vr/render/frame_command_host.hpp"
#include "vr/render/frame_retire_host.hpp"
#include "vr/render/frame_sync_host.hpp"
#include "vr/render/swapchain_host.hpp"
#include "vr/vulkan_context.hpp"

#include <concepts>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace vr::render {

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
                    const RenderLoopCreateInfo& create_info_ = {}) {
        Shutdown(context_, swapchain_);

        create_info_cache = create_info_;
        create_info_cache.commands.frames_in_flight = frames_in_flight_v;
        if (create_info_cache.commands.initial_primary_per_frame == 0U) {
            create_info_cache.commands.initial_primary_per_frame = 1U;
        }
        if (create_info_cache.commands.primary_growth_chunk == 0U) {
            create_info_cache.commands.primary_growth_chunk = 1U;
        }

        const bool swapchain_was_valid = swapchain_.IsValid();
        bool swapchain_created_in_initialize = false;
        bool frame_sync_initialized = false;
        bool frame_commands_initialized = false;

        try {
            if (!swapchain_was_valid) {
                swapchain_.Initialize(context_,
                                      window_surface_,
                                      create_info_cache.swapchain,
                                      &frame_retire,
                                      frame_sync.LastSubmittedValue());
                swapchain_created_in_initialize = true;
            }

            frame_sync.Initialize(context_, swapchain_.ImageCount());
            frame_sync_initialized = true;

            frame_commands.Initialize(context_, create_info_cache.commands);
            frame_commands_initialized = true;

            last_known_swapchain_generation = swapchain_.Generation();
            last_known_swapchain_image_count = swapchain_.ImageCount();
            last_known_swapchain_extent = swapchain_.Extent();
            last_known_swapchain_format = swapchain_.Format();
            recorder_swapchain_notified = false;
            initialized = true;
        } catch (...) {
            if (frame_commands_initialized || frame_commands.IsInitialized()) {
                frame_commands.Shutdown(context_);
            }
            if (frame_sync_initialized || frame_sync.IsInitialized()) {
                frame_sync.Shutdown(context_);
            }
            if (swapchain_created_in_initialize && swapchain_.IsValid()) {
                swapchain_.Shutdown(context_,
                                    &frame_retire,
                                    frame_sync.CompletedSubmitValue());
            }
            (void)frame_retire.Flush(context_.Device());
            throw;
        }
    }

    void Shutdown(VulkanContext& context_, SwapchainHostT& swapchain_) {
        if (!initialized) {
            return;
        }

        frame_sync.Shutdown(context_);
        if (swapchain_.IsValid()) {
            swapchain_.Shutdown(context_,
                                &frame_retire,
                                frame_sync.CompletedSubmitValue());
        }
        (void)frame_retire.Collect(context_.Device(),
                                   frame_sync.CompletedSubmitValue(),
                                   std::numeric_limits<uint32_t>::max());
        (void)frame_retire.Flush(context_.Device());

        frame_commands.Shutdown(context_);

        initialized = false;
        last_known_swapchain_generation = 0U;
        last_known_swapchain_image_count = 0U;
        last_known_swapchain_extent = {};
        last_known_swapchain_format = VK_FORMAT_UNDEFINED;
        recorder_swapchain_notified = false;
    }

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
                                  uint32_t extra_submit_wait_count_) {
        if (!initialized) {
            throw std::runtime_error("RenderLoopHost::Tick called before Initialize");
        }

        if (!swapchain_.EnsureValid(context_,
                                    window_surface_,
                                    create_info_cache.swapchain,
                                    &frame_retire,
                                    frame_sync.LastSubmittedValue())) {
            (void)frame_retire.Collect(context_.Device(),
                                       frame_sync.CompletedSubmitValue(),
                                       create_info_cache.retire_collect_budget_per_type);
            return {
                .code = TickCode::SkippedWindowHidden,
                .frame_index = frame_sync.CurrentFrameIndex(),
                .image_index = 0U
            };
        }

        const uint64_t current_generation = swapchain_.Generation();
        const uint32_t current_image_count = swapchain_.ImageCount();
        const VkExtent2D current_extent = swapchain_.Extent();
        const VkFormat current_format = swapchain_.Format();

        const bool swapchain_recreated = (current_generation != last_known_swapchain_generation);
        const bool swapchain_signature_changed =
            (current_image_count != last_known_swapchain_image_count) ||
            (current_extent.width != last_known_swapchain_extent.width) ||
            (current_extent.height != last_known_swapchain_extent.height) ||
            (current_format != last_known_swapchain_format);

        if (swapchain_recreated || current_image_count != last_known_swapchain_image_count) {
            frame_sync.OnSwapchainRecreated(current_image_count);
        }

        if (!recorder_swapchain_notified || swapchain_recreated || swapchain_signature_changed) {
            NotifySwapchainRecreatedIfSupported(recorder_,
                                                current_image_count,
                                                current_extent,
                                                current_format);
            recorder_swapchain_notified = true;
        }

        last_known_swapchain_generation = current_generation;
        last_known_swapchain_image_count = current_image_count;
        last_known_swapchain_extent = current_extent;
        last_known_swapchain_format = current_format;

        auto begin_result = frame_sync.BeginFrame(context_, swapchain_);
        if (begin_result.code == FrameBeginCode::RecreateSwapchain) {
            swapchain_.MarkDirty();
            return {
                .code = TickCode::RecreateRequested,
                .frame_index = begin_result.token.frame_index,
                .image_index = begin_result.token.image_index
            };
        }

        const FrameToken token = begin_result.token;
        frame_commands.ResetFrame(context_, token.frame_index);

        VkCommandBuffer command_buffer = frame_commands.BeginPrimary(context_,
                                                                     token.frame_index,
                                                                     create_info_cache.command_usage_flags);
        RecordFrame(recorder_,
                    command_buffer,
                    token.frame_index,
                    token.image_index,
                    swapchain_.Extent(),
                    swapchain_.Format(),
                    swapchain_.Image(token.image_index),
                    swapchain_.ImageView(token.image_index));
        frame_commands.EndCommandBuffer(command_buffer);

        (void)frame_sync.Submit(context_,
                                token,
                                command_buffer,
                                create_info_cache.submit_wait_stage_mask,
                                extra_submit_waits_,
                                extra_submit_wait_count_);

        const bool need_recreate = frame_sync.Present(context_, swapchain_, token);
        if (need_recreate) {
            swapchain_.MarkDirty();
        }

        frame_sync.AdvanceFrame();
        (void)frame_retire.Collect(context_.Device(),
                                   frame_sync.CompletedSubmitValue(),
                                   create_info_cache.retire_collect_budget_per_type);
        return {
            .code = need_recreate ? TickCode::RecreateRequested : TickCode::Submitted,
            .frame_index = token.frame_index,
            .image_index = token.image_index
        };
    }

    [[nodiscard]] bool IsInitialized() const noexcept {
        return initialized;
    }

    [[nodiscard]] const FrameSyncHost<frames_in_flight_v>& Sync() const noexcept {
        return frame_sync;
    }

    [[nodiscard]] FrameSyncHost<frames_in_flight_v>& Sync() noexcept {
        return frame_sync;
    }

    [[nodiscard]] const FrameCommandHost& Commands() const noexcept {
        return frame_commands;
    }

    [[nodiscard]] FrameCommandHost& Commands() noexcept {
        return frame_commands;
    }

    [[nodiscard]] const FrameRetireHost& Retire() const noexcept {
        return frame_retire;
    }

    [[nodiscard]] FrameRetireHost& Retire() noexcept {
        return frame_retire;
    }

    [[nodiscard]] const RenderLoopCreateInfo& Config() const noexcept {
        return create_info_cache;
    }

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
                                                    VkFormat format_) {
        if constexpr (requires(RecorderT& recorder__,
                               uint32_t image_count__,
                               VkExtent2D extent__,
                               VkFormat format__) {
                          recorder__.OnSwapchainRecreated(image_count__, extent__, format__);
                      }) {
            recorder_.OnSwapchainRecreated(image_count_, extent_, format_);
        } else if constexpr (requires(RecorderT& recorder__,
                                      uint32_t image_count__,
                                      VkExtent2D extent__) {
                                 recorder__.OnSwapchainRecreated(image_count__, extent__);
                             }) {
            recorder_.OnSwapchainRecreated(image_count_, extent_);
        } else if constexpr (requires(RecorderT& recorder__, uint32_t image_count__) {
                                 recorder__.OnSwapchainRecreated(image_count__);
                             }) {
            recorder_.OnSwapchainRecreated(image_count_);
        }
    }

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

} // namespace vr::render
