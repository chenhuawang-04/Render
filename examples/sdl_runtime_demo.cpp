#include "vr/runtime/crash_tracer_support.hpp"
#include "vr/runtime/runtime.hpp"

#include <iostream>

namespace {

class GraphClearRecorder final {
public:
    void PrepareFrame(const vr::render::FrameComposerPrepareView&) noexcept {}

    void BuildRenderGraph(
        vr::render_graph::RenderGraphBuilder& builder_,
        const vr::render_graph::ResourceHandle present_target_,
        const vr::render_graph::Extent3D& reference_extent_,
        vr::render_graph::ResourceVersionHandle& present_ready_version_,
        const vr::render::FrameComposerHost::ImportedTextureRegisterFn& register_imported_texture_) {
        (void)reference_extent_;
        (void)register_imported_texture_;

        const auto pass = builder_.AddPass("runtime_demo_clear");
        present_ready_version_ = builder_.Write(
            pass,
            present_target_,
            vr::render_graph::AccessDesc{
                .access = vr::render_graph::AccessKind::color_attachment_write,
            });
        builder_.SetRasterPassDesc(
            pass,
            vr::render_graph::RasterPassDesc{
                .color_attachments = {
                    vr::render_graph::RasterColorAttachmentDesc{
                        .target = present_target_,
                        .load_op = vr::render_graph::AttachmentLoadOp::clear,
                        .store_op = vr::render_graph::AttachmentStoreOp::store,
                        .clear_value = {.red = 0.09F, .green = 0.05F, .blue = 0.18F, .alpha = 1.0F},
                    },
                },
            });
        builder_.SetExecuteCallback(pass, [](vr::render_graph::GraphCommandContext&) {});
    }
};

} // namespace

int main(int argc_, char** argv_) {
    vr::runtime::InstallProcessCrashTracer(argc_, argv_);
    try {
        using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;

        Runtime::CreateInfo create_info;
        create_info.platform.window.title = "Vulkan SDL3 RuntimeHost Demo";
        create_info.platform.window.width = 1280;
        create_info.platform.window.height = 720;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = true;

        create_info.render_loop.swapchain.enable_vsync = true;
        create_info.render_loop.swapchain.preferred_image_count = 3U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;

        Runtime runtime;
        runtime.Initialize(create_info);

        GraphClearRecorder recorder;

        std::cout << "Runtime initialized. Close window to exit (~5s auto exit).\n";

        int loop_count = 0;
        constexpr int max_loops = 500;
        while (runtime.IsRunning() && loop_count < max_loops) {
            const auto tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::SkippedWindowHidden) {
                SDL_Delay(8);
                ++loop_count;
                continue;
            }

            SDL_Delay(8);
            ++loop_count;
        }

        runtime.Shutdown();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "sdl_runtime_demo failed: " << ex.what() << '\n';
        return 1;
    }
}

