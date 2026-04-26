#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/text_render_3d_system.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/text/glyph_upload_host.hpp"

#include <array>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render {
struct RuntimePrepareContext;
struct FrameRecordContext;
class UploadHost;
}

namespace vr::text {

template<typename T>
using TextRenderer3DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct TextRenderer3DCreateInfo {
    ecs::TextRuntimeBuildConfig runtime_build{};
    std::uint32_t reserve_component_count = 2048U;
    std::uint32_t reserve_glyph_count = 32768U;
    VkDeviceSize initial_vertex_buffer_bytes = 4U * 1024U * 1024U;
    bool enable_depth = true;
    VkFormat preferred_depth_format = VK_FORMAT_D32_SFLOAT;
    bool clear_depth = true;
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
    float sdf_smooth = 1.0F;
    float bitmap_gamma = 1.0F;
    float bitmap_edge_sharpness = 1.0F;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
};

struct TextRenderer3DStats {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t built_component_count = 0U;
    std::uint32_t glyph_quad_count = 0U;
    std::uint32_t instance_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t billboard_instance_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t skipped_draw_batch_count = 0U;
    std::uint32_t depth_pipeline_bind_count = 0U;
    std::uint32_t reverse_z_draw_call_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
};

class TextRenderer3D final {
public:
    TextRenderer3D() = default;
    ~TextRenderer3D() = default;

    TextRenderer3D(const TextRenderer3D&) = delete;
    TextRenderer3D& operator=(const TextRenderer3D&) = delete;

    TextRenderer3D(TextRenderer3D&&) = delete;
    TextRenderer3D& operator=(TextRenderer3D&&) = delete;

    void Initialize(const TextRenderer3DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetSceneData(ecs::Text<ecs::Dim3>* text_components_,
                      ecs::Transform<ecs::Dim3>* text_transforms_,
                      std::uint32_t component_count_,
                      ecs::Camera<ecs::Dim3>* camera_component_,
                      ecs::Transform<ecs::Dim3>* camera_transform_) noexcept;

    void PrepareFrame(const render::RuntimePrepareContext& prepare_context_);
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
    [[nodiscard]] const TextRenderer3DStats& Stats() const noexcept;

private:
    struct PushConstants final {
        ecs::Matrix4x4 view_projection;
        float sdf_smooth;
        float bitmap_gamma;
        float bitmap_edge_sharpness;
        float reserved0;
    };

    static_assert(sizeof(PushConstants) == 80U);

    enum class DepthPipelineMode : std::uint8_t {
        no_depth = 0U,
        depth_test = 1U,
        depth_test_write = 2U,
        depth_test_reverse_z = 3U,
        depth_test_write_reverse_z = 4U,
        count = 5U
    };

    struct PerFrameState final {
        resource::BufferResource vertex_buffer{};
        VkDeviceSize vertex_buffer_capacity_bytes = 0U;
        std::uint32_t instance_count = 0U;
        std::uint64_t uploaded_revision = 0U;
        TextRenderer3DMcVector<VkDescriptorSet> page_sets{};
        TextRenderer3DMcVector<std::uint32_t> page_set_epochs{};
        std::uint32_t page_set_epoch = 1U;
        TextRenderer3DMcVector<std::uint32_t> page_touch_epochs{};
        std::uint32_t page_touch_epoch = 1U;
    };

    struct RetiredDepthImage final {
        resource::ImageResource resource{};
        std::uint64_t retire_value = 0U;
    };

    [[nodiscard]] static VkDeviceSize NextPow2(VkDeviceSize value_) noexcept;
    [[nodiscard]] static bool AnyTextComponentDirty(const ecs::Text<ecs::Dim3>* components_,
                                                    std::uint32_t component_count_) noexcept;
    [[nodiscard]] static bool IsDepthFormatSupported(VulkanContext& context_,
                                                     VkFormat format_) noexcept;
    [[nodiscard]] static bool DepthFormatHasStencil(VkFormat format_) noexcept;
    [[nodiscard]] static VkImageAspectFlags DepthImageAspectMask(VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_,
                                                     VkFormat preferred_format_);
    [[nodiscard]] static std::size_t PipelineModeIndex(DepthPipelineMode mode_) noexcept;
    [[nodiscard]] static std::uint64_t ComputeTransformRevisionSignature(
        const ecs::Transform<ecs::Dim3>* transforms_,
        std::uint32_t component_count_) noexcept;
    [[nodiscard]] static DepthPipelineMode ResolveDepthPipelineMode(const ecs::Text3DDrawBatch& batch_,
                                                                    bool use_depth_,
                                                                    bool reverse_z_) noexcept;

    void ResetPerFrameDrawState(std::uint32_t frame_index_,
                                std::uint32_t atlas_page_count_);
    void EnsureGpuResourcesForFrame(VulkanContext& context_,
                                    const render::RuntimePrepareContext& prepare_context_,
                                    std::uint32_t frame_index_,
                                    VkDeviceSize required_bytes_);
    void PreparePageDescriptorSetsForFrame(std::uint32_t frame_index_);

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::DescriptorHost& descriptor_host_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_,
                               VkFormat depth_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsureGraphicsPipelineForMode(VulkanContext& context_,
                                                                            render::PipelineHost& pipeline_host_,
                                                                            VkFormat color_format_,
                                                                            VkFormat depth_format_,
                                                                            DepthPipelineMode mode_);
    void DestroyDepthResources(VulkanContext& context_);
    void DestroyRetiredDepthResources(VulkanContext& context_);
    void RetireDepthResources(std::uint64_t retire_value_);
    void CollectRetiredDepthResources(VulkanContext& context_,
                                      std::uint64_t completed_value_);
    void EnsureDepthResources(VulkanContext& context_,
                              std::uint32_t image_count_,
                              VkExtent2D extent_);

    [[nodiscard]] VkDescriptorSet EnsurePageDescriptorSet(VulkanContext& context_,
                                                          render::DescriptorHost& descriptor_host_,
                                                          std::uint32_t frame_index_,
                                                          std::uint32_t page_index_);

    void RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_,
                                                bool has_previous_content_) const;
    void RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const;
    void RecordDepthTransitionToAttachment(VkCommandBuffer command_buffer_,
                                           const resource::ImageResource& depth_resource_,
                                           bool initialized_) const;

private:
    TextRenderer3DCreateInfo create_info_cache{};
    TextRenderer3DStats stats{};

