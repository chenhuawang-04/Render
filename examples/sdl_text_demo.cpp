#include "vr/ecs/system/text_system.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/text/text_renderer_2d.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Text2D = vr::ecs::Text<vr::ecs::Dim2>;
using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

void InitializeTextComponent(Text2D& component_,
                             std::uint32_t font_id_,
                             std::uint32_t material_id_,
                             std::int16_t layer_,
                             std::string_view text_) {
    TextSystem2D::Initialize(component_);
    TextSystem2D::SetRuntimeRoute(component_, font_id_, material_id_, 0U, 0U);
    TextSystem2D::SetLayer(component_, layer_);
    TextSystem2D::SetColor(component_, vr::ecs::Rgba8{255U, 255U, 255U, 255U});
    TextSystem2D::SetOutlineEnabled(component_, true);
    TextSystem2D::SetOutlineWidthPx(component_, 1U);
    TextSystem2D::SetOutlineColor(component_, vr::ecs::Rgba8{12U, 14U, 20U, 255U});
    (void)TextSystem2D::SetText(component_, text_);
}

} // namespace

int main() {
    try {
        Runtime runtime{};
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "Vulkan SDL3 Text Demo";
        create_info.platform.window.width = 1280;
        create_info.platform.window.height = 720;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = true;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;

        create_info.render_loop.swapchain.enable_vsync = true;
        create_info.render_loop.swapchain.preferred_image_count = 3U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;

        runtime.Initialize(create_info);

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = "C:/Windows/Fonts/segoeui.ttf";
        face_create_info.pixel_height = 28U;
        const vr::text::FontFaceId base_face_id = runtime.FreeType().RegisterFace(face_create_info);
        runtime.GlyphAtlas().MapFont(1U, base_face_id);

        std::array<Text2D, 4U> text_components{};
        InitializeTextComponent(text_components[0U], 1U, 1U, 0, "Melosyne Vulkan Text");
        TextSystem2D::SetPixelSize(text_components[0U], 44.0F);

        InitializeTextComponent(text_components[1U], 1U, 1U, 1, "Runtime + ECS + FreeType + Atlas + Upload");
        TextSystem2D::SetPixelSize(text_components[1U], 24.0F);

        InitializeTextComponent(text_components[2U], 1U, 1U, 2, "Press window close button to exit");
        TextSystem2D::SetPixelSize(text_components[2U], 20.0F);

        InitializeTextComponent(text_components[3U], 1U, 1U, 3, "Frame: 0");
        TextSystem2D::SetPixelSize(text_components[3U], 26.0F);
        TextSystem2D::SetColor(text_components[3U], vr::ecs::Rgba8{220U, 255U, 166U, 255U});

        vr::text::TextRenderer2D text_renderer{};
        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.runtime_build.pixel_size_quantization = 1.0F;
        text_renderer_create_info.runtime_build.enable_kerning = true;
        text_renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(text_components.size());
        text_renderer_create_info.reserve_glyph_count = 32768U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 4U * 1024U * 1024U;
        text_renderer_create_info.clear_swapchain = true;
        text_renderer_create_info.clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
        text_renderer.Initialize(text_renderer_create_info);
        text_renderer.SetComponents(text_components.data(),
                                    static_cast<std::uint32_t>(text_components.size()));

        std::uint64_t frame_counter = 0U;
        std::cout << "sdl_text_demo running. Close window to exit.\n";
        while (runtime.IsRunning()) {
            char frame_text[64]{};
            std::snprintf(frame_text, sizeof(frame_text), "Frame: %llu", static_cast<unsigned long long>(frame_counter));
            (void)TextSystem2D::SetText(text_components[3U], frame_text);
            (void)runtime.Tick(text_renderer);

            SDL_Delay(8U);
            ++frame_counter;
        }

        text_renderer.Shutdown(runtime.Context());
        runtime.Shutdown();
        return 0;
    } catch (const std::exception& exception_) {
        std::cerr << "sdl_text_demo failed: " << exception_.what() << '\n';
        return 1;
    }
}

