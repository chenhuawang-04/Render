#pragma once

#include "vr/runtime/runtime_profile.hpp"
#include "vr/runtime/services/command_service.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/freetype_service.hpp"
#include "vr/runtime/services/glyph_atlas_service.hpp"
#include "vr/runtime/services/glyph_upload_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/particle_render_service.hpp"
#include "vr/runtime/services/particle_simulation_service.hpp"
#include "vr/runtime/services/particle_upload_service.hpp"
#include "vr/runtime/services/pipeline_service.hpp"
#include "vr/runtime/services/render_target_pool_service.hpp"
#include "vr/runtime/services/render_target_service.hpp"
#include "vr/runtime/services/sampler_service.hpp"
#include "vr/runtime/services/texture_service.hpp"
#include "vr/runtime/services/upload_service.hpp"

namespace vr::runtime::profiles {

using Runtime2DProfile = vr::runtime::RuntimeProfile<
    services::GpuMemoryService,
    services::CommandService,
    services::UploadService,
    services::DescriptorService,
    services::PipelineService,
    services::SamplerService,
    services::TextureService,
    services::RenderTargetService,
    services::RenderTargetPoolService,
    services::FreeTypeService,
    services::GlyphAtlasService,
    services::GlyphUploadService,
    services::ParticleUploadService,
    services::ParticleSimulationService,
    services::ParticleRenderService>;

} // namespace vr::runtime::profiles
