#pragma once

#include "vr/particle/particle_renderer_2d.hpp"
#include "vr/particle/particle_renderer_3d.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/particle_simulation_service.hpp"
#include "vr/runtime/services/particle_upload_service.hpp"
#include "vr/runtime/services/pipeline_service.hpp"
#include "vr/runtime/services/render_target_service.hpp"
#include "vr/runtime/services/sampler_service.hpp"
#include "vr/runtime/services/texture_service.hpp"
#include "vr/runtime/services/upload_service.hpp"

#include <stdexcept>
#include <string_view>

namespace vr::runtime::services {

class ParticleRenderService final {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<
        ParticleUploadService,
        ParticleSimulationService,
        TextureService,
        DescriptorService,
        PipelineService,
        SamplerService,
        RenderTargetService,
        UploadService>;
    static constexpr std::string_view Name = "ParticleRenderService";

    void Bind(ParticleUploadService& upload_service_,
              ParticleSimulationService& simulation_service_,
              TextureService* texture_service_ = nullptr) noexcept {
        upload_service = &upload_service_;
        simulation_service = &simulation_service_;
        texture_service = texture_service_;
    }

    void Reset() noexcept {
        upload_service = nullptr;
        simulation_service = nullptr;
        texture_service = nullptr;
    }

    [[nodiscard]] bool IsAvailable() const noexcept {
        return upload_service != nullptr &&
               simulation_service != nullptr &&
               upload_service->IsAvailable() &&
               simulation_service->IsAvailable();
    }

    void ConfigureRenderer(vr::particle::ParticleRenderer2D& renderer_) const noexcept {
        renderer_.SetHost(upload_service != nullptr ? upload_service->HostPtr() : nullptr);
        renderer_.SetSimulationHost(simulation_service != nullptr ? simulation_service->HostPtr() : nullptr);
        renderer_.SetTextureHost(texture_service != nullptr ? texture_service->HostPtr() : nullptr);
    }

    void ConfigureRenderer(vr::particle::ParticleRenderer3D& renderer_) const noexcept {
        renderer_.SetHost(upload_service != nullptr ? upload_service->HostPtr() : nullptr);
        renderer_.SetSimulationHost(simulation_service != nullptr ? simulation_service->HostPtr() : nullptr);
        renderer_.SetTextureHost(texture_service != nullptr ? texture_service->HostPtr() : nullptr);
    }

    [[nodiscard]] ParticleUploadService& Upload() {
        return Require(upload_service, "ParticleUploadService");
    }

    [[nodiscard]] const ParticleUploadService& Upload() const {
        return Require(upload_service, "ParticleUploadService");
    }

    [[nodiscard]] ParticleSimulationService& Simulation() {
        return Require(simulation_service, "ParticleSimulationService");
    }

    [[nodiscard]] const ParticleSimulationService& Simulation() const {
        return Require(simulation_service, "ParticleSimulationService");
    }

    [[nodiscard]] TextureService* Texture() noexcept {
        return texture_service;
    }

    [[nodiscard]] const TextureService* Texture() const noexcept {
        return texture_service;
    }

private:
    template<typename ServiceT>
    [[nodiscard]] static ServiceT& Require(ServiceT* service_, std::string_view service_name_) {
        if (service_ == nullptr) {
            throw std::runtime_error(std::string(service_name_) + " is not available");
        }
        return *service_;
    }

private:
    ParticleUploadService* upload_service = nullptr;
    ParticleSimulationService* simulation_service = nullptr;
    TextureService* texture_service = nullptr;
};

} // namespace vr::runtime::services

