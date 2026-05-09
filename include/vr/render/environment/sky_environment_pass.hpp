#pragma once

#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/render_view.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/scene/background/sky_environment.hpp"

#include <cstdint>

namespace vr::render {

struct SkyEnvironmentPassCreateInfo final {
    VkClearColorValue fallback_clear_color = {{0.02F, 0.02F, 0.03F, 1.0F}};
};

struct SkyEnvironmentPassStats final {
    std::uint32_t prepare_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t skipped_draw_count = 0U;
};

class SkyEnvironmentPass final {
public:
    void Initialize(const SkyEnvironmentPassCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetOutputTargetConfig(const RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;
    void SetDepthTargetConfig(const RenderTargetDepthOutputConfig& depth_output_target_config_) noexcept;
    void ResetDepthTargetConfig() noexcept;

    void PrepareFrame(const SkyEnvironmentPassPrepareView& prepare_view_,
                      const scene::SkyEnvironmentRenderState& state_,
                      scene::SkyEnvironmentGpuHandle gpu_handle_);
    void Record(const FrameRecordContext& record_context_,
                const RenderView3D& view_,
                const scene::SkyEnvironmentRenderState& state_,
                scene::SkyEnvironmentGpuHandle gpu_handle_);

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const SkyEnvironmentPassStats& Stats() const noexcept;

private:
    struct GradientPushBlock final {
        ecs::Float4 color0{};
        ecs::Float4 color1{};
        float exposure = 1.0F;
        std::uint32_t mode = 0U;
        float reserved0 = 0.0F;
        float reserved1 = 0.0F;
    };

    struct EnvironmentImagePushBlock final {
        ecs::Float4 camera_right_scale_x{};
        ecs::Float4 camera_up_scale_y{};
        ecs::Float4 camera_forward_reserved{};
    };

    static_assert(sizeof(EnvironmentImagePushBlock) == 48U);

    struct EquirectPushBlock final {
        ecs::Float4 camera_right_scale_x{};
        ecs::Float4 camera_up_scale_y{};
        ecs::Float4 camera_forward_reserved{};
        ecs::Float4 tint_exposure{};
        ecs::Float4 rotation_sin_cos_intensity{};
    };

    static_assert(sizeof(EquirectPushBlock) == 80U);

    struct AtmospherePushBlock final {
        ecs::Float4 camera_right_scale_x{};
        ecs::Float4 camera_up_scale_y{};
        ecs::Float4 camera_forward_reserved{};
        ecs::Float4 zenith_exposure{};
        ecs::Float4 horizon_intensity{};
        ecs::Float4 ground_density{};
        ecs::Float4 tint_mie{};
        ecs::Float4 sun_direction_rayleigh{};
    };

    static_assert(sizeof(AtmospherePushBlock) == 128U);

    void EnsureGradientPipelineObjects(VulkanContext& context_,
                                       PipelineHost& pipeline_host_,
                                       VkFormat color_format_,
                                       bool depth_tested_ = false,
                                       VkFormat depth_format_ = VK_FORMAT_UNDEFINED);
    void EnsureImageEnvironmentPipelineObjects(VulkanContext& context_,
                                               DescriptorHost& descriptor_host_,
                                               PipelineHost& pipeline_host_,
                                               VkFormat color_format_,
                                               bool depth_tested_ = false,
                                               VkFormat depth_format_ = VK_FORMAT_UNDEFINED);
    void EnsureEquirectPipelineObjects(VulkanContext& context_,
                                       DescriptorHost& descriptor_host_,
                                               PipelineHost& pipeline_host_,
                                       VkFormat color_format_,
                                       bool depth_tested_ = false,
                                       VkFormat depth_format_ = VK_FORMAT_UNDEFINED);
    void EnsureAtmospherePipelineObjects(VulkanContext& context_,
                                         PipelineHost& pipeline_host_,
                                         VkFormat color_format_,
                                         bool depth_tested_ = false,
                                         VkFormat depth_format_ = VK_FORMAT_UNDEFINED);
    [[nodiscard]] static EnvironmentImagePushBlock BuildEnvironmentImagePushBlock(
        const RenderView3D& view_) noexcept;
    [[nodiscard]] static EquirectPushBlock BuildEquirectPushBlock(
        const RenderView3D& view_,
        const scene::SkyEnvironmentRenderState& state_) noexcept;
    [[nodiscard]] static AtmospherePushBlock BuildAtmospherePushBlock(
        const RenderView3D& view_,
        const scene::SkyEnvironmentRenderState& state_) noexcept;

private:
    SkyEnvironmentPassCreateInfo create_info_cache{};
    SkyEnvironmentPassStats stats{};

    VulkanContext* context = nullptr;
    asset::TextureHost* texture_host = nullptr;
    DescriptorHost* descriptor_host = nullptr;
    PipelineHost* pipeline_host = nullptr;
    IblHost* ibl_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;
    VkDescriptorSet active_ibl_descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet active_equirect_descriptor_set = VK_NULL_HANDLE;

    PipelineLayoutId gradient_pipeline_layout_id{};
    ShaderModuleId shader_vertex_id{};
    ShaderModuleId shader_vertex_far_id{};
    ShaderModuleId gradient_shader_fragment_id{};
    GraphicsPipelineId gradient_pipeline_id{};
    VkFormat gradient_pipeline_color_format = VK_FORMAT_UNDEFINED;
    GraphicsPipelineId gradient_depth_tested_pipeline_id{};
    VkFormat gradient_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat gradient_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;

    DescriptorSetLayoutId environment_image_descriptor_layout_id{};
    DescriptorSetLayoutId environment_image_pipeline_layout_descriptor_layout_id{};
    PipelineLayoutId environment_image_pipeline_layout_id{};
    ShaderModuleId environment_image_shader_fragment_id{};
    GraphicsPipelineId environment_image_pipeline_id{};
    VkFormat environment_image_pipeline_color_format = VK_FORMAT_UNDEFINED;
    GraphicsPipelineId environment_image_depth_tested_pipeline_id{};
    VkFormat environment_image_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat environment_image_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;

    DescriptorSetLayoutId equirect_descriptor_layout_id{};
    PipelineLayoutId equirect_pipeline_layout_id{};
    ShaderModuleId equirect_shader_fragment_id{};
    GraphicsPipelineId equirect_pipeline_id{};
    VkFormat equirect_pipeline_color_format = VK_FORMAT_UNDEFINED;
    GraphicsPipelineId equirect_depth_tested_pipeline_id{};
    VkFormat equirect_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat equirect_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;
    resource::SamplerId equirect_sampler_id{};
    DescriptorMcVector<DescriptorImageWrite> descriptor_image_write_scratch{};
    DescriptorMcVector<DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    DescriptorMcVector<DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};

    PipelineLayoutId atmosphere_pipeline_layout_id{};
    ShaderModuleId atmosphere_shader_fragment_id{};
    GraphicsPipelineId atmosphere_pipeline_id{};
    VkFormat atmosphere_pipeline_color_format = VK_FORMAT_UNDEFINED;
    GraphicsPipelineId atmosphere_depth_tested_pipeline_id{};
    VkFormat atmosphere_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat atmosphere_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;

    RenderTargetColorOutputConfig output_target_config{};
    RenderTargetDepthOutputConfig depth_output_target_config{};
    bool has_depth_output_target_config = false;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    bool initialized = false;
};

} // namespace vr::render
