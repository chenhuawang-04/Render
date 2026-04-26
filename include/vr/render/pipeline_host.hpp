#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/vulkan_context.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vulkan/vulkan.h>

namespace vr::render {

template<typename T>
using PipelineMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct PipelineHostCreateInfo {
    bool enable_pipeline_cache = true;
    const void* initial_pipeline_cache_data = nullptr;
    std::size_t initial_pipeline_cache_size = 0U;
    uint32_t reserve_shader_module_count = 64U;
    uint32_t reserve_pipeline_layout_count = 64U;
    uint32_t reserve_graphics_pipeline_count = 128U;
    uint32_t reserve_compute_pipeline_count = 32U;
    bool fail_on_pipeline_compile_required = false;
    bool early_return_on_pipeline_failure = false;
};

struct ShaderModuleCreateInfo {
    VkShaderModuleCreateFlags flags = 0U;
    const uint32_t* code_words = nullptr;
    std::size_t word_count = 0U;
};

struct PushConstantRangeDesc {
    VkShaderStageFlags stage_flags = 0U;
    uint32_t offset = 0U;
    uint32_t size = 0U;
};

struct PipelineLayoutDesc {
    VkPipelineLayoutCreateFlags flags = 0U;
    PipelineMcVector<VkDescriptorSetLayout> set_layouts{};
    PipelineMcVector<PushConstantRangeDesc> push_constant_ranges{};
};

struct PipelineShaderSpecializationDesc {
    PipelineMcVector<VkSpecializationMapEntry> map_entries{};
    PipelineMcVector<uint8_t> data{};
};

struct PipelineShaderStageDesc {
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
    VkShaderModule module = VK_NULL_HANDLE;
    const char* entry_name = "main";
    VkPipelineShaderStageCreateFlags flags = 0U;
    PipelineShaderSpecializationDesc specialization{};
};

struct GraphicsVertexInputStateDesc {
    PipelineMcVector<VkVertexInputBindingDescription> bindings{};
    PipelineMcVector<VkVertexInputAttributeDescription> attributes{};
};

struct GraphicsInputAssemblyStateDesc {
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool primitive_restart_enable = false;
};

struct GraphicsTessellationStateDesc {
    uint32_t patch_control_points = 0U;
};

struct GraphicsViewportStateDesc {
    uint32_t viewport_count = 1U;
    uint32_t scissor_count = 1U;
    PipelineMcVector<VkViewport> static_viewports{};
    PipelineMcVector<VkRect2D> static_scissors{};
};

struct GraphicsRasterizationStateDesc {
    bool depth_clamp_enable = false;
    bool rasterizer_discard_enable = false;
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool depth_bias_enable = false;
    float depth_bias_constant_factor = 0.0F;
    float depth_bias_clamp = 0.0F;
    float depth_bias_slope_factor = 0.0F;
    float line_width = 1.0F;
};

struct GraphicsMultisampleStateDesc {
    VkSampleCountFlagBits rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    bool sample_shading_enable = false;
    float min_sample_shading = 0.0F;
    PipelineMcVector<VkSampleMask> sample_masks{};
    bool alpha_to_coverage_enable = false;
    bool alpha_to_one_enable = false;
};

struct GraphicsDepthStencilStateDesc {
    bool depth_test_enable = false;
    bool depth_write_enable = false;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
    bool depth_bounds_test_enable = false;
    bool stencil_test_enable = false;
    VkStencilOpState front{};
    VkStencilOpState back{};
    float min_depth_bounds = 0.0F;
    float max_depth_bounds = 1.0F;
};

struct GraphicsColorBlendStateDesc {
    bool logic_op_enable = false;
    VkLogicOp logic_op = VK_LOGIC_OP_COPY;
    PipelineMcVector<VkPipelineColorBlendAttachmentState> attachments{};
    float blend_constants[4] = {0.0F, 0.0F, 0.0F, 0.0F};
};

struct GraphicsDynamicStateDesc {
    PipelineMcVector<VkDynamicState> states{};
};

struct GraphicsRenderingInfoDesc {
    uint32_t view_mask = 0U;
    PipelineMcVector<VkFormat> color_attachment_formats{};
    VkFormat depth_attachment_format = VK_FORMAT_UNDEFINED;
    VkFormat stencil_attachment_format = VK_FORMAT_UNDEFINED;
};

struct GraphicsPipelineDesc {
    VkPipelineCreateFlags flags = 0U;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    PipelineMcVector<PipelineShaderStageDesc> shader_stages{};

