#pragma once

#include "vr/render/swapchain_target_set.hpp"
#include "vr/runtime/queue_timeline.hpp"
#include "vr/runtime/runtime_services.hpp"
#include "vr/vulkan_context.hpp"

namespace vr::runtime {

class CommandService;
struct RuntimeExecutionTrace;

template<typename BackendTagT, std::uint32_t frames_in_flight_v>
class RuntimeKernel;

struct FrameStaticContext final {
    std::uint64_t frame_id = 0U;
    std::uint32_t frame_index = 0U;
    std::uint32_t image_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
};

struct FrameGpuProgressContext final {
    std::uint64_t graphics_submitted = 0U;
    std::uint64_t graphics_completed = 0U;
    std::uint64_t transfer_submitted = 0U;
    std::uint64_t transfer_completed = 0U;
    std::uint64_t compute_submitted = 0U;
    std::uint64_t compute_completed = 0U;
};

template<typename ProfileT,
         typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
struct RuntimeFrameContext final {
    using Profile = ProfileT;
    using BackendTag = BackendTagT;
    using KernelType = RuntimeKernel<BackendTagT, frames_in_flight_v>;
    using ServicesType = RuntimeServices<ProfileT>;
    static constexpr std::uint32_t FramesInFlight = frames_in_flight_v;

    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
    QueueTimelineSet timelines{};
    ServicesType& services;
    KernelType& kernel;
    CommandService& commands;
    vr::render::SwapchainTargetSet* swapchain_targets = nullptr;
};

template<typename ProfileT,
         typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
struct RuntimeInitContext final {
    using Profile = ProfileT;
    using KernelType = RuntimeKernel<BackendTagT, frames_in_flight_v>;
    using ServicesType = RuntimeServices<ProfileT>;

    ServicesType& services;
    KernelType& kernel;
    VulkanContext* device = nullptr;
};

template<typename ProfileT,
         typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
using RuntimePostInitContext = RuntimeInitContext<ProfileT, BackendTagT, frames_in_flight_v>;

template<typename ProfileT,
         typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
struct RuntimeBeginFrameContext final {
    using FrameContextType = RuntimeFrameContext<ProfileT, BackendTagT, frames_in_flight_v>;

    FrameContextType& frame_context;
    RuntimeExecutionTrace& execution;
};

template<typename ProfileT,
         typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
using RuntimePreparePhaseContext = RuntimeBeginFrameContext<ProfileT, BackendTagT, frames_in_flight_v>;

template<typename ProfileT,
         typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
using RuntimeRecordPhaseContext = RuntimeBeginFrameContext<ProfileT, BackendTagT, frames_in_flight_v>;

template<typename ProfileT,
         typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
using RuntimeEndFrameContext = RuntimeBeginFrameContext<ProfileT, BackendTagT, frames_in_flight_v>;

template<typename ProfileT,
         typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
using RuntimeRetireContext = RuntimeBeginFrameContext<ProfileT, BackendTagT, frames_in_flight_v>;

template<typename ProfileT,
         typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
struct RuntimeShutdownContext final {
    using Profile = ProfileT;
    using KernelType = RuntimeKernel<BackendTagT, frames_in_flight_v>;
    using ServicesType = RuntimeServices<ProfileT>;

    ServicesType& services;
    KernelType& kernel;
    VulkanContext* device = nullptr;
};

} // namespace vr::runtime

