#pragma once

#include "vr/asset/texture_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/upload_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class TextureService final : public detail::BoundHostService<vr::asset::TextureHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<GpuMemoryService, UploadService>;
    static constexpr std::string_view Name = "TextureService";

    [[nodiscard]] vr::asset::TextureHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::asset::TextureHost& Host() const {
        return this->RequireHost(Name);
    }

    template<typename ContextT>
    void BeginFrame(ContextT& context_) {
        if (auto* host_ = this->HostPtr()) {
            host_->BeginFrame(vr::runtime::detail::ResolveDevice(context_),
                              vr::runtime::detail::ResolveGraphicsCompleted(context_));
        }
    }
};

} // namespace vr::runtime::services
