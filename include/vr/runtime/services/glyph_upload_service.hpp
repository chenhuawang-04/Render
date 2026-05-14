#pragma once

#include "vr/text/glyph_upload_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/glyph_atlas_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/sampler_service.hpp"
#include "vr/runtime/services/upload_service.hpp"

#include <string_view>
#include <type_traits>

namespace vr::runtime::services {

class GlyphUploadService final : public detail::BoundHostService<vr::text::GlyphUploadHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<
        GlyphAtlasService,
        GpuMemoryService,
        UploadService,
        DescriptorService,
        SamplerService>;
    static constexpr std::string_view Name = "GlyphUploadService";

    [[nodiscard]] vr::text::GlyphUploadHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::text::GlyphUploadHost& Host() const {
        return this->RequireHost(Name);
    }

    template<typename ContextT>
    void PrepareFrame(ContextT& context_) {
        auto* host_ = this->HostPtr();
        if (host_ == nullptr) {
            return;
        }

        auto& services_ = vr::runtime::detail::ResolveServices(context_);
        if constexpr (std::remove_reference_t<decltype(services_)>::template Contains<UploadService>() &&
                      std::remove_reference_t<decltype(services_)>::template Contains<GlyphAtlasService>()) {
            auto* upload_service_ = services_.template TryGet<UploadService>();
            auto* atlas_service_ = services_.template TryGet<GlyphAtlasService>();
            if (upload_service_ != nullptr && atlas_service_ != nullptr) {
                host_->UploadDirtyPages(vr::runtime::detail::ResolveDevice(context_),
                                        upload_service_->Host(),
                                        vr::runtime::detail::ResolveFrameIndex(context_),
                                        atlas_service_->Host(),
                                        vr::runtime::detail::ResolveGraphicsSubmitted(context_),
                                        vr::runtime::detail::ResolveGraphicsCompleted(context_));
            }
        }
    }
};

} // namespace vr::runtime::services

