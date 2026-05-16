#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <vulkan/vulkan.h>

namespace vr::runtime {

struct EmptyServiceCreateInfo final {};

namespace detail {

template<typename ContextT>
[[nodiscard]] decltype(auto) ResolveFrameContext(ContextT& context_) noexcept {
    if constexpr (requires(ContextT& ctx_) { ctx_.frame_context; }) {
        return (context_.frame_context);
    } else {
        return (context_);
    }
}

template<typename ContextT>
[[nodiscard]] decltype(auto) ResolveDevice(ContextT& context_) noexcept {
    auto& frame_context = ResolveFrameContext(context_);
    if constexpr (requires { frame_context.device; }) {
        return (frame_context.device);
    } else {
        return (frame_context.kernel.Device());
    }
}

template<typename ContextT>
[[nodiscard]] decltype(auto) ResolveServices(ContextT& context_) noexcept {
    return (ResolveFrameContext(context_).services);
}

template<typename ContextT>
[[nodiscard]] std::uint32_t ResolveFrameIndex(ContextT& context_) noexcept {
    return ResolveFrameContext(context_).frame.frame_index;
}

template<typename ContextT>
[[nodiscard]] std::uint64_t ResolveGraphicsSubmitted(ContextT& context_) noexcept {
    return ResolveFrameContext(context_).progress.graphics_submitted;
}

template<typename ContextT>
[[nodiscard]] std::uint64_t ResolveGraphicsCompleted(ContextT& context_) noexcept {
    return ResolveFrameContext(context_).progress.graphics_completed;
}

template<typename ContextT>
[[nodiscard]] VkCommandBuffer ResolveCommandBuffer(ContextT& context_) noexcept {
    auto& frame_context = ResolveFrameContext(context_);
    if constexpr (requires { frame_context.command_buffer; }) {
        return frame_context.command_buffer;
    }
    return VK_NULL_HANDLE;
}

} // namespace detail

template<typename T>
concept RuntimeService = requires {
    typename T::CreateInfo;
    typename T::Dependencies;
    { T::Name } -> std::convertible_to<std::string_view>;
};

template<typename ServiceT, typename ContextT>
concept RuntimeServiceSupportsInitialize = requires(ServiceT& service_, ContextT& context_) {
    service_.Initialize(context_);
};

template<typename ServiceT, typename ContextT>
concept RuntimeServiceSupportsPostInitialize = requires(ServiceT& service_, ContextT& context_) {
    service_.PostInitialize(context_);
};

template<typename ServiceT, typename ContextT>
concept RuntimeServiceSupportsBeginFrame = requires(ServiceT& service_, ContextT& context_) {
    service_.BeginFrame(context_);
};

template<typename ServiceT, typename ContextT>
concept RuntimeServiceSupportsPrepareFrame = requires(ServiceT& service_, ContextT& context_) {
    service_.PrepareFrame(context_);
};

template<typename ServiceT, typename ContextT>
concept RuntimeServiceSupportsPreRecord = requires(ServiceT& service_, ContextT& context_) {
    service_.PreRecord(context_);
};

template<typename ServiceT, typename ContextT>
concept RuntimeServiceSupportsRecord = requires(ServiceT& service_, ContextT& context_) {
    service_.Record(context_);
};

template<typename ServiceT, typename ContextT>
concept RuntimeServiceSupportsPostRecord = requires(ServiceT& service_, ContextT& context_) {
    service_.PostRecord(context_);
};

template<typename ServiceT, typename ContextT>
concept RuntimeServiceSupportsEndFrame = requires(ServiceT& service_, ContextT& context_) {
    service_.EndFrame(context_);
};

template<typename ServiceT, typename ContextT>
concept RuntimeServiceSupportsRetire = requires(ServiceT& service_, ContextT& context_) {
    service_.Retire(context_);
};

template<typename ServiceT, typename ContextT>
concept RuntimeServiceSupportsShutdown = requires(ServiceT& service_, ContextT& context_) {
    service_.Shutdown(context_);
};

template<typename ServiceT, typename ContextT>
inline void CallInitializeIfSupported(ServiceT& service_, ContextT& context_) {
    if constexpr (RuntimeServiceSupportsInitialize<ServiceT, ContextT>) {
        service_.Initialize(context_);
    }
}

template<typename ServiceT, typename ContextT>
inline void CallPostInitializeIfSupported(ServiceT& service_, ContextT& context_) {
    if constexpr (RuntimeServiceSupportsPostInitialize<ServiceT, ContextT>) {
        service_.PostInitialize(context_);
    }
}

template<typename ServiceT, typename ContextT>
inline void CallBeginFrameIfSupported(ServiceT& service_, ContextT& context_) {
    if constexpr (RuntimeServiceSupportsBeginFrame<ServiceT, ContextT>) {
        service_.BeginFrame(context_);
    }
}

template<typename ServiceT, typename ContextT>
inline void CallPrepareFrameIfSupported(ServiceT& service_, ContextT& context_) {
    if constexpr (RuntimeServiceSupportsPrepareFrame<ServiceT, ContextT>) {
        service_.PrepareFrame(context_);
    }
}

template<typename ServiceT, typename ContextT>
inline void CallPreRecordIfSupported(ServiceT& service_, ContextT& context_) {
    if constexpr (RuntimeServiceSupportsPreRecord<ServiceT, ContextT>) {
        service_.PreRecord(context_);
    }
}

template<typename ServiceT, typename ContextT>
inline void CallRecordIfSupported(ServiceT& service_, ContextT& context_) {
    if constexpr (RuntimeServiceSupportsRecord<ServiceT, ContextT>) {
        service_.Record(context_);
    }
}

template<typename ServiceT, typename ContextT>
inline void CallPostRecordIfSupported(ServiceT& service_, ContextT& context_) {
    if constexpr (RuntimeServiceSupportsPostRecord<ServiceT, ContextT>) {
        service_.PostRecord(context_);
    }
}

template<typename ServiceT, typename ContextT>
inline void CallEndFrameIfSupported(ServiceT& service_, ContextT& context_) {
    if constexpr (RuntimeServiceSupportsEndFrame<ServiceT, ContextT>) {
        service_.EndFrame(context_);
    }
}

template<typename ServiceT, typename ContextT>
inline void CallRetireIfSupported(ServiceT& service_, ContextT& context_) {
    if constexpr (RuntimeServiceSupportsRetire<ServiceT, ContextT>) {
        service_.Retire(context_);
    }
}

template<typename ServiceT, typename ContextT>
inline void CallShutdownIfSupported(ServiceT& service_, ContextT& context_) {
    if constexpr (RuntimeServiceSupportsShutdown<ServiceT, ContextT>) {
        service_.Shutdown(context_);
    }
}

} // namespace vr::runtime

