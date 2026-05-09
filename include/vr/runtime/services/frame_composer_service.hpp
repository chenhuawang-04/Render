#pragma once

#include "vr/render/frame_composer_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/pipeline_service.hpp"
#include "vr/runtime/services/render_target_pool_service.hpp"
#include "vr/runtime/services/render_target_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class FrameComposerService final : public detail::BoundHostService<vr::render::FrameComposerHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<
        DescriptorService,
        PipelineService,
        RenderTargetService,
        RenderTargetPoolService>;
    static constexpr std::string_view Name = "FrameComposerService";

    [[nodiscard]] vr::render::FrameComposerHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::render::FrameComposerHost& Host() const {
        return this->RequireHost(Name);
    }
};

} // namespace vr::runtime::services
