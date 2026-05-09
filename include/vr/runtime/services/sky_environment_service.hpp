#pragma once

#include "vr/render/environment/sky_environment_gpu_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/pipeline_service.hpp"
#include "vr/runtime/services/sampler_service.hpp"
#include "vr/runtime/services/texture_service.hpp"
#include "vr/runtime/services/upload_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class SkyEnvironmentService final
    : public detail::BoundHostService<vr::render::SkyEnvironmentGpuHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<
        TextureService,
        DescriptorService,
        PipelineService,
        SamplerService,
        UploadService,
        GpuMemoryService>;
    static constexpr std::string_view Name = "SkyEnvironmentService";

    [[nodiscard]] vr::render::SkyEnvironmentGpuHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::render::SkyEnvironmentGpuHost& Host() const {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::render::SkyEnvironmentGpuHostStats& Stats() const noexcept {
        static const vr::render::SkyEnvironmentGpuHostStats empty{};
        return this->HostPtr() != nullptr ? this->HostPtr()->Stats() : empty;
    }

};

} // namespace vr::runtime::services