    ecs::Text<ecs::Dim3>* text_components = nullptr;
    ecs::Transform<ecs::Dim3>* text_transforms = nullptr;
    std::uint32_t component_count = 0U;
    ecs::Camera<ecs::Dim3>* camera_component = nullptr;
    ecs::Transform<ecs::Dim3>* camera_transform = nullptr;

    ecs::TextRender3DScratch render_scratch{};
    TextRenderer3DMcVector<PerFrameState> frame_states{};
    TextRenderer3DMcVector<std::uint8_t> image_initialized{};

    render::DescriptorSetLayoutId descriptor_layout_id{};
    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    std::array<render::GraphicsPipelineId,
               static_cast<std::size_t>(DepthPipelineMode::count)> graphics_pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;

    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    TextRenderer3DMcVector<resource::ImageResource> depth_images{};
    TextRenderer3DMcVector<std::uint8_t> depth_image_initialized{};
    TextRenderer3DMcVector<RetiredDepthImage> retired_depth_images{};

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

    ecs::TextRuntimeBuildStats cached_runtime_stats{};
    ecs::TextRender3DBuildStats cached_render_stats{};
    ecs::Text3DFrameData frame_data_cache{};

    const ecs::Text<ecs::Dim3>* cached_components_ptr = nullptr;
    const ecs::Transform<ecs::Dim3>* cached_transforms_ptr = nullptr;
    const ecs::Camera<ecs::Dim3>* cached_camera_component_ptr = nullptr;
    const ecs::Transform<ecs::Dim3>* cached_camera_transform_ptr = nullptr;
    std::uint32_t cached_component_count = 0U;
    std::uint64_t cached_transform_signature = 0U;
    std::uint32_t cached_camera_world_revision = 0U;

    std::uint64_t runtime_geometry_revision = 1U;
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool runtime_geometry_valid = false;
    bool instance_geometry_valid = false;
    bool contains_billboard_instances = false;
    bool active_camera_reverse_z = false;
    bool initialized = false;
};

} // namespace vr::text