    GraphicsVertexInputStateDesc vertex_input{};
    GraphicsInputAssemblyStateDesc input_assembly{};
    GraphicsTessellationStateDesc tessellation{};
    GraphicsViewportStateDesc viewport{};
    GraphicsRasterizationStateDesc rasterization{};
    GraphicsMultisampleStateDesc multisample{};
    GraphicsDepthStencilStateDesc depth_stencil{};
    GraphicsColorBlendStateDesc color_blend{};
    GraphicsDynamicStateDesc dynamic{};

    bool use_dynamic_rendering = true;
    GraphicsRenderingInfoDesc rendering{};
    VkRenderPass render_pass = VK_NULL_HANDLE;
    uint32_t subpass = 0U;

    VkPipeline base_pipeline_handle = VK_NULL_HANDLE;
    int32_t base_pipeline_index = -1;
};

struct ComputePipelineDesc {
    VkPipelineCreateFlags flags = 0U;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    PipelineShaderStageDesc shader_stage{
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = VK_NULL_HANDLE,
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    };

    VkPipeline base_pipeline_handle = VK_NULL_HANDLE;
    int32_t base_pipeline_index = -1;
};

struct PipelineHostStats {
    uint32_t shader_module_count = 0U;
    uint32_t shader_module_cache_hits = 0U;
    uint32_t shader_module_cache_misses = 0U;

    uint32_t pipeline_layout_count = 0U;
    uint32_t pipeline_layout_cache_hits = 0U;
    uint32_t pipeline_layout_cache_misses = 0U;

    uint32_t graphics_pipeline_count = 0U;
    uint32_t graphics_pipeline_cache_hits = 0U;
    uint32_t graphics_pipeline_cache_misses = 0U;

    uint32_t compute_pipeline_count = 0U;
    uint32_t compute_pipeline_cache_hits = 0U;
    uint32_t compute_pipeline_cache_misses = 0U;
};

struct ShaderModuleId {
    uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

struct PipelineLayoutId {
    uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

struct GraphicsPipelineId {
    uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

struct ComputePipelineId {
    uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

class PipelineHost final {
public:
    PipelineHost() = default;
    ~PipelineHost() = default;

    PipelineHost(const PipelineHost&) = delete;
    PipelineHost& operator=(const PipelineHost&) = delete;

