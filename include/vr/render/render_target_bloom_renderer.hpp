#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/render_target_pool.hpp"
#include "vr/resource/sampler_host.hpp"

#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render {

struct RuntimePrepareContext;

template<typename T>
using RenderTargetBloomMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct RenderTargetBloomRendererCreateInfo final {
    bool clear_swapchain = true;
    VkClearColorValue clear_color = {{0.02F, 0.02F, 0.03F, 1.0F}};
    float bloom_threshold = 1.0F;
    float bloom_knee = 0.5F;
    float bloom_intensity = 0.75F;
    float blur_filter_scale = 1.0F;
    float downsample_scale = 0.5F;
    std::uint32_t blur_pass_pair_count = 1U;
    bool enable_reinhard_tonemap = true;
    float exposure = 1.0F;
    float output_gamma = 2.2F;
    bool apply_manual_gamma = false;
    VkFormat intermediate_format = VK_FORMAT_UNDEFINED;
    RenderTargetColorEncoding intermediate_color_encoding = RenderTargetColorEncoding::linear;
};

struct RenderTargetBloomRendererStats final {
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t prefilter_draw_call_count = 0U;
    std::uint32_t blur_draw_call_count = 0U;
    std::uint32_t combine_draw_call_count = 0U;
    std::uint32_t skipped_draw_count = 0U;
    std::uint32_t pass_count = 0U;
    std::uint32_t transient_target_count = 0U;
    std::uint32_t transient_reuse_count = 0U;
    bool reused_descriptor_set = false;
};

class RenderTargetBloomRenderer final {
public:
    void Initialize(const RenderTargetBloomRendererCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetSceneSourceTarget(RenderTargetHandle source_target_,
                              RenderTargetStateKind expected_source_state_ = RenderTargetStateKind::shader_read) noexcept;
    void ClearSceneSourceTarget() noexcept;
    void SetOutputTargetConfig(const RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;

    void PrepareFrame(const RuntimePrepareContext& prepare_context_);
    void Record(const FrameRecordContext& record_context_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const RenderTargetBloomRendererStats& Stats() const noexcept;

private:
    struct PrefilterPushConstants final {
        float threshold = 1.0F;
        float knee = 0.5F;
        float reserved0 = 0.0F;
        float reserved1 = 0.0F;
    };

    struct BlurPushConstants final {
        float texel_offset_x = 0.0F;
        float texel_offset_y = 0.0F;
        float filter_scale = 1.0F;
        float reserved0 = 0.0F;
    };

    struct CombinePushConstants final {
        float exposure = 1.0F;
        float inv_gamma = 1.0F;
        float bloom_intensity = 0.75F;
        std::uint32_t flags = 0U;
    };

    struct SingleSourceDescriptorCacheEntry final {
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        RenderTargetDescriptorKey descriptor_key{};
        std::uint32_t resource_revision = 0U;
        bool valid = false;
    };

    struct DualSourceDescriptorCacheEntry final {
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        RenderTargetDescriptorKey scene_descriptor_key{};
        RenderTargetDescriptorKey bloom_descriptor_key{};
        std::uint32_t scene_resource_revision = 0U;
        std::uint32_t bloom_resource_revision = 0U;
        bool valid = false;
    };

    struct FrameDescriptorCache final {
        SingleSourceDescriptorCacheEntry prefilter{};
        SingleSourceDescriptorCacheEntry blur_horizontal{};
        SingleSourceDescriptorCacheEntry blur_vertical{};
        DualSourceDescriptorCacheEntry combine{};
    };

    static_assert(sizeof(PrefilterPushConstants) == 16U);
    static_assert(sizeof(BlurPushConstants) == 16U);
    static_assert(sizeof(CombinePushConstants) == 16U);

    void EnsurePipelineObjects(VulkanContext& context_,
                               DescriptorHost& descriptor_host_,
                               PipelineHost& pipeline_host_,
                               VkFormat intermediate_format_,
                               VkFormat final_color_format_);
    [[nodiscard]] VkDescriptorSet AcquireSingleSourceDescriptorSet(
        std::uint32_t frame_index_,
        SingleSourceDescriptorCacheEntry& cache_entry_,
        DescriptorSetLayoutId layout_id_,
        RenderTargetHandle source_target_,
        RenderTargetStateKind expected_source_state_);
    [[nodiscard]] VkDescriptorSet AcquireDualSourceDescriptorSet(
        std::uint32_t frame_index_,
        DualSourceDescriptorCacheEntry& cache_entry_,
        DescriptorSetLayoutId layout_id_,
        RenderTargetHandle scene_target_,
        RenderTargetStateKind scene_expected_state_,
        RenderTargetHandle bloom_target_,
        RenderTargetStateKind bloom_expected_state_);
    [[nodiscard]] RenderTargetDesc BuildIntermediateTargetDesc(
        const RenderTargetResolvedView& source_view_) const;
    [[nodiscard]] static VkFormat ResolveIntermediateFormat(VulkanContext& context_,
                                                            VkFormat requested_format_,
                                                            VkFormat source_format_);
    [[nodiscard]] static float SafeInvGamma(float gamma_) noexcept;
    [[nodiscard]] static bool SingleSourceDescriptorKeyEquals(
        const RenderTargetDescriptorKey& lhs_,
        const RenderTargetDescriptorKey& rhs_) noexcept;
    [[nodiscard]] static bool DualSourceDescriptorKeysEqual(
        const DualSourceDescriptorCacheEntry& cache_entry_,
        const RenderTargetDescriptorKey& scene_descriptor_key_,
        const RenderTargetDescriptorKey& bloom_descriptor_key_,
        std::uint32_t scene_resource_revision_,
        std::uint32_t bloom_resource_revision_) noexcept;

private:
    RenderTargetBloomRendererCreateInfo create_info_cache{};
    RenderTargetBloomRendererStats stats{};

    VulkanContext* context = nullptr;
    DescriptorHost* descriptor_host = nullptr;
    PipelineHost* pipeline_host = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    RenderTargetPool* render_target_pool = nullptr;
    resource::SamplerHost* sampler_host = nullptr;

    DescriptorSetLayoutId single_source_layout_id{};
    DescriptorSetLayoutId dual_source_layout_id{};
    PipelineLayoutId single_source_pipeline_layout_id{};
    PipelineLayoutId dual_source_pipeline_layout_id{};
    ShaderModuleId fullscreen_vertex_shader_id{};
    ShaderModuleId prefilter_fragment_shader_id{};
    ShaderModuleId blur_fragment_shader_id{};
    ShaderModuleId combine_fragment_shader_id{};
    GraphicsPipelineId prefilter_pipeline_id{};
    GraphicsPipelineId blur_pipeline_id{};
    GraphicsPipelineId combine_pipeline_id{};
    VkFormat intermediate_pipeline_format = VK_FORMAT_UNDEFINED;
    VkFormat final_pipeline_color_format = VK_FORMAT_UNDEFINED;
    resource::SamplerId linear_clamp_sampler_id{};

    RenderTargetBloomMcVector<FrameDescriptorCache> frame_descriptor_cache{};
    RenderTargetHandle scene_source_target{};
    RenderTargetStateKind scene_source_expected_state = RenderTargetStateKind::shader_read;
    RenderTargetColorOutputConfig output_target_config{};
    RenderTargetHandle bloom_target_a{};
    RenderTargetHandle bloom_target_b{};
    VkFormat active_intermediate_format = VK_FORMAT_UNDEFINED;
    bool frame_ready = false;
    bool initialized = false;
};

} // namespace vr::render
