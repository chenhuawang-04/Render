#pragma once

#include "vr/platform/window_surface.hpp"
#include "vr/vulkan_context.hpp"

#include <stdexcept>

namespace vr::platform {

struct RenderHostCreateInfo {
    WindowCreateInfo window{};
    VulkanInstanceCreateInfo instance{};
    VulkanDeviceCreateInfo device{};
};

template<typename BackendTagT>
class RenderHost;

template<>
class RenderHost<Sdl3BackendTag> final {
public:
    using BackendTag = Sdl3BackendTag;
    using WindowSurfaceType = WindowSurface<BackendTag>;

    RenderHost() = default;
    ~RenderHost() {
        Shutdown();
    }

    RenderHost(const RenderHost&) = delete;
    RenderHost& operator=(const RenderHost&) = delete;

    RenderHost(RenderHost&&) = delete;
    RenderHost& operator=(RenderHost&&) = delete;

    void Initialize(const RenderHostCreateInfo& create_info_ = {}) {
        Shutdown();

        window.Initialize(create_info_.window);

        VulkanInstanceCreateInfo instance_info = create_info_.instance;
        window.AppendRequiredInstanceExtensions(instance_info.required_extensions);
        context.InitializeInstance(instance_info);

        bool surface_created = false;
        try {
            window.CreateSurface(context.Instance());
            surface_created = true;
            context.InitializeDevice(create_info_.device, window.Surface());
        } catch (...) {
            if (surface_created) {
                window.DestroySurface();
            }
            context.Shutdown();
            window.Shutdown();
            throw;
        }
    }

    void Shutdown() {
        if (context.IsDeviceInitialized()) {
            context.ShutdownDevice();
        }

        if (window.HasSurface()) {
            window.DestroySurface();
        }

        if (context.IsInstanceInitialized()) {
            context.Shutdown();
        }

        if (window.IsInitialized()) {
            window.Shutdown();
        }
    }

    [[nodiscard]] bool PollAndHandleCloseEvents() {
        SDL_Event event{};
        bool got_event = false;
        while (window.PollEvent(&event)) {
            got_event = true;
            (void)window.HandleCloseEvent(event);
        }
        return got_event;
    }

    [[nodiscard]] bool IsRunning() const noexcept {
        return window.IsOpen();
    }

    void RequestClose() noexcept {
        window.RequestClose();
    }

    [[nodiscard]] VulkanContext& Context() noexcept {
        return context;
    }

    [[nodiscard]] const VulkanContext& Context() const noexcept {
        return context;
    }

    [[nodiscard]] WindowSurfaceType& SurfaceHost() noexcept {
        return window;
    }

    [[nodiscard]] const WindowSurfaceType& SurfaceHost() const noexcept {
        return window;
    }

private:
    VulkanContext context{};
    WindowSurfaceType window{};
};

using DefaultRenderHost = RenderHost<ActiveBackendTag>;

} // namespace vr::platform

