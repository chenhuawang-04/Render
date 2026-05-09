#pragma once

#include "vr/runtime/runtime_profile.hpp"
#include "vr/runtime/services/command_service.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/pipeline_service.hpp"

namespace vr::runtime::profiles {

using MinimalProfile = vr::runtime::RuntimeProfile<
    services::GpuMemoryService,
    services::CommandService,
    services::DescriptorService,
    services::PipelineService>;

} // namespace vr::runtime::profiles