    PipelineHost(PipelineHost&&) = delete;
    PipelineHost& operator=(PipelineHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const PipelineHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    [[nodiscard]] ShaderModuleId RegisterShaderModule(VulkanContext& context_,
                                                      const ShaderModuleCreateInfo& create_info_);
    [[nodiscard]] PipelineLayoutId RegisterPipelineLayout(VulkanContext& context_,
                                                          const PipelineLayoutDesc& layout_desc_);
    [[nodiscard]] GraphicsPipelineId RegisterGraphicsPipeline(VulkanContext& context_,
                                                              const GraphicsPipelineDesc& pipeline_desc_);
    [[nodiscard]] ComputePipelineId RegisterComputePipeline(VulkanContext& context_,
                                                            const ComputePipelineDesc& pipeline_desc_);

    void RegisterGraphicsPipelines(VulkanContext& context_,
                                   const GraphicsPipelineDesc* pipeline_descs_,
                                   uint32_t pipeline_count_,
                                   GraphicsPipelineId* out_pipeline_ids_);

    void RegisterComputePipelines(VulkanContext& context_,
                                  const ComputePipelineDesc* pipeline_descs_,
                                  uint32_t pipeline_count_,
                                  ComputePipelineId* out_pipeline_ids_);

    [[nodiscard]] VkShaderModule AcquireShaderModule(VulkanContext& context_,
                                                     const ShaderModuleCreateInfo& create_info_);

    [[nodiscard]] VkPipelineLayout AcquirePipelineLayout(VulkanContext& context_,
                                                         const PipelineLayoutDesc& layout_desc_);

    [[nodiscard]] VkPipeline AcquireGraphicsPipeline(VulkanContext& context_,
                                                     const GraphicsPipelineDesc& pipeline_desc_);

    [[nodiscard]] VkPipeline AcquireComputePipeline(VulkanContext& context_,
                                                    const ComputePipelineDesc& pipeline_desc_);

    [[nodiscard]] VkShaderModule GetShaderModule(ShaderModuleId shader_module_id_) const;
    [[nodiscard]] VkPipelineLayout GetPipelineLayout(PipelineLayoutId pipeline_layout_id_) const;
    [[nodiscard]] VkPipeline GetGraphicsPipeline(GraphicsPipelineId graphics_pipeline_id_) const;
    [[nodiscard]] VkPipeline GetComputePipeline(ComputePipelineId compute_pipeline_id_) const;

    void EnqueueGraphicsPipeline(const GraphicsPipelineDesc& pipeline_desc_);
    void EnqueueComputePipeline(const ComputePipelineDesc& pipeline_desc_);
    [[nodiscard]] uint32_t ProcessPendingCompiles(
        VulkanContext& context_,
        uint32_t max_graphics_count_ = std::numeric_limits<uint32_t>::max(),
        uint32_t max_compute_count_ = std::numeric_limits<uint32_t>::max());
    void ClearPendingCompiles() noexcept;
    [[nodiscard]] uint32_t PendingGraphicsCompileCount() const noexcept;
    [[nodiscard]] uint32_t PendingComputeCompileCount() const noexcept;

    [[nodiscard]] bool LoadPipelineCacheFromFile(VulkanContext& context_,
                                                 const char* file_path_);

    [[nodiscard]] bool SavePipelineCacheToFile(VulkanContext& context_,
                                               const char* file_path_) const;

    [[nodiscard]] VkPipelineCache CacheHandle() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const PipelineHostStats& Stats() const noexcept;

private:
    struct ShaderModuleEntry {
        uint64_t hash = 0U;
        VkShaderModuleCreateFlags flags = 0U;
        PipelineMcVector<uint32_t> code_words{};
        VkShaderModule module = VK_NULL_HANDLE;
    };

    struct PipelineLayoutEntry {
        uint64_t hash = 0U;
        VkPipelineLayoutCreateFlags flags = 0U;
        PipelineMcVector<VkDescriptorSetLayout> set_layouts{};
        PipelineMcVector<VkPushConstantRange> push_constant_ranges{};
        VkPipelineLayout layout = VK_NULL_HANDLE;
    };

    struct ShaderStageEntry {
        VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
        VkShaderModule module = VK_NULL_HANDLE;
        VkPipelineShaderStageCreateFlags flags = 0U;
        std::string entry_name{"main"};
        PipelineMcVector<VkSpecializationMapEntry> specialization_map_entries{};
        PipelineMcVector<uint8_t> specialization_data{};
    };

    struct GraphicsPipelineEntry {
        uint64_t hash = 0U;
        VkPipelineCreateFlags flags = 0U;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        PipelineMcVector<ShaderStageEntry> shader_stages{};

        GraphicsVertexInputStateDesc vertex_input{};
        GraphicsInputAssemblyStateDesc input_assembly{};
        GraphicsTessellationStateDesc tessellation{};
        GraphicsViewportStateDesc viewport{};
        GraphicsRasterizationStateDesc rasterization{};
        GraphicsMultisampleStateDesc multisample{};
        GraphicsDepthStencilStateDesc depth_stencil{};
        GraphicsColorBlendStateDesc color_blend{};
        GraphicsDynamicStateDesc dynamic{};

        bool use_dynamic_rendering = true;
        GraphicsRenderingInfoDesc rendering{};
        VkRenderPass render_pass = VK_NULL_HANDLE;
        uint32_t subpass = 0U;

        VkPipeline base_pipeline_handle = VK_NULL_HANDLE;
        int32_t base_pipeline_index = -1;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    struct ComputePipelineEntry {
        uint64_t hash = 0U;
        VkPipelineCreateFlags flags = 0U;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        ShaderStageEntry shader_stage{};
        VkPipeline base_pipeline_handle = VK_NULL_HANDLE;
        int32_t base_pipeline_index = -1;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    struct HashLookupNode {
        uint64_t hash = 0U;
        uint32_t entry_index = 0U;
    };

    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);
    static uint64_t HashBytes(const void* data_, std::size_t size_) noexcept;
    static void HashCombine(uint64_t& hash_, uint64_t value_) noexcept;

    static bool EqualSpecialization(const ShaderStageEntry& lhs_,
                                    const ShaderStageEntry& rhs_) noexcept;
    static bool EqualShaderStage(const ShaderStageEntry& lhs_,
                                 const ShaderStageEntry& rhs_) noexcept;
    static bool EqualLayoutEntry(const PipelineLayoutEntry& lhs_,
                                 const PipelineLayoutEntry& rhs_) noexcept;
    static bool EqualGraphicsEntry(const GraphicsPipelineEntry& lhs_,
                                   const GraphicsPipelineEntry& rhs_) noexcept;
    static bool EqualComputeEntry(const ComputePipelineEntry& lhs_,
                                  const ComputePipelineEntry& rhs_) noexcept;

    static uint64_t HashShaderModuleEntry(const ShaderModuleEntry& entry_) noexcept;
    static uint64_t HashLayoutEntry(const PipelineLayoutEntry& entry_) noexcept;
    static uint64_t HashShaderStageEntry(const ShaderStageEntry& entry_) noexcept;
    static uint64_t HashGraphicsEntry(const GraphicsPipelineEntry& entry_) noexcept;
    static uint64_t HashComputeEntry(const ComputePipelineEntry& entry_) noexcept;

    static ShaderStageEntry NormalizeShaderStage(const PipelineShaderStageDesc& stage_desc_);

    static PipelineLayoutEntry NormalizeLayout(const PipelineLayoutDesc& layout_desc_);
    static GraphicsPipelineEntry NormalizeGraphics(const GraphicsPipelineDesc& pipeline_desc_);
    static ComputePipelineEntry NormalizeCompute(const ComputePipelineDesc& pipeline_desc_);

    void RegisterGraphicsEntriesBatch(VulkanContext& context_,
                                      const GraphicsPipelineEntry* entries_,
                                      uint32_t entry_count_,
                                      GraphicsPipelineId* out_ids_);
    void RegisterComputeEntriesBatch(VulkanContext& context_,
                                     const ComputePipelineEntry* entries_,
                                     uint32_t entry_count_,
                                     ComputePipelineId* out_ids_);

    [[nodiscard]] GraphicsPipelineId RegisterGraphicsEntry(VulkanContext& context_,
                                                           GraphicsPipelineEntry&& entry_);
    [[nodiscard]] ComputePipelineId RegisterComputeEntry(VulkanContext& context_,
                                                         ComputePipelineEntry&& entry_);

    void CreatePipelineCache(VulkanContext& context_,
                             const void* data_,
                             std::size_t size_);
    void CompactPendingCompilesIfNeeded();

    [[nodiscard]] static uint32_t IdToIndex(uint32_t id_value_);
    [[nodiscard]] static ShaderModuleId MakeShaderModuleId(uint32_t entry_index_);
    [[nodiscard]] static PipelineLayoutId MakePipelineLayoutId(uint32_t entry_index_);
    [[nodiscard]] static GraphicsPipelineId MakeGraphicsPipelineId(uint32_t entry_index_);
    [[nodiscard]] static ComputePipelineId MakeComputePipelineId(uint32_t entry_index_);

private:
    PipelineHostCreateInfo create_info_cache{};
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;

    PipelineMcVector<ShaderModuleEntry> shader_modules{};
    PipelineMcVector<HashLookupNode> shader_module_lookup{};
    PipelineMcVector<PipelineLayoutEntry> pipeline_layouts{};
    PipelineMcVector<HashLookupNode> pipeline_layout_lookup{};
    PipelineMcVector<GraphicsPipelineEntry> graphics_pipelines{};
    PipelineMcVector<HashLookupNode> graphics_pipeline_lookup{};
    PipelineMcVector<ComputePipelineEntry> compute_pipelines{};
    PipelineMcVector<HashLookupNode> compute_pipeline_lookup{};
    PipelineMcVector<GraphicsPipelineEntry> pending_graphics_pipelines{};
    PipelineMcVector<ComputePipelineEntry> pending_compute_pipelines{};
    uint32_t pending_graphics_head = 0U;
    uint32_t pending_compute_head = 0U;

    PipelineHostStats stats{};
    bool initialized = false;
};

} // namespace vr::render
