#pragma once

#include "vr/asset/texture_host.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/platform/render_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/environment/sky_environment_gpu_host.hpp"
#include "vr/render/frame_composer_host.hpp"
#include "vr/render/ibl_bake_host.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/runtime/runtime_diagnostics.hpp"
#include "vr/text/freetype_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"
#include "vr/text/glyph_upload_host.hpp"

#include <cstdint>

namespace vr::runtime {

struct RuntimeModulesCreateInfo final {
    bool enable_texture_host = true;
    bool enable_frame_composer_host = true;
    bool enable_ibl_host = true;
    bool enable_ibl_bake_host = false;
    bool enable_sky_environment_gpu_host = true;
    bool enable_upload_host = true;
    bool enable_descriptor_host = true;
    bool enable_pipeline_host = true;
    bool enable_render_target_host = true;
    bool enable_sampler_host = true;
    bool enable_freetype_host = true;
    bool enable_glyph_atlas_host = true;
    bool enable_glyph_upload_host = true;
    bool enable_particle_upload_host = true;
    bool enable_particle_simulation_host = true;
};

template<typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
struct RuntimePipelineWarmupCreateInfo final {
    std::uint32_t max_graphics_compiles_per_tick = 0U;
    std::uint32_t max_compute_compiles_per_tick = 0U;
    bool compile_before_render = true;
    bool compile_after_render = false;
};

template<typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
struct RuntimeCreateInfo final {
    platform::RenderHostCreateInfo platform{};
    render::RenderLoopCreateInfo render_loop{};

    resource::GpuMemoryHostCreateInfo gpu_memory{};
    asset::TextureHostCreateInfo texture{};
    render::FrameComposerHostCreateInfo frame_composer{};
    render::IblHostCreateInfo ibl{};
    render::IblBakeHostCreateInfo ibl_bake{};
    render::SkyEnvironmentGpuHostCreateInfo sky_environment{};
    render::UploadHostCreateInfo upload{};
    render::DescriptorHostCreateInfo descriptor{};
    render::BindlessResourceSystemCreateInfo bindless{};
    render::PipelineHostCreateInfo pipeline{};
    render::RenderTargetHostCreateInfo render_target{};
    resource::SamplerHostCreateInfo sampler{};
    text::FreeTypeHostCreateInfo freetype{};
    text::GlyphAtlasCreateInfo glyph_atlas{};
    text::GlyphUploadHostCreateInfo glyph_upload{};
    particle::ParticleUploadHostCreateInfo particle_upload{};
    particle::ParticleSimulationHostCreateInfo particle_simulation{};

    RuntimeModulesCreateInfo modules{};
    RuntimeDiagnosticsCreateInfo diagnostics{};
    RuntimePipelineWarmupCreateInfo<BackendTagT, frames_in_flight_v> pipeline_warmup{};

    bool poll_events_each_tick = true;
    VkPipelineStageFlags upload_wait_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
};

} // namespace vr::runtime
