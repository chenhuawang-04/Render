#pragma once

#include "vr/asset/texture_host.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/frame_composer_host.hpp"
#include "vr/render/ibl_bake_host.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/render_target_pool.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"
#include "vr/text/glyph_upload_host.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>

namespace vr::runtime {

enum class DiagnosticsLevel : std::uint8_t {
    Off = 0,
    CountersOnly = 1,
    Detailed = 2,
    GpuTiming = 3,
    Capture = 4,
};

struct RuntimeDiagnosticsCreateInfo final {
    DiagnosticsLevel level = DiagnosticsLevel::Off;
};

struct FrameStats final {
    std::uint64_t frame_id = 0U;
    std::uint32_t frame_index = 0U;
    std::uint32_t image_index = 0U;
    bool upload_submitted = false;
    bool upload_cross_queue_wait = false;
};

struct SwapchainStats final {
    bool valid = false;
    std::uint64_t generation = 0U;
    std::uint32_t image_count = 0U;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
};

struct QueueTimelineStats final {
    std::uint64_t graphics_submitted = 0U;
    std::uint64_t graphics_completed = 0U;
    std::uint64_t transfer_submitted = 0U;
    std::uint64_t transfer_completed = 0U;
    std::uint64_t compute_submitted = 0U;
    std::uint64_t compute_completed = 0U;
};

struct CommandStats final {
    std::uint32_t frame_slot_count = 0U;
    std::uint32_t used_primary_count = 0U;
};

struct DescriptorStats final {
    std::uint32_t total_pool_count = 0U;
    std::uint32_t frame_pool_count = 0U;
    std::uint32_t total_allocated_set_count = 0U;
    std::uint32_t frame_allocated_set_count = 0U;
    vr::render::DescriptorValidationStats validation{};
};

struct ParticleRenderStats final {
    bool service_available = false;
};

struct AllocationStats final {
    std::uint64_t upload_capacity_bytes = 0U;
    std::uint32_t upload_staging_page_growth_count = 0U;
    std::uint32_t descriptor_total_pool_count = 0U;
    std::uint32_t render_target_transient_acquired_count = 0U;
};

struct RuntimeFrameDiagnosticsV2 final {
    bool collected = false;
    DiagnosticsLevel level = DiagnosticsLevel::Off;

    FrameStats frame{};
    SwapchainStats swapchain{};
    QueueTimelineStats queues{};
    CommandStats commands{};

    vr::render::UploadFrameStats upload{};
    DescriptorStats descriptor{};
    vr::render::PipelineHostStats pipeline{};
    vr::render::RenderTargetHostStats render_target{};
    vr::render::RenderTargetPoolStats render_target_pool{};

    vr::asset::TextureHostStats texture{};
    vr::render::FrameComposerHostStats frame_composer{};
    vr::render::IblHostStats ibl{};
    vr::render::IblBakeHostStats ibl_bake{};
    vr::text::GlyphAtlasHostStats glyph_atlas{};
    vr::text::GlyphUploadHostStats glyph_upload{};

    vr::particle::ParticleUploadHostStats particle_upload{};
    vr::particle::ParticleSimulationHostStats particle_simulation{};
    ParticleRenderStats particle_render{};

    AllocationStats allocations{};
};

[[nodiscard]] constexpr bool DiagnosticsCollectsFrameData(DiagnosticsLevel level_) noexcept {
    return level_ != DiagnosticsLevel::Off;
}

[[nodiscard]] constexpr bool DiagnosticsCollectsServiceCounters(DiagnosticsLevel level_) noexcept {
    return level_ >= DiagnosticsLevel::CountersOnly;
}

[[nodiscard]] constexpr bool DiagnosticsCollectsDetailedData(DiagnosticsLevel level_) noexcept {
    return level_ >= DiagnosticsLevel::Detailed;
}

} // namespace vr::runtime
