#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/text_runtime_system.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/text/glyph_upload_host.hpp"

#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render {
struct TextRenderer2DPrepareView;
struct FrameRecordContext;
class UploadHost;
}

namespace vr::text {

template<typename T>
using TextRenderer2DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct TextRenderer2DCreateInfo {
    ecs::TextRuntimeBuildConfig runtime_build{};
    std::uint32_t reserve_component_count = 2048U;
    std::uint32_t reserve_glyph_count = 32768U;
    VkDeviceSize initial_vertex_buffer_bytes = 4U * 1024U * 1024U;
    float depth = 0.0F;
    float sdf_smooth = 1.0F;
    float bitmap_gamma = 1.0F;
    float bitmap_edge_sharpness = 1.0F;
    bool enable_pixel_snap = true;
    float pixel_snap_step = 1.0F;
    bool clear_swapchain = true;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
};

struct TextRenderer2DStats {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t built_component_count = 0U;
    std::uint32_t glyph_quad_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t skipped_draw_batch_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
};

class TextRenderer2D final {
public:
    TextRenderer2D() = default;
    ~TextRenderer2D() = default;

    TextRenderer2D(const TextRenderer2D&) = delete;
    TextRenderer2D& operator=(const TextRenderer2D&) = delete;

    TextRenderer2D(TextRenderer2D&&) = delete;
    TextRenderer2D& operator=(TextRenderer2D&&) = delete;

    void Initialize(const TextRenderer2DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetComponents(ecs::Text<ecs::Dim2>* components_,
                       std::uint32_t component_count_) noexcept;
    void SetOutputTargetConfig(const render::RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;

    void PrepareFrame(const render::TextRenderer2DPrepareView& prepare_view_);
    void Record(const render::FrameRecordContext& record_context_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const TextRenderer2DStats& Stats() const noexcept;

private:
    struct GpuTextInstance final {
        float rect_x0;
        float rect_y0;
        float rect_x1;
        float rect_y1;

        float uv_u0;
        float uv_v0;
        float uv_u1;
        float uv_v1;

        std::uint32_t color_rgba8;
        std::uint32_t outline_color_rgba8;
        std::uint32_t params;
    };

    struct PushConstants final {
        float inv_viewport_x;
        float inv_viewport_y;
        float depth;
        float sdf_smooth;
        float bitmap_gamma;
        float bitmap_edge_sharpness;
    };

    static_assert(ecs::PurePodComponent<GpuTextInstance>);
    static_assert(sizeof(PushConstants) == 24U);

    struct PerFrameState final {
        resource::BufferResource vertex_buffer{};
        VkDeviceSize vertex_buffer_capacity_bytes = 0U;
        std::uint32_t instance_count = 0U;
        std::uint64_t uploaded_revision = 0U;
        TextRenderer2DMcVector<VkDescriptorSet> page_sets{};
        TextRenderer2DMcVector<std::uint32_t> page_set_epochs{};
        std::uint32_t page_set_epoch = 1U;
        TextRenderer2DMcVector<std::uint32_t> page_touch_epochs{};
        std::uint32_t page_touch_epoch = 1U;
    };

    [[nodiscard]] static std::uint32_t PackRgba8(const ecs::Rgba8& color_) noexcept;
    [[nodiscard]] static std::uint32_t PackParams(const ecs::TextGlyphQuad& quad_) noexcept;
    [[nodiscard]] static VkDeviceSize NextPow2(VkDeviceSize value_) noexcept;
    [[nodiscard]] static float QuantizeToStep(float value_, float step_) noexcept;
    [[nodiscard]] static bool AnyComponentDirty(const ecs::Text<ecs::Dim2>* components_,
                                                std::uint32_t component_count_) noexcept;

    void ResetPerFrameDrawState(std::uint32_t frame_index_,
                                std::uint32_t atlas_page_count_);
    void BuildGpuInstancesFromScratch();
    void EnsureGpuResourcesForFrame(VulkanContext& context_,
                                    const render::TextRenderer2DPrepareView& prepare_view_,
                                    std::uint32_t frame_index_,
                                    VkDeviceSize required_bytes_);
    void PreparePageDescriptorSetsForFrame(std::uint32_t frame_index_);

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::DescriptorHost& descriptor_host_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_);

    [[nodiscard]] VkDescriptorSet EnsurePageDescriptorSet(VulkanContext& context_,
                                                          render::DescriptorHost& descriptor_host_,
                                                          std::uint32_t frame_index_,
                                                          std::uint32_t page_index_);

private:
    TextRenderer2DCreateInfo create_info_cache{};
    TextRenderer2DStats stats{};

    ecs::Text<ecs::Dim2>* components = nullptr;
    std::uint32_t component_count = 0U;

    ecs::TextRuntimeScratch<ecs::Dim2> runtime_scratch{};
    TextRenderer2DMcVector<GpuTextInstance> gpu_instances{};
    TextRenderer2DMcVector<PerFrameState> frame_states{};
    TextRenderer2DMcVector<std::uint8_t> image_initialized{};
    render::RenderTargetColorOutputConfig output_target_config{};

    render::DescriptorSetLayoutId descriptor_layout_id{};
    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    render::GraphicsPipelineId graphics_pipeline_id{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;

    render::DescriptorMcVector<render::DescriptorImageWrite> descriptor_image_write_scratch{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};

    VulkanContext* context = nullptr;
    render::UploadHost* upload_host = nullptr;
    render::DescriptorHost* descriptor_host = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    FreeTypeHost* freetype_host = nullptr;
    GlyphAtlasHost* glyph_atlas_host = nullptr;
    GlyphUploadHost* glyph_upload_host = nullptr;

    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    ecs::TextRuntimeBuildStats cached_build_stats{};
    const ecs::Text<ecs::Dim2>* cached_components_ptr = nullptr;
    std::uint32_t cached_component_count = 0U;
    std::uint64_t runtime_geometry_revision = 1U;
    bool runtime_geometry_valid = false;
    bool initialized = false;
};

} // namespace vr::text
