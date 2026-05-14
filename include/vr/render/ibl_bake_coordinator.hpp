#pragma once

#include "vr/render/ibl_bake_host.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"

#include <cstdint>
#include <stdexcept>

namespace vr::render {

struct IblBakeCoordinatorStats final {
    std::uint32_t set_request_call_count = 0U;
    std::uint32_t clear_request_call_count = 0U;
    std::uint32_t invalidate_call_count = 0U;
    std::uint32_t prepare_count = 0U;
    std::uint32_t bake_count = 0U;
    std::uint32_t source_revision = 0U;
    std::uint32_t last_baked_revision = 0U;
    std::uint8_t configured = 0U;
    std::uint8_t baked = 0U;
    std::uint16_t reserved0 = 0U;
};

class IblBakeCoordinator final {
public:
    void Reset() noexcept {
        request = {};
        result = {};
        source_revision = 0U;
        baked_revision = 0U;
        stats = {};
    }

    void SetRequest(const IblBakeRequest& request_) noexcept {
        ++stats.set_request_call_count;
        request = request_;
        ++source_revision;
        baked_revision = 0U;
        result = {};
        stats.source_revision = source_revision;
        stats.last_baked_revision = 0U;
        stats.configured = IsValidSource(request.source) ? 1U : 0U;
        stats.baked = 0U;
    }

    void ClearRequest() noexcept {
        ++stats.clear_request_call_count;
        request = {};
        result = {};
        ++source_revision;
        baked_revision = 0U;
        stats.source_revision = source_revision;
        stats.last_baked_revision = 0U;
        stats.configured = 0U;
        stats.baked = 0U;
    }

    void InvalidateBake() noexcept {
        ++stats.invalidate_call_count;
        baked_revision = 0U;
        result = {};
        stats.last_baked_revision = 0U;
        stats.baked = 0U;
    }

    void PrepareFrame(const IblBakeCoordinatorPrepareView& prepare_view_) {
        ++stats.prepare_count;
        if (!HasRequest()) {
            return;
        }
        if (baked_revision == source_revision) {
            return;
        }

        result = prepare_view_.ibl_bake.BakeEnvironment(MakeIblBakeHostPrepareView(prepare_view_),
                                                        request);
        baked_revision = source_revision;
        ++stats.bake_count;
        stats.baked = 1U;
        stats.last_baked_revision = baked_revision;
    }

    void Record(const FrameRecordContext& record_context_) const noexcept {
        (void)record_context_;
    }

    [[nodiscard]] bool HasRequest() const noexcept {
        return IsValidSource(request.source);
    }

    [[nodiscard]] const IblBakeRequest& Request() const noexcept {
        return request;
    }

    [[nodiscard]] const IblBakeResult& Result() const noexcept {
        return result;
    }

    [[nodiscard]] bool HasBakedResult() const noexcept {
        return baked_revision == source_revision && stats.baked != 0U;
    }

    [[nodiscard]] const IblBakeCoordinatorStats& Stats() const noexcept {
        return stats;
    }

private:
    [[nodiscard]] static bool IsValidImageView(const IblCpuImageView& image_) noexcept {
        return image_.pixels != nullptr && image_.width > 0U && image_.height > 0U;
    }

    [[nodiscard]] static bool IsValidSource(const IblBakeSourceDesc& source_) noexcept {
        if (source_.kind == IblBakeSourceKind::equirectangular) {
            return IsValidImageView(source_.equirect);
        }
        for (const IblCpuImageView& face : source_.cubemap.faces) {
            if (!IsValidImageView(face)) {
                return false;
            }
        }
        return true;
    }

private:
    IblBakeRequest request{};
    IblBakeResult result{};
    std::uint32_t source_revision = 0U;
    std::uint32_t baked_revision = 0U;
    IblBakeCoordinatorStats stats{};
};

} // namespace vr::render

