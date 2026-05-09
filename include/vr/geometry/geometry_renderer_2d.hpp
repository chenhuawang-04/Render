#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/geometry_runtime_system.hpp"
#include "vr/geometry/geometry_upload_host.hpp"
#include "vr/render/appearance_prepare_bridge.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_pass.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render {
struct GeometryRenderer2DPrepareView;
struct FrameRecordContext;
}

namespace vr::geometry {

template<typename T>
using GeometryRenderer2DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct GeometryRenderer2DCreateInfo {
    ecs::Geometry2DRuntimeBuildConfig runtime_build{};
    std::uint32_t reserve_component_count = 4096U;
    std::uint32_t reserve_primitive_count = 16384U;
    bool input_positions_pixel_space = false;
    bool pixel_space_origin_top_left = true;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
};

struct GeometryRenderer2DStats {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t appearance_component_count = 0U;
    std::uint32_t appearance_visible_count = 0U;
    std::uint32_t appearance_updated_record_count = 0U;
    std::uint32_t appearance_link_scanned_count = 0U;
    std::uint32_t appearance_link_updated_count = 0U;
    std::uint32_t primitive_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t skipped_batch_count = 0U;
    std::uint32_t uploaded_primitive_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    bool cache_reused = false;
    bool appearance_cache_reused = false;
};

class GeometryRenderer2D final {
public:
    GeometryRenderer2D() = default;
    ~GeometryRenderer2D() = default;

    GeometryRenderer2D(const GeometryRenderer2D&) = delete;
    GeometryRenderer2D& operator=(const GeometryRenderer2D&) = delete;

    GeometryRenderer2D(GeometryRenderer2D&&) = delete;
    GeometryRenderer2D& operator=(GeometryRenderer2D&&) = delete;

    void Initialize(const GeometryRenderer2DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetHost(GeometryUploadHost* upload_host_) noexcept;
    void SetSceneData(ecs::Geometry<ecs::Dim2>* geometry_components_,
                      std::uint32_t component_count_) noexcept;
    void SetAppearanceData(ecs::Appearance<ecs::Dim2>* appearance_components_,
                           std::uint32_t appearance_component_count_) noexcept;
    void SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                std::uint32_t dirty_component_count_) noexcept;
    void SetAppearanceCoordinator(render::AppearanceFrameCoordinator<ecs::Dim2>* appearance_frame_coordinator_) noexcept;
    void SetOutputTargetConfig(const render::RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;

    void PrepareFrame(const render::GeometryRenderer2DPrepareView& prepare_view_);
    void Record(const render::FrameRecordContext& record_context_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GeometryRenderer2DStats& Stats() const noexcept;

private:
    enum class BlendMode : std::uint8_t {
        opaque = 0U,
        alpha = 1U,
        additive = 2U,
        multiply = 3U,
        premultiplied_alpha = 4U,
        screen = 5U,
        count = 6U
    };

    struct PushConstants final {
        float viewport_width;
        float viewport_height;
        float inv_viewport_width_2x;
        float inv_viewport_height_2x;
        std::uint32_t params;
        std::uint32_t reserved0;
        std::uint32_t reserved1;
        std::uint32_t reserved2;
    };

    static_assert(sizeof(PushConstants) == 32U);

    [[nodiscard]] static std::size_t BlendModeIndex(BlendMode mode_) noexcept;
    [[nodiscard]] static BlendMode ResolveBlendModeFromBatchParams(std::uint32_t params_) noexcept;
    void EnsurePipelineObjects(VulkanContext& context_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsurePipelineForBlendMode(
        VulkanContext& context_,
        render::PipelineHost& pipeline_host_,
        VkFormat color_format_,
        BlendMode blend_mode_);
private:
    GeometryRenderer2DCreateInfo create_info_cache{};
    GeometryRenderer2DStats stats{};

    ecs::Geometry<ecs::Dim2>* geometry_components = nullptr;
    std::uint32_t component_count = 0U;
    std::uint32_t appearance_component_count = 0U;

    ecs::Geometry2DRuntimeScratch runtime_scratch{};
    ecs::Geometry2DRuntimeBuildStats runtime_stats{};
    render::AppearancePrepareBridge<ecs::Dim2> appearance_prepare_bridge{};
    ecs::AppearanceRuntimeBuildStats appearance_runtime_stats{};
    ecs::AppearanceLinkStats appearance_link_stats{};

    GeometryUploadHost* geometry_upload_host = nullptr;
    VulkanContext* context = nullptr;
    render::UploadHost* upload_host = nullptr;
    render::PipelineHost* pipeline_host = nullptr;

    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    std::array<render::GraphicsPipelineId, static_cast<std::size_t>(BlendMode::count)> pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;

    GeometryRenderer2DMcVector<std::uint8_t> image_initialized{};
    GeometryUploadRange primitive_range{};
    render::RenderTargetColorOutputConfig output_target_config{};

    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;

    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool initialized = false;
};

} // namespace vr::geometry
