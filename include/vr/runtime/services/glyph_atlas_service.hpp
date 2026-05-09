#pragma once

#include "vr/text/glyph_atlas_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/freetype_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class GlyphAtlasService final : public detail::BoundHostService<vr::text::GlyphAtlasHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<FreeTypeService>;
    static constexpr std::string_view Name = "GlyphAtlasService";

    [[nodiscard]] vr::text::GlyphAtlasHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::text::GlyphAtlasHost& Host() const {
        return this->RequireHost(Name);
    }
};

} // namespace vr::runtime::services
