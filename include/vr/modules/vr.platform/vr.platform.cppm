module;
// Global module fragment
#include "vr/detail/vr_module_fwd.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

export module vr.platform;
import vr.types;
import vr.context;

export {
namespace vr::platform {

// --- window_surface.hpp ------------------------------------------------------

enum class BackendKind : std::uint8_t {
    Sdl3 = 0
};

template<BackendKind backend_kind_>
struct BackendTag final {};

using Sdl3BackendTag = BackendTag<BackendKind::Sdl3>;
using ActiveBackendTag = Sdl3BackendTag;

struct WindowCreateInfo {
    const char* title = "VulkanRender_New";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    bool high_pixel_density = true;
};

template<typename BackendTagT>
class WindowSurface;

template<>
class WindowSurface<Sdl3BackendTag> final {
public:
    WindowSurface() = default;

    ~WindowSurface() {
        Shutdown();
    }

    WindowSurface(const WindowSurface&) = delete;
    WindowSurface& operator=(const WindowSurface&) = delete;

    WindowSurface(WindowSurface&& other_) noexcept {
        *this = std::move(other_);
    }

    WindowSurface& operator=(WindowSurface&& other_) noexcept {
        if (this == &other_) {
            return *this;
        }

        Shutdown();

        window = std::exchange(other_.window, nullptr);
        window_id = std::exchange(other_.window_id, 0U);
        is_open = std::exchange(other_.is_open, false);
        owns_video_subsystem = std::exchange(other_.owns_video_subsystem, false);
        instance = std::exchange(other_.instance, VK_NULL_HANDLE);
        surface = std::exchange(other_.surface, VK_NULL_HANDLE);
        return *this;
    }

    void Initialize(const WindowCreateInfo& create_info_ = {}) {
        if (window != nullptr) {
            throw std::runtime_error("WindowSurface::Initialize called twice");
        }

        const bool video_was_initialized = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0U;
        if (!video_was_initialized) {
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
                throw std::runtime_error(std::string("SDL_InitSubSystem(SDL_INIT_VIDEO) failed: ") + SDL_GetError());
            }
            owns_video_subsystem = true;
        } else {
            owns_video_subsystem = false;
        }

        SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
        if (create_info_.resizable) {
            flags |= SDL_WINDOW_RESIZABLE;
        }
        if (create_info_.high_pixel_density) {
            flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        }

        window = SDL_CreateWindow(create_info_.title, create_info_.width, create_info_.height, flags);
        if (window == nullptr) {
            Shutdown();
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }

        window_id = SDL_GetWindowID(window);
        is_open = true;
    }

    void Shutdown() {
        DestroySurface();

        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        window_id = 0U;
        is_open = false;

        if (owns_video_subsystem) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            owns_video_subsystem = false;
        }
    }

    void AppendRequiredInstanceExtensions(McVector<const char*>& extensions_) const {
        if (window == nullptr) {
            throw std::runtime_error("AppendRequiredInstanceExtensions requires a valid SDL window");
        }

        Uint32 count = 0U;
        const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
        if (names == nullptr) {
            throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
        }

        extensions_.reserve(extensions_.size() + count);
        for (Uint32 i = 0U; i < count; ++i) {
            const char* name = names[i];
            bool found = false;
            for (const char* ext : extensions_) {
                if (std::strcmp(ext, name) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                extensions_.push_back(name);
            }
        }
    }

    void CreateSurface(VkInstance instance_) {
        if (window == nullptr) {
            throw std::runtime_error("CreateSurface requires a valid SDL window");
        }
        if (instance_ == VK_NULL_HANDLE) {
            throw std::runtime_error("CreateSurface requires a valid VkInstance");
        }

        DestroySurface();

        VkSurfaceKHR new_surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &new_surface)) {
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
        }

        instance = instance_;
        surface = new_surface;
    }

    void DestroySurface() {
        if (surface != VK_NULL_HANDLE) {
            if (instance != VK_NULL_HANDLE) {
                SDL_Vulkan_DestroySurface(instance, surface, nullptr);
            }
            surface = VK_NULL_HANDLE;
        }
        instance = VK_NULL_HANDLE;
    }

    [[nodiscard]] bool PollEvent(SDL_Event* event_) const {
        return SDL_PollEvent(event_);
    }

    [[nodiscard]] bool HandleCloseEvent(const SDL_Event& event_) noexcept {
        if (event_.type == SDL_EVENT_QUIT) {
            is_open = false;
            return true;
        }
        if (event_.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event_.window.windowID == window_id) {
            is_open = false;
            return true;
        }
        return false;
    }

    void RequestClose() noexcept {
        is_open = false;
    }

    [[nodiscard]] bool IsOpen() const noexcept {
        return is_open;
    }

    [[nodiscard]] bool IsInitialized() const noexcept {
        return window != nullptr;
    }

    [[nodiscard]] bool HasSurface() const noexcept {
        return surface != VK_NULL_HANDLE;
    }

    [[nodiscard]] SDL_Window* NativeWindow() const noexcept {
        return window;
    }

    [[nodiscard]] VkSurfaceKHR Surface() const noexcept {
        return surface;
    }

    [[nodiscard]] SDL_WindowID WindowId() const noexcept {
        return window_id;
    }

    void SetTitle(const char* title_) {
        if (window == nullptr) {
            throw std::runtime_error("SetTitle requires a valid SDL window");
        }
        if (!SDL_SetWindowTitle(window, title_)) {
            throw std::runtime_error(std::string("SDL_SetWindowTitle failed: ") + SDL_GetError());
        }
    }

    void QueryFramebufferSize(int& width_, int& height_) const {
        if (window == nullptr) {
            throw std::runtime_error("QueryFramebufferSize requires a valid SDL window");
        }
        if (!SDL_GetWindowSizeInPixels(window, &width_, &height_)) {
            throw std::runtime_error(std::string("SDL_GetWindowSizeInPixels failed: ") + SDL_GetError());
        }
    }

private:
    SDL_Window* window = nullptr;
    SDL_WindowID window_id = 0U;
    bool is_open = false;
    bool owns_video_subsystem = false;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
};

using DefaultWindowSurface = WindowSurface<ActiveBackendTag>;

// --- render_host.hpp ---------------------------------------------------------

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
} // export
