#pragma once

#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_pass.hpp"

#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render {

struct RuntimePrepareContext;
struct FrameRecordContext;
class IblHost;

struct SkyboxRendererCreateInfo final {
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.0F, 0.0F, 0.0F, 1.0F}};
    bool clear_depth = true;
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
};

struct SkyboxRendererStats final {
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t skipped_draw_count = 0U;
    std::uint8_t used_depth_output = 0U;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
};

class SkyboxRenderer final {
public:
    SkyboxRenderer() = default;
    ~SkyboxRenderer() = default;

    SkyboxRenderer(const SkyboxRenderer&) = delete;
    SkyboxRenderer& operator=(const SkyboxRenderer&) = delete;

    SkyboxRenderer(SkyboxRenderer&&) = delete;
    SkyboxRenderer& operator=(SkyboxRenderer&&) = delete;

    void Initialize(const SkyboxRendererCreateInfo& create_info_ = {}) noexcept;
    void Shutdown(VulkanContext& context_) noexcept;

    void SetCameraData(ecs::Camera<ecs::Dim3>* camera_component_,
                       ecs::Transform<ecs::Dim3>* camera_transform_) noexcept;
    void SetOutputTargetConfig(const RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;
    void SetDepthTargetConfig(const RenderTargetDepthOutputConfig& depth_output_target_config_) noexcept;
    void ResetDepthTargetConfig() noexcept;

    void PrepareFrame(const RuntimePrepareContext& prepare_context_);
    void Record(const FrameRecordContext& record_context_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_) noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const SkyboxRendererStats& Stats() const noexcept;

private:
    struct PushConstants final {
        ecs::Float4 camera_right_scale_x{};
        ecs::Float4 camera_up_scale_y{};
        ecs::Float4 camera_forward_reserved{};
    };

    static_assert(sizeof(PushConstants) == 48U);

    void EnsurePipelineObjects(VulkanContext& context_,
                               DescriptorHost& descriptor_host_,
                               PipelineHost& pipeline_host_,
                               VkFormat color_format_,
                               VkFormat depth_format_);
    [[nodiscard]] PushConstants BuildPushConstants() const noexcept;

private:
    SkyboxRendererCreateInfo create_info_cache{};
    SkyboxRendererStats stats{};

    VulkanContext* context = nullptr;
    DescriptorHost* descriptor_host = nullptr;
    PipelineHost* pipeline_host = nullptr;
    IblHost* ibl_host = nullptr;

    ecs::Camera<ecs::Dim3>* camera_component = nullptr;
    ecs::Transform<ecs::Dim3>* camera_transform = nullptr;

    DescriptorSetLayoutId descriptor_layout_id{};
    PipelineLayoutId pipeline_layout_id{};
    ShaderModuleId shader_vertex_id{};
    ShaderModuleId shader_fragment_id{};
    GraphicsPipelineId pipeline_id{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;

    RenderTargetColorOutputConfig output_target_config{};
    RenderTargetDepthOutputConfig depth_output_target_config{};

    bool initialized = false;
};

} // namespace vr::render
