#include "vr/render/pipeline_host.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vr::render {

namespace {

[[nodiscard]] const char* VkResultName(VkResult result_) noexcept {
    switch (result_) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
#ifdef VK_ERROR_OUT_OF_POOL_MEMORY
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
#endif
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        default: return "VK_ERROR_UNKNOWN";
    }
}

template<typename HandleT>
[[nodiscard]] uint64_t HandleBits(HandleT handle_) noexcept {
    if constexpr (std::is_pointer_v<HandleT>) {
        return static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(handle_));
    } else {
        return static_cast<uint64_t>(handle_);
    }
}

[[nodiscard]] bool EqualPushConstantRange(const VkPushConstantRange& lhs_,
                                          const VkPushConstantRange& rhs_) noexcept {
    return lhs_.stageFlags == rhs_.stageFlags &&
           lhs_.offset == rhs_.offset &&
           lhs_.size == rhs_.size;
}

[[nodiscard]] bool EqualVertexBinding(const VkVertexInputBindingDescription& lhs_,
                                      const VkVertexInputBindingDescription& rhs_) noexcept {
    return lhs_.binding == rhs_.binding &&
           lhs_.stride == rhs_.stride &&
           lhs_.inputRate == rhs_.inputRate;
}

[[nodiscard]] bool EqualVertexAttribute(const VkVertexInputAttributeDescription& lhs_,
                                        const VkVertexInputAttributeDescription& rhs_) noexcept {
    return lhs_.location == rhs_.location &&
           lhs_.binding == rhs_.binding &&
           lhs_.format == rhs_.format &&
           lhs_.offset == rhs_.offset;
}

[[nodiscard]] bool EqualViewport(const VkViewport& lhs_,
                                 const VkViewport& rhs_) noexcept {
    return std::bit_cast<uint32_t>(lhs_.x) == std::bit_cast<uint32_t>(rhs_.x) &&
           std::bit_cast<uint32_t>(lhs_.y) == std::bit_cast<uint32_t>(rhs_.y) &&
           std::bit_cast<uint32_t>(lhs_.width) == std::bit_cast<uint32_t>(rhs_.width) &&
           std::bit_cast<uint32_t>(lhs_.height) == std::bit_cast<uint32_t>(rhs_.height) &&
           std::bit_cast<uint32_t>(lhs_.minDepth) == std::bit_cast<uint32_t>(rhs_.minDepth) &&
           std::bit_cast<uint32_t>(lhs_.maxDepth) == std::bit_cast<uint32_t>(rhs_.maxDepth);
}

[[nodiscard]] bool EqualScissor(const VkRect2D& lhs_,
                                const VkRect2D& rhs_) noexcept {
    return lhs_.offset.x == rhs_.offset.x &&
           lhs_.offset.y == rhs_.offset.y &&
           lhs_.extent.width == rhs_.extent.width &&
           lhs_.extent.height == rhs_.extent.height;
}

[[nodiscard]] bool EqualStencilState(const VkStencilOpState& lhs_,
                                     const VkStencilOpState& rhs_) noexcept {
    return lhs_.failOp == rhs_.failOp &&
           lhs_.passOp == rhs_.passOp &&
           lhs_.depthFailOp == rhs_.depthFailOp &&
           lhs_.compareOp == rhs_.compareOp &&
           lhs_.compareMask == rhs_.compareMask &&
           lhs_.writeMask == rhs_.writeMask &&
           lhs_.reference == rhs_.reference;
}

[[nodiscard]] bool EqualColorBlendAttachment(const VkPipelineColorBlendAttachmentState& lhs_,
                                             const VkPipelineColorBlendAttachmentState& rhs_) noexcept {
    return lhs_.blendEnable == rhs_.blendEnable &&
           lhs_.srcColorBlendFactor == rhs_.srcColorBlendFactor &&
           lhs_.dstColorBlendFactor == rhs_.dstColorBlendFactor &&
           lhs_.colorBlendOp == rhs_.colorBlendOp &&
           lhs_.srcAlphaBlendFactor == rhs_.srcAlphaBlendFactor &&
           lhs_.dstAlphaBlendFactor == rhs_.dstAlphaBlendFactor &&
           lhs_.alphaBlendOp == rhs_.alphaBlendOp &&
           lhs_.colorWriteMask == rhs_.colorWriteMask;
}

[[nodiscard]] bool EqualSpecializationMapEntry(const VkSpecializationMapEntry& lhs_,
                                               const VkSpecializationMapEntry& rhs_) noexcept {
    return lhs_.constantID == rhs_.constantID &&
           lhs_.offset == rhs_.offset &&
           lhs_.size == rhs_.size;
}

[[nodiscard]] uint32_t SampleCountToInt(VkSampleCountFlagBits sample_count_) noexcept {
    switch (sample_count_) {
        case VK_SAMPLE_COUNT_1_BIT: return 1U;
        case VK_SAMPLE_COUNT_2_BIT: return 2U;
        case VK_SAMPLE_COUNT_4_BIT: return 4U;
        case VK_SAMPLE_COUNT_8_BIT: return 8U;
        case VK_SAMPLE_COUNT_16_BIT: return 16U;
        case VK_SAMPLE_COUNT_32_BIT: return 32U;
        case VK_SAMPLE_COUNT_64_BIT: return 64U;
        default: return 1U;
    }
}

[[nodiscard]] bool HasDynamicState(const GraphicsDynamicStateDesc& dynamic_state_,
                                   VkDynamicState dynamic_) noexcept {
    for (VkDynamicState state : dynamic_state_.states) {
        if (state == dynamic_) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool HasDynamicViewportState(const GraphicsDynamicStateDesc& dynamic_state_) noexcept {
    if (HasDynamicState(dynamic_state_, VK_DYNAMIC_STATE_VIEWPORT)) {
        return true;
    }
#ifdef VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT
    if (HasDynamicState(dynamic_state_, VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT)) {
        return true;
    }
#endif
    return false;
}

[[nodiscard]] bool HasDynamicScissorState(const GraphicsDynamicStateDesc& dynamic_state_) noexcept {
    if (HasDynamicState(dynamic_state_, VK_DYNAMIC_STATE_SCISSOR)) {
        return true;
    }
#ifdef VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT
    if (HasDynamicState(dynamic_state_, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT)) {
        return true;
    }
#endif
    return false;
}

template<typename LookupVectorT>
[[nodiscard]] uint32_t LowerBoundLookupByHash(const LookupVectorT& lookup_,
                                              uint64_t hash_) noexcept {
    uint32_t first = 0U;
    uint32_t count = static_cast<uint32_t>(lookup_.size());
    while (count > 0U) {
        const uint32_t step = count / 2U;
        const uint32_t it = first + step;
        if (lookup_[it].hash < hash_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

template<typename LookupVectorT>
void InsertLookupNode(LookupVectorT& lookup_, uint64_t hash_, uint32_t entry_index_) {
    typename LookupVectorT::value_type node{};
    node.hash = hash_;
    node.entry_index = entry_index_;

    const uint32_t insert_pos = LowerBoundLookupByHash(lookup_, hash_);
    lookup_.push_back(node);
    for (uint32_t i = static_cast<uint32_t>(lookup_.size() - 1U); i > insert_pos; --i) {
        lookup_[i] = lookup_[i - 1U];
    }
    lookup_[insert_pos] = node;
}

template<typename LookupVectorT, typename EntryT, typename EqualsFnT>
[[nodiscard]] uint32_t FindEntryIndex(const LookupVectorT& lookup_,
                                      const PipelineMcVector<EntryT>& entries_,
                                      uint64_t hash_,
                                      const EntryT& candidate_,
                                      EqualsFnT equals_) noexcept {
    const uint32_t begin = LowerBoundLookupByHash(lookup_, hash_);
    for (uint32_t i = begin; i < lookup_.size(); ++i) {
        const auto& node = lookup_[i];
        if (node.hash != hash_) {
            break;
        }
        if (node.entry_index >= entries_.size()) {
            continue;
        }
        if (equals_(entries_[node.entry_index], candidate_)) {
            return node.entry_index;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

struct PipelineCacheHeaderRaw {
    uint32_t header_length = 0U;
    uint32_t header_version = 0U;
    uint32_t vendor_id = 0U;
    uint32_t device_id = 0U;
    uint8_t uuid[VK_UUID_SIZE]{};
};

[[nodiscard]] bool ValidatePipelineCacheDataAgainstPhysicalDevice(VkPhysicalDevice physical_device_,
                                                                  const void* data_,
                                                                  std::size_t size_) {
    if (physical_device_ == VK_NULL_HANDLE || data_ == nullptr) {
        return false;
    }
    if (size_ < sizeof(PipelineCacheHeaderRaw)) {
        return false;
    }

    PipelineCacheHeaderRaw header{};
    std::memcpy(&header, data_, sizeof(PipelineCacheHeaderRaw));

    if (header.header_length < sizeof(PipelineCacheHeaderRaw) ||
        header.header_length > size_) {
        return false;
    }
    if (header.header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) {
        return false;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device_, &properties);
    if (header.vendor_id != properties.vendorID) {
        return false;
    }
    if (header.device_id != properties.deviceID) {
        return false;
    }
    if (std::memcmp(header.uuid, properties.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
        return false;
    }
    return true;
}

} // namespace

void PipelineHost::Initialize(VulkanContext& context_,
                              const PipelineHostCreateInfo& create_info_) {
    if (!context_.IsDeviceInitialized()) {
        throw std::runtime_error("PipelineHost::Initialize requires initialized Vulkan device");
    }

    Shutdown(context_);

    create_info_cache = create_info_;
    if (create_info_cache.initial_pipeline_cache_data == nullptr) {
        create_info_cache.initial_pipeline_cache_size = 0U;
    }

    shader_modules.reserve(create_info_cache.reserve_shader_module_count);
    shader_module_lookup.reserve(create_info_cache.reserve_shader_module_count);
    pipeline_layouts.reserve(create_info_cache.reserve_pipeline_layout_count);
    pipeline_layout_lookup.reserve(create_info_cache.reserve_pipeline_layout_count);
    graphics_pipelines.reserve(create_info_cache.reserve_graphics_pipeline_count);
    graphics_pipeline_lookup.reserve(create_info_cache.reserve_graphics_pipeline_count);
    compute_pipelines.reserve(create_info_cache.reserve_compute_pipeline_count);
    compute_pipeline_lookup.reserve(create_info_cache.reserve_compute_pipeline_count);
    pending_graphics_pipelines.reserve(create_info_cache.reserve_graphics_pipeline_count);
    pending_compute_pipelines.reserve(create_info_cache.reserve_compute_pipeline_count);

    if (create_info_cache.enable_pipeline_cache) {
        CreatePipelineCache(context_,
                            create_info_cache.initial_pipeline_cache_data,
                            create_info_cache.initial_pipeline_cache_size);
    } else {
        pipeline_cache = VK_NULL_HANDLE;
    }

    stats = {};
    initialized = true;
}

void PipelineHost::Shutdown(VulkanContext& context_) {
    if (!initialized &&
        shader_modules.empty() &&
        shader_module_lookup.empty() &&
        pipeline_layouts.empty() &&
        pipeline_layout_lookup.empty() &&
        graphics_pipelines.empty() &&
        graphics_pipeline_lookup.empty() &&
        compute_pipelines.empty() &&
        compute_pipeline_lookup.empty() &&
        pending_graphics_pipelines.empty() &&
        pending_compute_pipelines.empty() &&
        pipeline_cache == VK_NULL_HANDLE) {
        return;
    }

    const VkDevice device = context_.Device();
    if (device != VK_NULL_HANDLE) {
        for (auto& entry : graphics_pipelines) {
            if (entry.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, entry.pipeline, nullptr);
                entry.pipeline = VK_NULL_HANDLE;
            }
        }
        for (auto& entry : compute_pipelines) {
            if (entry.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, entry.pipeline, nullptr);
                entry.pipeline = VK_NULL_HANDLE;
            }
        }

        for (auto& entry : pipeline_layouts) {
            if (entry.layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, entry.layout, nullptr);
                entry.layout = VK_NULL_HANDLE;
            }
        }

        for (auto& entry : shader_modules) {
            if (entry.module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, entry.module, nullptr);
                entry.module = VK_NULL_HANDLE;
            }
        }

        if (pipeline_cache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(device, pipeline_cache, nullptr);
            pipeline_cache = VK_NULL_HANDLE;
        }
    } else {
        pipeline_cache = VK_NULL_HANDLE;
    }

    shader_modules.clear();
    shader_module_lookup.clear();
    pipeline_layouts.clear();
    pipeline_layout_lookup.clear();
    graphics_pipelines.clear();
    graphics_pipeline_lookup.clear();
    compute_pipelines.clear();
    compute_pipeline_lookup.clear();
    pending_graphics_pipelines.clear();
    pending_compute_pipelines.clear();
    pending_graphics_head = 0U;
    pending_compute_head = 0U;
    stats = {};
    initialized = false;
}

ShaderModuleId PipelineHost::RegisterShaderModule(VulkanContext& context_,
                                                  const ShaderModuleCreateInfo& create_info_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::RegisterShaderModule called before Initialize");
    }
    if (create_info_.code_words == nullptr || create_info_.word_count == 0U) {
        throw std::runtime_error("PipelineHost::RegisterShaderModule requires valid SPIR-V code");
    }

    ShaderModuleEntry candidate{};
    candidate.flags = create_info_.flags;
    candidate.code_words.resize(create_info_.word_count);
    std::memcpy(candidate.code_words.data(),
                create_info_.code_words,
                sizeof(uint32_t) * create_info_.word_count);
    candidate.hash = HashShaderModuleEntry(candidate);

    const uint32_t existing_index = FindEntryIndex(
        shader_module_lookup,
        shader_modules,
        candidate.hash,
        candidate,
        [](const ShaderModuleEntry& lhs_, const ShaderModuleEntry& rhs_) noexcept {
            if (lhs_.flags != rhs_.flags) {
                return false;
            }
            if (lhs_.code_words.size() != rhs_.code_words.size()) {
                return false;
            }
            for (uint32_t i = 0U; i < lhs_.code_words.size(); ++i) {
                if (lhs_.code_words[i] != rhs_.code_words[i]) {
                    return false;
                }
            }
            return true;
        });
    if (existing_index != std::numeric_limits<uint32_t>::max()) {
        ++stats.shader_module_cache_hits;
        return MakeShaderModuleId(existing_index);
    }

    VkShaderModuleCreateInfo vk_create_info{};
    vk_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vk_create_info.flags = candidate.flags;
    vk_create_info.codeSize = candidate.code_words.size() * sizeof(uint32_t);
    vk_create_info.pCode = candidate.code_words.data();

    CheckVk("vkCreateShaderModule",
            vkCreateShaderModule(context_.Device(), &vk_create_info, nullptr, &candidate.module));

    if (shader_modules.size() >= std::numeric_limits<uint32_t>::max()) {
        vkDestroyShaderModule(context_.Device(), candidate.module, nullptr);
        throw std::runtime_error("PipelineHost shader module registry overflow");
    }

    shader_modules.push_back(std::move(candidate));
    const uint32_t new_index = static_cast<uint32_t>(shader_modules.size() - 1U);
    InsertLookupNode(shader_module_lookup, shader_modules[new_index].hash, new_index);
    ++stats.shader_module_cache_misses;
    stats.shader_module_count = static_cast<uint32_t>(shader_modules.size());
    return MakeShaderModuleId(new_index);
}

VkShaderModule PipelineHost::AcquireShaderModule(VulkanContext& context_,
                                                 const ShaderModuleCreateInfo& create_info_) {
    const ShaderModuleId shader_module_id = RegisterShaderModule(context_, create_info_);
    return GetShaderModule(shader_module_id);
}

PipelineLayoutId PipelineHost::RegisterPipelineLayout(VulkanContext& context_,
                                                      const PipelineLayoutDesc& layout_desc_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::RegisterPipelineLayout called before Initialize");
    }

    PipelineLayoutEntry candidate = NormalizeLayout(layout_desc_);
    candidate.hash = HashLayoutEntry(candidate);

    const uint32_t existing_index = FindEntryIndex(
        pipeline_layout_lookup,
        pipeline_layouts,
        candidate.hash,
        candidate,
        [](const PipelineLayoutEntry& lhs_, const PipelineLayoutEntry& rhs_) noexcept {
            return EqualLayoutEntry(lhs_, rhs_);
        });
    if (existing_index != std::numeric_limits<uint32_t>::max()) {
        ++stats.pipeline_layout_cache_hits;
        return MakePipelineLayoutId(existing_index);
    }

    VkPipelineLayoutCreateInfo vk_create_info{};
    vk_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    vk_create_info.flags = candidate.flags;
    vk_create_info.setLayoutCount = static_cast<uint32_t>(candidate.set_layouts.size());
    vk_create_info.pSetLayouts = candidate.set_layouts.empty()
        ? nullptr
        : candidate.set_layouts.data();
    vk_create_info.pushConstantRangeCount = static_cast<uint32_t>(candidate.push_constant_ranges.size());
    vk_create_info.pPushConstantRanges = candidate.push_constant_ranges.empty()
        ? nullptr
        : candidate.push_constant_ranges.data();

    CheckVk("vkCreatePipelineLayout",
            vkCreatePipelineLayout(context_.Device(), &vk_create_info, nullptr, &candidate.layout));

    if (pipeline_layouts.size() >= std::numeric_limits<uint32_t>::max()) {
        vkDestroyPipelineLayout(context_.Device(), candidate.layout, nullptr);
        throw std::runtime_error("PipelineHost pipeline layout registry overflow");
    }

    pipeline_layouts.push_back(std::move(candidate));
    const uint32_t new_index = static_cast<uint32_t>(pipeline_layouts.size() - 1U);
    InsertLookupNode(pipeline_layout_lookup, pipeline_layouts[new_index].hash, new_index);
    ++stats.pipeline_layout_cache_misses;
    stats.pipeline_layout_count = static_cast<uint32_t>(pipeline_layouts.size());
    return MakePipelineLayoutId(new_index);
}

VkPipelineLayout PipelineHost::AcquirePipelineLayout(VulkanContext& context_,
                                                     const PipelineLayoutDesc& layout_desc_) {
    const PipelineLayoutId pipeline_layout_id = RegisterPipelineLayout(context_, layout_desc_);
    return GetPipelineLayout(pipeline_layout_id);
}

GraphicsPipelineId PipelineHost::RegisterGraphicsPipeline(VulkanContext& context_,
                                                          const GraphicsPipelineDesc& pipeline_desc_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::RegisterGraphicsPipeline called before Initialize");
    }

    GraphicsPipelineEntry candidate = NormalizeGraphics(pipeline_desc_);
    candidate.hash = HashGraphicsEntry(candidate);
    return RegisterGraphicsEntry(context_, std::move(candidate));
}

void PipelineHost::RegisterGraphicsPipelines(VulkanContext& context_,
                                             const GraphicsPipelineDesc* pipeline_descs_,
                                             uint32_t pipeline_count_,
                                             GraphicsPipelineId* out_pipeline_ids_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::RegisterGraphicsPipelines called before Initialize");
    }
    if (pipeline_count_ == 0U) {
        return;
    }
    if (pipeline_descs_ == nullptr) {
        throw std::runtime_error("PipelineHost::RegisterGraphicsPipelines requires valid pipeline_descs");
    }
    if (out_pipeline_ids_ == nullptr) {
        throw std::runtime_error("PipelineHost::RegisterGraphicsPipelines requires valid out_pipeline_ids");
    }

    PipelineMcVector<GraphicsPipelineEntry> normalized_entries{};
    normalized_entries.resize(pipeline_count_);
    for (uint32_t i = 0U; i < pipeline_count_; ++i) {
        normalized_entries[i] = NormalizeGraphics(pipeline_descs_[i]);
        normalized_entries[i].hash = HashGraphicsEntry(normalized_entries[i]);
    }
    RegisterGraphicsEntriesBatch(context_,
                                normalized_entries.data(),
                                pipeline_count_,
                                out_pipeline_ids_);
}

GraphicsPipelineId PipelineHost::RegisterGraphicsEntry(VulkanContext& context_,
                                                       GraphicsPipelineEntry&& entry_) {
    GraphicsPipelineEntry entry_copy = std::move(entry_);
    GraphicsPipelineId out_id{};
    RegisterGraphicsEntriesBatch(context_, &entry_copy, 1U, &out_id);
    return out_id;
}

VkPipeline PipelineHost::AcquireGraphicsPipeline(VulkanContext& context_,
                                                 const GraphicsPipelineDesc& pipeline_desc_) {
    const GraphicsPipelineId graphics_pipeline_id = RegisterGraphicsPipeline(context_, pipeline_desc_);
    return GetGraphicsPipeline(graphics_pipeline_id);
}

ComputePipelineId PipelineHost::RegisterComputePipeline(VulkanContext& context_,
                                                        const ComputePipelineDesc& pipeline_desc_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::RegisterComputePipeline called before Initialize");
    }

    ComputePipelineEntry candidate = NormalizeCompute(pipeline_desc_);
    candidate.hash = HashComputeEntry(candidate);
    return RegisterComputeEntry(context_, std::move(candidate));
}

void PipelineHost::RegisterComputePipelines(VulkanContext& context_,
                                            const ComputePipelineDesc* pipeline_descs_,
                                            uint32_t pipeline_count_,
                                            ComputePipelineId* out_pipeline_ids_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::RegisterComputePipelines called before Initialize");
    }
    if (pipeline_count_ == 0U) {
        return;
    }
    if (pipeline_descs_ == nullptr) {
        throw std::runtime_error("PipelineHost::RegisterComputePipelines requires valid pipeline_descs");
    }
    if (out_pipeline_ids_ == nullptr) {
        throw std::runtime_error("PipelineHost::RegisterComputePipelines requires valid out_pipeline_ids");
    }

    PipelineMcVector<ComputePipelineEntry> normalized_entries{};
    normalized_entries.resize(pipeline_count_);
    for (uint32_t i = 0U; i < pipeline_count_; ++i) {
        normalized_entries[i] = NormalizeCompute(pipeline_descs_[i]);
        normalized_entries[i].hash = HashComputeEntry(normalized_entries[i]);
    }
    RegisterComputeEntriesBatch(context_,
                                normalized_entries.data(),
                                pipeline_count_,
                                out_pipeline_ids_);
}

ComputePipelineId PipelineHost::RegisterComputeEntry(VulkanContext& context_,
                                                     ComputePipelineEntry&& entry_) {
    ComputePipelineEntry entry_copy = std::move(entry_);
    ComputePipelineId out_id{};
    RegisterComputeEntriesBatch(context_, &entry_copy, 1U, &out_id);
    return out_id;
}

void PipelineHost::RegisterGraphicsEntriesBatch(VulkanContext& context_,
                                                const GraphicsPipelineEntry* entries_,
                                                uint32_t entry_count_,
                                                GraphicsPipelineId* out_ids_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::RegisterGraphicsEntriesBatch called before Initialize");
    }
    if (entry_count_ == 0U) {
        return;
    }
    if (entries_ == nullptr) {
        throw std::runtime_error("PipelineHost::RegisterGraphicsEntriesBatch requires valid entries");
    }
    if (out_ids_ == nullptr) {
        throw std::runtime_error("PipelineHost::RegisterGraphicsEntriesBatch requires valid out_ids");
    }

    constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
    const bool request_compile_control_flags =
        create_info_cache.fail_on_pipeline_compile_required ||
        create_info_cache.early_return_on_pipeline_failure;
    if (request_compile_control_flags &&
        context_.EnabledVulkan13Features().pipelineCreationCacheControl != VK_TRUE) {
        throw std::runtime_error(
            "PipelineHostCreateInfo requests pipeline compile-control flags, but pipelineCreationCacheControl is not enabled");
    }

    PipelineMcVector<GraphicsPipelineEntry> unique_entries{};
    unique_entries.reserve(entry_count_);
    PipelineMcVector<HashLookupNode> unique_lookup{};
    unique_lookup.reserve(entry_count_);

    PipelineMcVector<uint32_t> request_to_unique_index{};
    request_to_unique_index.resize(entry_count_);
    for (uint32_t i = 0U; i < entry_count_; ++i) {
        request_to_unique_index[i] = kInvalidIndex;
    }

    for (uint32_t i = 0U; i < entry_count_; ++i) {
        GraphicsPipelineEntry candidate = entries_[i];
        if (candidate.hash == 0U) {
            candidate.hash = HashGraphicsEntry(candidate);
        }

        if (candidate.use_dynamic_rendering &&
            context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE) {
            throw std::runtime_error(
                "Graphics pipeline requests dynamic rendering, but Vulkan13 dynamicRendering feature is not enabled");
        }

        const uint32_t cached_index = FindEntryIndex(
            graphics_pipeline_lookup,
            graphics_pipelines,
            candidate.hash,
            candidate,
            [](const GraphicsPipelineEntry& lhs_, const GraphicsPipelineEntry& rhs_) noexcept {
                return EqualGraphicsEntry(lhs_, rhs_);
            });
        if (cached_index != kInvalidIndex) {
            out_ids_[i] = MakeGraphicsPipelineId(cached_index);
            ++stats.graphics_pipeline_cache_hits;
            continue;
        }

        const uint32_t duplicate_index = FindEntryIndex(
            unique_lookup,
            unique_entries,
            candidate.hash,
            candidate,
            [](const GraphicsPipelineEntry& lhs_, const GraphicsPipelineEntry& rhs_) noexcept {
                return EqualGraphicsEntry(lhs_, rhs_);
            });
        if (duplicate_index != kInvalidIndex) {
            request_to_unique_index[i] = duplicate_index;
            ++stats.graphics_pipeline_cache_hits;
            continue;
        }

        request_to_unique_index[i] = static_cast<uint32_t>(unique_entries.size());
        unique_entries.push_back(std::move(candidate));
        InsertLookupNode(unique_lookup,
                         unique_entries.back().hash,
                         static_cast<uint32_t>(unique_entries.size() - 1U));
    }

    if (unique_entries.empty()) {
        stats.graphics_pipeline_count = static_cast<uint32_t>(graphics_pipelines.size());
        return;
    }

    if (unique_entries.size() > std::numeric_limits<uint32_t>::max() - graphics_pipelines.size()) {
        throw std::runtime_error("PipelineHost graphics pipeline registry overflow");
    }

    struct GraphicsBatchScratch {
        PipelineMcVector<VkPipelineShaderStageCreateInfo> shader_stage_infos{};
        PipelineMcVector<VkSpecializationInfo> specialization_infos{};
        VkPipelineVertexInputStateCreateInfo vertex_input_info{};
        VkPipelineInputAssemblyStateCreateInfo input_assembly_info{};
        VkPipelineTessellationStateCreateInfo tessellation_info{};
        VkPipelineViewportStateCreateInfo viewport_info{};
        VkPipelineRasterizationStateCreateInfo rasterization_info{};
        VkPipelineMultisampleStateCreateInfo multisample_info{};
        VkPipelineDepthStencilStateCreateInfo depth_stencil_info{};
        VkPipelineColorBlendStateCreateInfo color_blend_info{};
        VkPipelineDynamicStateCreateInfo dynamic_state_info{};
        VkPipelineRenderingCreateInfo rendering_info{};
        bool has_tessellation_stage = false;
    };

    PipelineMcVector<GraphicsBatchScratch> scratches{};
    scratches.resize(unique_entries.size());

    PipelineMcVector<VkGraphicsPipelineCreateInfo> create_infos{};
    create_infos.resize(unique_entries.size());

    PipelineMcVector<VkPipeline> created_pipelines{};
    created_pipelines.resize(unique_entries.size());
    for (auto& pipeline : created_pipelines) {
        pipeline = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0U; i < unique_entries.size(); ++i) {
        GraphicsPipelineEntry& candidate = unique_entries[i];
        GraphicsBatchScratch& scratch = scratches[i];

        VkPipelineCreateFlags effective_create_flags = candidate.flags;
#ifdef VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT
        if (create_info_cache.fail_on_pipeline_compile_required) {
            effective_create_flags |= VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
        }
#endif
#ifdef VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT
        if (create_info_cache.early_return_on_pipeline_failure) {
            effective_create_flags |= VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT;
        }
#endif
        (void)request_compile_control_flags;

        scratch.shader_stage_infos.resize(candidate.shader_stages.size());
        scratch.specialization_infos.resize(candidate.shader_stages.size());
        for (uint32_t stage_index = 0U; stage_index < candidate.shader_stages.size(); ++stage_index) {
            const ShaderStageEntry& stage = candidate.shader_stages[stage_index];

            VkPipelineShaderStageCreateInfo stage_info{};
            stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage_info.flags = stage.flags;
            stage_info.stage = stage.stage;
            stage_info.module = stage.module;
            stage_info.pName = stage.entry_name.c_str();
            stage_info.pSpecializationInfo = nullptr;

            if (!stage.specialization_map_entries.empty() ||
                !stage.specialization_data.empty()) {
                VkSpecializationInfo specialization_info{};
                specialization_info.mapEntryCount =
                    static_cast<uint32_t>(stage.specialization_map_entries.size());
                specialization_info.pMapEntries = stage.specialization_map_entries.empty()
                    ? nullptr
                    : stage.specialization_map_entries.data();
                specialization_info.dataSize = stage.specialization_data.size();
                specialization_info.pData = stage.specialization_data.empty()
                    ? nullptr
                    : stage.specialization_data.data();
                scratch.specialization_infos[stage_index] = specialization_info;
                stage_info.pSpecializationInfo = &scratch.specialization_infos[stage_index];
            }

            scratch.shader_stage_infos[stage_index] = stage_info;
        }

        scratch.has_tessellation_stage =
            std::any_of(candidate.shader_stages.begin(),
                        candidate.shader_stages.end(),
                        [](const ShaderStageEntry& stage_) {
                            return stage_.stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
                                   stage_.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                        });
        if (scratch.has_tessellation_stage && candidate.tessellation.patch_control_points == 0U) {
            throw std::runtime_error(
                "Graphics pipeline uses tessellation shader stage but patch_control_points is zero");
        }

        const uint32_t sample_count = SampleCountToInt(candidate.multisample.rasterization_samples);
        const uint32_t required_sample_mask_words = (sample_count + 31U) / 32U;
        if (!candidate.multisample.sample_masks.empty() &&
            candidate.multisample.sample_masks.size() < required_sample_mask_words) {
            throw std::runtime_error(
                "Graphics multisample sample_masks size is smaller than required sample mask words");
        }

        scratch.vertex_input_info = {};
        scratch.vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        scratch.vertex_input_info.vertexBindingDescriptionCount =
            static_cast<uint32_t>(candidate.vertex_input.bindings.size());
        scratch.vertex_input_info.pVertexBindingDescriptions = candidate.vertex_input.bindings.empty()
            ? nullptr
            : candidate.vertex_input.bindings.data();
        scratch.vertex_input_info.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(candidate.vertex_input.attributes.size());
        scratch.vertex_input_info.pVertexAttributeDescriptions = candidate.vertex_input.attributes.empty()
            ? nullptr
            : candidate.vertex_input.attributes.data();

        scratch.input_assembly_info = {};
        scratch.input_assembly_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        scratch.input_assembly_info.topology = candidate.input_assembly.topology;
        scratch.input_assembly_info.primitiveRestartEnable =
            candidate.input_assembly.primitive_restart_enable ? VK_TRUE : VK_FALSE;

        scratch.tessellation_info = {};
        scratch.tessellation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
        scratch.tessellation_info.patchControlPoints = candidate.tessellation.patch_control_points;

        scratch.viewport_info = {};
        scratch.viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        scratch.viewport_info.viewportCount = candidate.viewport.viewport_count;
        scratch.viewport_info.pViewports = candidate.viewport.static_viewports.empty()
            ? nullptr
            : candidate.viewport.static_viewports.data();
        scratch.viewport_info.scissorCount = candidate.viewport.scissor_count;
        scratch.viewport_info.pScissors = candidate.viewport.static_scissors.empty()
            ? nullptr
            : candidate.viewport.static_scissors.data();

        scratch.rasterization_info = {};
        scratch.rasterization_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        scratch.rasterization_info.depthClampEnable =
            candidate.rasterization.depth_clamp_enable ? VK_TRUE : VK_FALSE;
        scratch.rasterization_info.rasterizerDiscardEnable =
            candidate.rasterization.rasterizer_discard_enable ? VK_TRUE : VK_FALSE;
        scratch.rasterization_info.polygonMode = candidate.rasterization.polygon_mode;
        scratch.rasterization_info.cullMode = candidate.rasterization.cull_mode;
        scratch.rasterization_info.frontFace = candidate.rasterization.front_face;
        scratch.rasterization_info.depthBiasEnable =
            candidate.rasterization.depth_bias_enable ? VK_TRUE : VK_FALSE;
        scratch.rasterization_info.depthBiasConstantFactor =
            candidate.rasterization.depth_bias_constant_factor;
        scratch.rasterization_info.depthBiasClamp = candidate.rasterization.depth_bias_clamp;
        scratch.rasterization_info.depthBiasSlopeFactor =
            candidate.rasterization.depth_bias_slope_factor;
        scratch.rasterization_info.lineWidth = candidate.rasterization.line_width;

        scratch.multisample_info = {};
        scratch.multisample_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        scratch.multisample_info.rasterizationSamples = candidate.multisample.rasterization_samples;
        scratch.multisample_info.sampleShadingEnable =
            candidate.multisample.sample_shading_enable ? VK_TRUE : VK_FALSE;
        scratch.multisample_info.minSampleShading = candidate.multisample.min_sample_shading;
        scratch.multisample_info.pSampleMask = candidate.multisample.sample_masks.empty()
            ? nullptr
            : candidate.multisample.sample_masks.data();
        scratch.multisample_info.alphaToCoverageEnable =
            candidate.multisample.alpha_to_coverage_enable ? VK_TRUE : VK_FALSE;
        scratch.multisample_info.alphaToOneEnable =
            candidate.multisample.alpha_to_one_enable ? VK_TRUE : VK_FALSE;

        scratch.depth_stencil_info = {};
        scratch.depth_stencil_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        scratch.depth_stencil_info.depthTestEnable =
            candidate.depth_stencil.depth_test_enable ? VK_TRUE : VK_FALSE;
        scratch.depth_stencil_info.depthWriteEnable =
            candidate.depth_stencil.depth_write_enable ? VK_TRUE : VK_FALSE;
        scratch.depth_stencil_info.depthCompareOp = candidate.depth_stencil.depth_compare_op;
        scratch.depth_stencil_info.depthBoundsTestEnable =
            candidate.depth_stencil.depth_bounds_test_enable ? VK_TRUE : VK_FALSE;
        scratch.depth_stencil_info.stencilTestEnable =
            candidate.depth_stencil.stencil_test_enable ? VK_TRUE : VK_FALSE;
        scratch.depth_stencil_info.front = candidate.depth_stencil.front;
        scratch.depth_stencil_info.back = candidate.depth_stencil.back;
        scratch.depth_stencil_info.minDepthBounds = candidate.depth_stencil.min_depth_bounds;
        scratch.depth_stencil_info.maxDepthBounds = candidate.depth_stencil.max_depth_bounds;

        scratch.color_blend_info = {};
        scratch.color_blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        scratch.color_blend_info.logicOpEnable = candidate.color_blend.logic_op_enable ? VK_TRUE : VK_FALSE;
        scratch.color_blend_info.logicOp = candidate.color_blend.logic_op;
        scratch.color_blend_info.attachmentCount =
            static_cast<uint32_t>(candidate.color_blend.attachments.size());
        scratch.color_blend_info.pAttachments = candidate.color_blend.attachments.empty()
            ? nullptr
            : candidate.color_blend.attachments.data();
        scratch.color_blend_info.blendConstants[0] = candidate.color_blend.blend_constants[0];
        scratch.color_blend_info.blendConstants[1] = candidate.color_blend.blend_constants[1];
        scratch.color_blend_info.blendConstants[2] = candidate.color_blend.blend_constants[2];
        scratch.color_blend_info.blendConstants[3] = candidate.color_blend.blend_constants[3];

        scratch.dynamic_state_info = {};
        scratch.dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        scratch.dynamic_state_info.dynamicStateCount = static_cast<uint32_t>(candidate.dynamic.states.size());
        scratch.dynamic_state_info.pDynamicStates = candidate.dynamic.states.empty()
            ? nullptr
            : candidate.dynamic.states.data();

        scratch.rendering_info = {};
        scratch.rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        scratch.rendering_info.viewMask = candidate.rendering.view_mask;
        scratch.rendering_info.colorAttachmentCount =
            static_cast<uint32_t>(candidate.rendering.color_attachment_formats.size());
        scratch.rendering_info.pColorAttachmentFormats = candidate.rendering.color_attachment_formats.empty()
            ? nullptr
            : candidate.rendering.color_attachment_formats.data();
        scratch.rendering_info.depthAttachmentFormat = candidate.rendering.depth_attachment_format;
        scratch.rendering_info.stencilAttachmentFormat = candidate.rendering.stencil_attachment_format;

        VkGraphicsPipelineCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        create_info.pNext = candidate.use_dynamic_rendering ? &scratch.rendering_info : nullptr;
        create_info.flags = effective_create_flags;
        create_info.stageCount = static_cast<uint32_t>(scratch.shader_stage_infos.size());
        create_info.pStages = scratch.shader_stage_infos.data();
        create_info.pVertexInputState = &scratch.vertex_input_info;
        create_info.pInputAssemblyState = &scratch.input_assembly_info;
        create_info.pTessellationState = scratch.has_tessellation_stage ? &scratch.tessellation_info : nullptr;
        create_info.pViewportState = &scratch.viewport_info;
        create_info.pRasterizationState = &scratch.rasterization_info;
        create_info.pMultisampleState = &scratch.multisample_info;
        create_info.pDepthStencilState = &scratch.depth_stencil_info;
        create_info.pColorBlendState = &scratch.color_blend_info;
        create_info.pDynamicState = candidate.dynamic.states.empty() ? nullptr : &scratch.dynamic_state_info;
        create_info.layout = candidate.layout;
        create_info.renderPass = candidate.use_dynamic_rendering ? VK_NULL_HANDLE : candidate.render_pass;
        create_info.subpass = candidate.use_dynamic_rendering ? 0U : candidate.subpass;
        create_info.basePipelineHandle = candidate.base_pipeline_handle;
        create_info.basePipelineIndex = candidate.base_pipeline_index;
        create_infos[i] = create_info;
    }

    const uint32_t create_count = static_cast<uint32_t>(create_infos.size());
    const VkResult create_result = vkCreateGraphicsPipelines(context_.Device(),
                                                             pipeline_cache,
                                                             create_count,
                                                             create_infos.data(),
                                                             nullptr,
                                                             created_pipelines.data());
    if (create_result != VK_SUCCESS) {
        for (VkPipeline pipeline : created_pipelines) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(context_.Device(), pipeline, nullptr);
            }
        }
        ThrowVk("vkCreateGraphicsPipelines(batch)", create_result);
    }

    PipelineMcVector<GraphicsPipelineId> unique_ids{};
    unique_ids.resize(unique_entries.size());
    for (uint32_t i = 0U; i < unique_entries.size(); ++i) {
        unique_entries[i].pipeline = created_pipelines[i];
        graphics_pipelines.push_back(std::move(unique_entries[i]));
        const uint32_t new_index = static_cast<uint32_t>(graphics_pipelines.size() - 1U);
        InsertLookupNode(graphics_pipeline_lookup, graphics_pipelines[new_index].hash, new_index);
        unique_ids[i] = MakeGraphicsPipelineId(new_index);
        ++stats.graphics_pipeline_cache_misses;
    }

    for (uint32_t i = 0U; i < entry_count_; ++i) {
        const uint32_t unique_index = request_to_unique_index[i];
        if (unique_index != kInvalidIndex) {
            out_ids_[i] = unique_ids[unique_index];
        }
    }

    stats.graphics_pipeline_count = static_cast<uint32_t>(graphics_pipelines.size());
}

void PipelineHost::RegisterComputeEntriesBatch(VulkanContext& context_,
                                               const ComputePipelineEntry* entries_,
                                               uint32_t entry_count_,
                                               ComputePipelineId* out_ids_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::RegisterComputeEntriesBatch called before Initialize");
    }
    if (entry_count_ == 0U) {
        return;
    }
    if (entries_ == nullptr) {
        throw std::runtime_error("PipelineHost::RegisterComputeEntriesBatch requires valid entries");
    }
    if (out_ids_ == nullptr) {
        throw std::runtime_error("PipelineHost::RegisterComputeEntriesBatch requires valid out_ids");
    }

    constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
    const bool request_compile_control_flags =
        create_info_cache.fail_on_pipeline_compile_required ||
        create_info_cache.early_return_on_pipeline_failure;
    if (request_compile_control_flags &&
        context_.EnabledVulkan13Features().pipelineCreationCacheControl != VK_TRUE) {
        throw std::runtime_error(
            "PipelineHostCreateInfo requests pipeline compile-control flags, but pipelineCreationCacheControl is not enabled");
    }

    PipelineMcVector<ComputePipelineEntry> unique_entries{};
    unique_entries.reserve(entry_count_);
    PipelineMcVector<HashLookupNode> unique_lookup{};
    unique_lookup.reserve(entry_count_);

    PipelineMcVector<uint32_t> request_to_unique_index{};
    request_to_unique_index.resize(entry_count_);
    for (uint32_t i = 0U; i < entry_count_; ++i) {
        request_to_unique_index[i] = kInvalidIndex;
    }

    for (uint32_t i = 0U; i < entry_count_; ++i) {
        ComputePipelineEntry candidate = entries_[i];
        if (candidate.hash == 0U) {
            candidate.hash = HashComputeEntry(candidate);
        }

        const uint32_t cached_index = FindEntryIndex(
            compute_pipeline_lookup,
            compute_pipelines,
            candidate.hash,
            candidate,
            [](const ComputePipelineEntry& lhs_, const ComputePipelineEntry& rhs_) noexcept {
                return EqualComputeEntry(lhs_, rhs_);
            });
        if (cached_index != kInvalidIndex) {
            out_ids_[i] = MakeComputePipelineId(cached_index);
            ++stats.compute_pipeline_cache_hits;
            continue;
        }

        const uint32_t duplicate_index = FindEntryIndex(
            unique_lookup,
            unique_entries,
            candidate.hash,
            candidate,
            [](const ComputePipelineEntry& lhs_, const ComputePipelineEntry& rhs_) noexcept {
                return EqualComputeEntry(lhs_, rhs_);
            });
        if (duplicate_index != kInvalidIndex) {
            request_to_unique_index[i] = duplicate_index;
            ++stats.compute_pipeline_cache_hits;
            continue;
        }

        request_to_unique_index[i] = static_cast<uint32_t>(unique_entries.size());
        unique_entries.push_back(std::move(candidate));
        InsertLookupNode(unique_lookup,
                         unique_entries.back().hash,
                         static_cast<uint32_t>(unique_entries.size() - 1U));
    }

    if (unique_entries.empty()) {
        stats.compute_pipeline_count = static_cast<uint32_t>(compute_pipelines.size());
        return;
    }

    if (unique_entries.size() > std::numeric_limits<uint32_t>::max() - compute_pipelines.size()) {
        throw std::runtime_error("PipelineHost compute pipeline registry overflow");
    }

    struct ComputeBatchScratch {
        VkSpecializationInfo specialization_info{};
    };

    PipelineMcVector<ComputeBatchScratch> scratches{};
    scratches.resize(unique_entries.size());

    PipelineMcVector<VkPipelineShaderStageCreateInfo> stage_infos{};
    stage_infos.resize(unique_entries.size());

    PipelineMcVector<VkComputePipelineCreateInfo> create_infos{};
    create_infos.resize(unique_entries.size());

    PipelineMcVector<VkPipeline> created_pipelines{};
    created_pipelines.resize(unique_entries.size());
    for (auto& pipeline : created_pipelines) {
        pipeline = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0U; i < unique_entries.size(); ++i) {
        ComputePipelineEntry& candidate = unique_entries[i];
        ComputeBatchScratch& scratch = scratches[i];

        VkPipelineCreateFlags effective_create_flags = candidate.flags;
#ifdef VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT
        if (create_info_cache.fail_on_pipeline_compile_required) {
            effective_create_flags |= VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
        }
#endif
#ifdef VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT
        if (create_info_cache.early_return_on_pipeline_failure) {
            effective_create_flags |= VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT;
        }
#endif
        (void)request_compile_control_flags;

        const bool has_specialization =
            !candidate.shader_stage.specialization_map_entries.empty() ||
            !candidate.shader_stage.specialization_data.empty();
        if (has_specialization) {
            scratch.specialization_info = {};
            scratch.specialization_info.mapEntryCount =
                static_cast<uint32_t>(candidate.shader_stage.specialization_map_entries.size());
            scratch.specialization_info.pMapEntries = candidate.shader_stage.specialization_map_entries.empty()
                ? nullptr
                : candidate.shader_stage.specialization_map_entries.data();
            scratch.specialization_info.dataSize = candidate.shader_stage.specialization_data.size();
            scratch.specialization_info.pData = candidate.shader_stage.specialization_data.empty()
                ? nullptr
                : candidate.shader_stage.specialization_data.data();
        }

        VkPipelineShaderStageCreateInfo stage_info{};
        stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage_info.flags = candidate.shader_stage.flags;
        stage_info.stage = candidate.shader_stage.stage;
        stage_info.module = candidate.shader_stage.module;
        stage_info.pName = candidate.shader_stage.entry_name.c_str();
        stage_info.pSpecializationInfo = has_specialization ? &scratch.specialization_info : nullptr;
        stage_infos[i] = stage_info;

        VkComputePipelineCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        create_info.flags = effective_create_flags;
        create_info.stage = stage_infos[i];
        create_info.layout = candidate.layout;
        create_info.basePipelineHandle = candidate.base_pipeline_handle;
        create_info.basePipelineIndex = candidate.base_pipeline_index;
        create_infos[i] = create_info;
    }

    const uint32_t create_count = static_cast<uint32_t>(create_infos.size());
    const VkResult create_result = vkCreateComputePipelines(context_.Device(),
                                                            pipeline_cache,
                                                            create_count,
                                                            create_infos.data(),
                                                            nullptr,
                                                            created_pipelines.data());
    if (create_result != VK_SUCCESS) {
        for (VkPipeline pipeline : created_pipelines) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(context_.Device(), pipeline, nullptr);
            }
        }
        ThrowVk("vkCreateComputePipelines(batch)", create_result);
    }

    PipelineMcVector<ComputePipelineId> unique_ids{};
    unique_ids.resize(unique_entries.size());
    for (uint32_t i = 0U; i < unique_entries.size(); ++i) {
        unique_entries[i].pipeline = created_pipelines[i];
        compute_pipelines.push_back(std::move(unique_entries[i]));
        const uint32_t new_index = static_cast<uint32_t>(compute_pipelines.size() - 1U);
        InsertLookupNode(compute_pipeline_lookup, compute_pipelines[new_index].hash, new_index);
        unique_ids[i] = MakeComputePipelineId(new_index);
        ++stats.compute_pipeline_cache_misses;
    }

    for (uint32_t i = 0U; i < entry_count_; ++i) {
        const uint32_t unique_index = request_to_unique_index[i];
        if (unique_index != kInvalidIndex) {
            out_ids_[i] = unique_ids[unique_index];
        }
    }

    stats.compute_pipeline_count = static_cast<uint32_t>(compute_pipelines.size());
}

VkPipeline PipelineHost::AcquireComputePipeline(VulkanContext& context_,
                                                const ComputePipelineDesc& pipeline_desc_) {
    const ComputePipelineId compute_pipeline_id = RegisterComputePipeline(context_, pipeline_desc_);
    return GetComputePipeline(compute_pipeline_id);
}

void PipelineHost::EnqueueGraphicsPipeline(const GraphicsPipelineDesc& pipeline_desc_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::EnqueueGraphicsPipeline called before Initialize");
    }

    GraphicsPipelineEntry candidate = NormalizeGraphics(pipeline_desc_);
    candidate.hash = HashGraphicsEntry(candidate);

    const uint32_t cached_index = FindEntryIndex(
        graphics_pipeline_lookup,
        graphics_pipelines,
        candidate.hash,
        candidate,
        [](const GraphicsPipelineEntry& lhs_, const GraphicsPipelineEntry& rhs_) noexcept {
            return EqualGraphicsEntry(lhs_, rhs_);
        });
    if (cached_index != std::numeric_limits<uint32_t>::max()) {
        return;
    }

    for (uint32_t i = pending_graphics_head; i < pending_graphics_pipelines.size(); ++i) {
        const auto& pending = pending_graphics_pipelines[i];
        if (pending.hash == candidate.hash && EqualGraphicsEntry(pending, candidate)) {
            return;
        }
    }

    pending_graphics_pipelines.push_back(std::move(candidate));
}

void PipelineHost::EnqueueComputePipeline(const ComputePipelineDesc& pipeline_desc_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::EnqueueComputePipeline called before Initialize");
    }

    ComputePipelineEntry candidate = NormalizeCompute(pipeline_desc_);
    candidate.hash = HashComputeEntry(candidate);

    const uint32_t cached_index = FindEntryIndex(
        compute_pipeline_lookup,
        compute_pipelines,
        candidate.hash,
        candidate,
        [](const ComputePipelineEntry& lhs_, const ComputePipelineEntry& rhs_) noexcept {
            return EqualComputeEntry(lhs_, rhs_);
        });
    if (cached_index != std::numeric_limits<uint32_t>::max()) {
        return;
    }

    for (uint32_t i = pending_compute_head; i < pending_compute_pipelines.size(); ++i) {
        const auto& pending = pending_compute_pipelines[i];
        if (pending.hash == candidate.hash && EqualComputeEntry(pending, candidate)) {
            return;
        }
    }

    pending_compute_pipelines.push_back(std::move(candidate));
}

uint32_t PipelineHost::ProcessPendingCompiles(VulkanContext& context_,
                                              uint32_t max_graphics_count_,
                                              uint32_t max_compute_count_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::ProcessPendingCompiles called before Initialize");
    }

    uint32_t processed_count = 0U;

    if (max_graphics_count_ > 0U && pending_graphics_head < pending_graphics_pipelines.size()) {
        const uint32_t remaining_graphics =
            static_cast<uint32_t>(pending_graphics_pipelines.size() - pending_graphics_head);
        const uint32_t batch_graphics_count = std::min(max_graphics_count_, remaining_graphics);

        PipelineMcVector<GraphicsPipelineEntry> batch_entries{};
        batch_entries.resize(batch_graphics_count);
        for (uint32_t i = 0U; i < batch_graphics_count; ++i) {
            batch_entries[i] = pending_graphics_pipelines[pending_graphics_head + i];
        }

        PipelineMcVector<GraphicsPipelineId> batch_ids{};
        batch_ids.resize(batch_graphics_count);
        RegisterGraphicsEntriesBatch(context_,
                                     batch_entries.data(),
                                     batch_graphics_count,
                                     batch_ids.data());
        pending_graphics_head += batch_graphics_count;
        processed_count += batch_graphics_count;
    }

    if (max_compute_count_ > 0U && pending_compute_head < pending_compute_pipelines.size()) {
        const uint32_t remaining_compute =
            static_cast<uint32_t>(pending_compute_pipelines.size() - pending_compute_head);
        const uint32_t batch_compute_count = std::min(max_compute_count_, remaining_compute);

        PipelineMcVector<ComputePipelineEntry> batch_entries{};
        batch_entries.resize(batch_compute_count);
        for (uint32_t i = 0U; i < batch_compute_count; ++i) {
            batch_entries[i] = pending_compute_pipelines[pending_compute_head + i];
        }

        PipelineMcVector<ComputePipelineId> batch_ids{};
        batch_ids.resize(batch_compute_count);
        RegisterComputeEntriesBatch(context_,
                                    batch_entries.data(),
                                    batch_compute_count,
                                    batch_ids.data());
        pending_compute_head += batch_compute_count;
        processed_count += batch_compute_count;
    }

    CompactPendingCompilesIfNeeded();
    return processed_count;
}

void PipelineHost::ClearPendingCompiles() noexcept {
    pending_graphics_pipelines.clear();
    pending_compute_pipelines.clear();
    pending_graphics_head = 0U;
    pending_compute_head = 0U;
}

uint32_t PipelineHost::PendingGraphicsCompileCount() const noexcept {
    if (pending_graphics_head >= pending_graphics_pipelines.size()) {
        return 0U;
    }
    return static_cast<uint32_t>(pending_graphics_pipelines.size() - pending_graphics_head);
}

uint32_t PipelineHost::PendingComputeCompileCount() const noexcept {
    if (pending_compute_head >= pending_compute_pipelines.size()) {
        return 0U;
    }
    return static_cast<uint32_t>(pending_compute_pipelines.size() - pending_compute_head);
}

void PipelineHost::CompactPendingCompilesIfNeeded() {
    auto compact_queue = [](auto& queue_, uint32_t& head_) {
        if (head_ == 0U) {
            return;
        }
        if (head_ >= queue_.size()) {
            queue_.clear();
            head_ = 0U;
            return;
        }
        if (head_ < 256U && head_ * 2U < queue_.size()) {
            return;
        }

        uint32_t write_index = 0U;
        for (uint32_t read_index = head_; read_index < queue_.size(); ++read_index) {
            if (write_index != read_index) {
                queue_[write_index] = std::move(queue_[read_index]);
            }
            ++write_index;
        }
        queue_.resize(write_index);
        head_ = 0U;
    };

    compact_queue(pending_graphics_pipelines, pending_graphics_head);
    compact_queue(pending_compute_pipelines, pending_compute_head);
}

VkShaderModule PipelineHost::GetShaderModule(ShaderModuleId shader_module_id_) const {
    if (!shader_module_id_.IsValid()) {
        throw std::runtime_error("PipelineHost::GetShaderModule received invalid id");
    }
    const uint32_t index = IdToIndex(shader_module_id_.value);
    if (index >= shader_modules.size()) {
        throw std::out_of_range("PipelineHost::GetShaderModule id out of range");
    }
    return shader_modules[index].module;
}

VkPipelineLayout PipelineHost::GetPipelineLayout(PipelineLayoutId pipeline_layout_id_) const {
    if (!pipeline_layout_id_.IsValid()) {
        throw std::runtime_error("PipelineHost::GetPipelineLayout received invalid id");
    }
    const uint32_t index = IdToIndex(pipeline_layout_id_.value);
    if (index >= pipeline_layouts.size()) {
        throw std::out_of_range("PipelineHost::GetPipelineLayout id out of range");
    }
    return pipeline_layouts[index].layout;
}

VkPipeline PipelineHost::GetGraphicsPipeline(GraphicsPipelineId graphics_pipeline_id_) const {
    if (!graphics_pipeline_id_.IsValid()) {
        throw std::runtime_error("PipelineHost::GetGraphicsPipeline received invalid id");
    }
    const uint32_t index = IdToIndex(graphics_pipeline_id_.value);
    if (index >= graphics_pipelines.size()) {
        throw std::out_of_range("PipelineHost::GetGraphicsPipeline id out of range");
    }
    return graphics_pipelines[index].pipeline;
}

VkPipeline PipelineHost::GetComputePipeline(ComputePipelineId compute_pipeline_id_) const {
    if (!compute_pipeline_id_.IsValid()) {
        throw std::runtime_error("PipelineHost::GetComputePipeline received invalid id");
    }
    const uint32_t index = IdToIndex(compute_pipeline_id_.value);
    if (index >= compute_pipelines.size()) {
        throw std::out_of_range("PipelineHost::GetComputePipeline id out of range");
    }
    return compute_pipelines[index].pipeline;
}

bool PipelineHost::LoadPipelineCacheFromFile(VulkanContext& context_,
                                             const char* file_path_) {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::LoadPipelineCacheFromFile called before Initialize");
    }
    if (!create_info_cache.enable_pipeline_cache) {
        return false;
    }
    if (file_path_ == nullptr || file_path_[0] == '\0') {
        return false;
    }

    std::ifstream input(file_path_, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        return false;
    }

    const std::streamsize file_size = input.tellg();
    if (file_size <= 0) {
        return false;
    }
    input.seekg(0, std::ios::beg);

    PipelineMcVector<uint8_t> data{};
    data.resize(static_cast<std::size_t>(file_size));
    if (!input.read(reinterpret_cast<char*>(data.data()), file_size)) {
        return false;
    }

    if (!ValidatePipelineCacheDataAgainstPhysicalDevice(context_.PhysicalDevice(),
                                                        data.data(),
                                                        static_cast<std::size_t>(data.size()))) {
        return false;
    }

    CreatePipelineCache(context_, data.data(), static_cast<std::size_t>(data.size()));
    return true;
}

bool PipelineHost::SavePipelineCacheToFile(VulkanContext& context_,
                                           const char* file_path_) const {
    if (!initialized) {
        throw std::runtime_error("PipelineHost::SavePipelineCacheToFile called before Initialize");
    }
    if (!create_info_cache.enable_pipeline_cache || pipeline_cache == VK_NULL_HANDLE) {
        return false;
    }
    if (file_path_ == nullptr || file_path_[0] == '\0') {
        return false;
    }

    std::size_t data_size = 0U;
    CheckVk("vkGetPipelineCacheData(size)",
            vkGetPipelineCacheData(context_.Device(), pipeline_cache, &data_size, nullptr));
    if (data_size == 0U) {
        return false;
    }

    PipelineMcVector<uint8_t> data{};
    data.resize(data_size);
    CheckVk("vkGetPipelineCacheData(data)",
            vkGetPipelineCacheData(context_.Device(), pipeline_cache, &data_size, data.data()));

    std::ofstream output(file_path_, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data_size));
    return output.good();
}

VkPipelineCache PipelineHost::CacheHandle() const noexcept {
    return pipeline_cache;
}

bool PipelineHost::IsInitialized() const noexcept {
    return initialized;
}

const PipelineHostStats& PipelineHost::Stats() const noexcept {
    return stats;
}

uint32_t PipelineHost::IdToIndex(uint32_t id_value_) {
    if (id_value_ == 0U) {
        throw std::runtime_error("PipelineHost id must be non-zero");
    }
    return id_value_ - 1U;
}

ShaderModuleId PipelineHost::MakeShaderModuleId(uint32_t entry_index_) {
    ShaderModuleId id{};
    id.value = entry_index_ + 1U;
    return id;
}

PipelineLayoutId PipelineHost::MakePipelineLayoutId(uint32_t entry_index_) {
    PipelineLayoutId id{};
    id.value = entry_index_ + 1U;
    return id;
}

GraphicsPipelineId PipelineHost::MakeGraphicsPipelineId(uint32_t entry_index_) {
    GraphicsPipelineId id{};
    id.value = entry_index_ + 1U;
    return id;
}

ComputePipelineId PipelineHost::MakeComputePipelineId(uint32_t entry_index_) {
    ComputePipelineId id{};
    id.value = entry_index_ + 1U;
    return id;
}

void PipelineHost::ThrowVk(const char* stage_, VkResult result_) {
    std::ostringstream oss;
    oss << stage_ << " failed: " << VkResultName(result_) << " (" << static_cast<int>(result_) << ")";
    throw std::runtime_error(oss.str());
}

void PipelineHost::CheckVk(const char* stage_, VkResult result_) {
    if (result_ != VK_SUCCESS) {
        ThrowVk(stage_, result_);
    }
}

uint64_t PipelineHost::HashBytes(const void* data_, std::size_t size_) noexcept {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;

    uint64_t hash = kOffset;
    const auto* bytes = static_cast<const uint8_t*>(data_);
    for (std::size_t i = 0U; i < size_; ++i) {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= kPrime;
    }
    return hash;
}

void PipelineHost::HashCombine(uint64_t& hash_, uint64_t value_) noexcept {
    hash_ ^= value_ + 0x9e3779b97f4a7c15ULL + (hash_ << 6U) + (hash_ >> 2U);
}

bool PipelineHost::EqualSpecialization(const ShaderStageEntry& lhs_,
                                       const ShaderStageEntry& rhs_) noexcept {
    if (lhs_.specialization_map_entries.size() != rhs_.specialization_map_entries.size()) {
        return false;
    }
    if (lhs_.specialization_data.size() != rhs_.specialization_data.size()) {
        return false;
    }

    for (uint32_t i = 0U; i < lhs_.specialization_map_entries.size(); ++i) {
        if (!EqualSpecializationMapEntry(lhs_.specialization_map_entries[i],
                                         rhs_.specialization_map_entries[i])) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < lhs_.specialization_data.size(); ++i) {
        if (lhs_.specialization_data[i] != rhs_.specialization_data[i]) {
            return false;
        }
    }
    return true;
}

bool PipelineHost::EqualShaderStage(const ShaderStageEntry& lhs_,
                                    const ShaderStageEntry& rhs_) noexcept {
    return lhs_.stage == rhs_.stage &&
           lhs_.module == rhs_.module &&
           lhs_.flags == rhs_.flags &&
           lhs_.entry_name == rhs_.entry_name &&
           EqualSpecialization(lhs_, rhs_);
}

bool PipelineHost::EqualLayoutEntry(const PipelineLayoutEntry& lhs_,
                                    const PipelineLayoutEntry& rhs_) noexcept {
    if (lhs_.flags != rhs_.flags) {
        return false;
    }
    if (lhs_.set_layouts.size() != rhs_.set_layouts.size()) {
        return false;
    }
    if (lhs_.push_constant_ranges.size() != rhs_.push_constant_ranges.size()) {
        return false;
    }

    for (uint32_t i = 0U; i < lhs_.set_layouts.size(); ++i) {
        if (lhs_.set_layouts[i] != rhs_.set_layouts[i]) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < lhs_.push_constant_ranges.size(); ++i) {
        if (!EqualPushConstantRange(lhs_.push_constant_ranges[i], rhs_.push_constant_ranges[i])) {
            return false;
        }
    }
    return true;
}

bool PipelineHost::EqualGraphicsEntry(const GraphicsPipelineEntry& lhs_,
                                      const GraphicsPipelineEntry& rhs_) noexcept {
    if (lhs_.flags != rhs_.flags ||
        lhs_.layout != rhs_.layout ||
        lhs_.use_dynamic_rendering != rhs_.use_dynamic_rendering ||
        lhs_.render_pass != rhs_.render_pass ||
        lhs_.subpass != rhs_.subpass ||
        lhs_.base_pipeline_handle != rhs_.base_pipeline_handle ||
        lhs_.base_pipeline_index != rhs_.base_pipeline_index) {
        return false;
    }

    if (lhs_.shader_stages.size() != rhs_.shader_stages.size()) {
        return false;
    }
    for (uint32_t i = 0U; i < lhs_.shader_stages.size(); ++i) {
        if (!EqualShaderStage(lhs_.shader_stages[i], rhs_.shader_stages[i])) {
            return false;
        }
    }

    if (lhs_.vertex_input.bindings.size() != rhs_.vertex_input.bindings.size() ||
        lhs_.vertex_input.attributes.size() != rhs_.vertex_input.attributes.size()) {
        return false;
    }
    for (uint32_t i = 0U; i < lhs_.vertex_input.bindings.size(); ++i) {
        if (!EqualVertexBinding(lhs_.vertex_input.bindings[i], rhs_.vertex_input.bindings[i])) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < lhs_.vertex_input.attributes.size(); ++i) {
        if (!EqualVertexAttribute(lhs_.vertex_input.attributes[i], rhs_.vertex_input.attributes[i])) {
            return false;
        }
    }

    if (lhs_.input_assembly.topology != rhs_.input_assembly.topology ||
        lhs_.input_assembly.primitive_restart_enable != rhs_.input_assembly.primitive_restart_enable) {
        return false;
    }
    if (lhs_.tessellation.patch_control_points != rhs_.tessellation.patch_control_points) {
        return false;
    }
    if (lhs_.viewport.viewport_count != rhs_.viewport.viewport_count ||
        lhs_.viewport.scissor_count != rhs_.viewport.scissor_count ||
        lhs_.viewport.static_viewports.size() != rhs_.viewport.static_viewports.size() ||
        lhs_.viewport.static_scissors.size() != rhs_.viewport.static_scissors.size()) {
        return false;
    }
    for (uint32_t i = 0U; i < lhs_.viewport.static_viewports.size(); ++i) {
        if (!EqualViewport(lhs_.viewport.static_viewports[i], rhs_.viewport.static_viewports[i])) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < lhs_.viewport.static_scissors.size(); ++i) {
        if (!EqualScissor(lhs_.viewport.static_scissors[i], rhs_.viewport.static_scissors[i])) {
            return false;
        }
    }

    if (lhs_.rasterization.depth_clamp_enable != rhs_.rasterization.depth_clamp_enable ||
        lhs_.rasterization.rasterizer_discard_enable != rhs_.rasterization.rasterizer_discard_enable ||
        lhs_.rasterization.polygon_mode != rhs_.rasterization.polygon_mode ||
        lhs_.rasterization.cull_mode != rhs_.rasterization.cull_mode ||
        lhs_.rasterization.front_face != rhs_.rasterization.front_face ||
        lhs_.rasterization.depth_bias_enable != rhs_.rasterization.depth_bias_enable ||
        lhs_.rasterization.depth_bias_constant_factor != rhs_.rasterization.depth_bias_constant_factor ||
        lhs_.rasterization.depth_bias_clamp != rhs_.rasterization.depth_bias_clamp ||
        lhs_.rasterization.depth_bias_slope_factor != rhs_.rasterization.depth_bias_slope_factor ||
        lhs_.rasterization.line_width != rhs_.rasterization.line_width) {
        return false;
    }

    if (lhs_.multisample.rasterization_samples != rhs_.multisample.rasterization_samples ||
        lhs_.multisample.sample_shading_enable != rhs_.multisample.sample_shading_enable ||
        lhs_.multisample.min_sample_shading != rhs_.multisample.min_sample_shading ||
        lhs_.multisample.alpha_to_coverage_enable != rhs_.multisample.alpha_to_coverage_enable ||
        lhs_.multisample.alpha_to_one_enable != rhs_.multisample.alpha_to_one_enable ||
        lhs_.multisample.sample_masks.size() != rhs_.multisample.sample_masks.size()) {
        return false;
    }
    for (uint32_t i = 0U; i < lhs_.multisample.sample_masks.size(); ++i) {
        if (lhs_.multisample.sample_masks[i] != rhs_.multisample.sample_masks[i]) {
            return false;
        }
    }

    if (lhs_.depth_stencil.depth_test_enable != rhs_.depth_stencil.depth_test_enable ||
        lhs_.depth_stencil.depth_write_enable != rhs_.depth_stencil.depth_write_enable ||
        lhs_.depth_stencil.depth_compare_op != rhs_.depth_stencil.depth_compare_op ||
        lhs_.depth_stencil.depth_bounds_test_enable != rhs_.depth_stencil.depth_bounds_test_enable ||
        lhs_.depth_stencil.stencil_test_enable != rhs_.depth_stencil.stencil_test_enable ||
        !EqualStencilState(lhs_.depth_stencil.front, rhs_.depth_stencil.front) ||
        !EqualStencilState(lhs_.depth_stencil.back, rhs_.depth_stencil.back) ||
        lhs_.depth_stencil.min_depth_bounds != rhs_.depth_stencil.min_depth_bounds ||
        lhs_.depth_stencil.max_depth_bounds != rhs_.depth_stencil.max_depth_bounds) {
        return false;
    }

    if (lhs_.color_blend.logic_op_enable != rhs_.color_blend.logic_op_enable ||
        lhs_.color_blend.logic_op != rhs_.color_blend.logic_op ||
        lhs_.color_blend.attachments.size() != rhs_.color_blend.attachments.size()) {
        return false;
    }
    for (uint32_t i = 0U; i < lhs_.color_blend.attachments.size(); ++i) {
        if (!EqualColorBlendAttachment(lhs_.color_blend.attachments[i], rhs_.color_blend.attachments[i])) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < 4U; ++i) {
        if (lhs_.color_blend.blend_constants[i] != rhs_.color_blend.blend_constants[i]) {
            return false;
        }
    }

    if (lhs_.dynamic.states.size() != rhs_.dynamic.states.size()) {
        return false;
    }
    for (uint32_t i = 0U; i < lhs_.dynamic.states.size(); ++i) {
        if (lhs_.dynamic.states[i] != rhs_.dynamic.states[i]) {
            return false;
        }
    }

    if (lhs_.rendering.view_mask != rhs_.rendering.view_mask ||
        lhs_.rendering.color_attachment_formats.size() != rhs_.rendering.color_attachment_formats.size() ||
        lhs_.rendering.depth_attachment_format != rhs_.rendering.depth_attachment_format ||
        lhs_.rendering.stencil_attachment_format != rhs_.rendering.stencil_attachment_format) {
        return false;
    }
    for (uint32_t i = 0U; i < lhs_.rendering.color_attachment_formats.size(); ++i) {
        if (lhs_.rendering.color_attachment_formats[i] != rhs_.rendering.color_attachment_formats[i]) {
            return false;
        }
    }

    return true;
}

bool PipelineHost::EqualComputeEntry(const ComputePipelineEntry& lhs_,
                                     const ComputePipelineEntry& rhs_) noexcept {
    return lhs_.flags == rhs_.flags &&
           lhs_.layout == rhs_.layout &&
           lhs_.base_pipeline_handle == rhs_.base_pipeline_handle &&
           lhs_.base_pipeline_index == rhs_.base_pipeline_index &&
           EqualShaderStage(lhs_.shader_stage, rhs_.shader_stage);
}

uint64_t PipelineHost::HashShaderModuleEntry(const ShaderModuleEntry& entry_) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;
    HashCombine(hash, static_cast<uint64_t>(entry_.flags));
    HashCombine(hash, static_cast<uint64_t>(entry_.code_words.size()));
    if (!entry_.code_words.empty()) {
        HashCombine(hash,
                    HashBytes(entry_.code_words.data(),
                              entry_.code_words.size() * sizeof(uint32_t)));
    }
    return hash;
}

uint64_t PipelineHost::HashLayoutEntry(const PipelineLayoutEntry& entry_) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;
    HashCombine(hash, static_cast<uint64_t>(entry_.flags));
    HashCombine(hash, static_cast<uint64_t>(entry_.set_layouts.size()));
    for (VkDescriptorSetLayout set_layout : entry_.set_layouts) {
        HashCombine(hash, HandleBits(set_layout));
    }
    HashCombine(hash, static_cast<uint64_t>(entry_.push_constant_ranges.size()));
    for (const auto& range : entry_.push_constant_ranges) {
        HashCombine(hash, static_cast<uint64_t>(range.stageFlags));
        HashCombine(hash, static_cast<uint64_t>(range.offset));
        HashCombine(hash, static_cast<uint64_t>(range.size));
    }
    return hash;
}

uint64_t PipelineHost::HashShaderStageEntry(const ShaderStageEntry& entry_) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;
    HashCombine(hash, static_cast<uint64_t>(entry_.stage));
    HashCombine(hash, HandleBits(entry_.module));
    HashCombine(hash, static_cast<uint64_t>(entry_.flags));
    HashCombine(hash, HashBytes(entry_.entry_name.data(), entry_.entry_name.size()));

    HashCombine(hash, static_cast<uint64_t>(entry_.specialization_map_entries.size()));
    for (const auto& map_entry : entry_.specialization_map_entries) {
        HashCombine(hash, static_cast<uint64_t>(map_entry.constantID));
        HashCombine(hash, static_cast<uint64_t>(map_entry.offset));
        HashCombine(hash, static_cast<uint64_t>(map_entry.size));
    }

    HashCombine(hash, static_cast<uint64_t>(entry_.specialization_data.size()));
    if (!entry_.specialization_data.empty()) {
        HashCombine(hash,
                    HashBytes(entry_.specialization_data.data(),
                              entry_.specialization_data.size()));
    }

    return hash;
}

uint64_t PipelineHost::HashGraphicsEntry(const GraphicsPipelineEntry& entry_) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;
    HashCombine(hash, static_cast<uint64_t>(entry_.flags));
    HashCombine(hash, HandleBits(entry_.layout));
    HashCombine(hash, static_cast<uint64_t>(entry_.shader_stages.size()));
    for (const auto& stage : entry_.shader_stages) {
        HashCombine(hash, HashShaderStageEntry(stage));
    }

    HashCombine(hash, static_cast<uint64_t>(entry_.vertex_input.bindings.size()));
    for (const auto& binding : entry_.vertex_input.bindings) {
        HashCombine(hash, static_cast<uint64_t>(binding.binding));
        HashCombine(hash, static_cast<uint64_t>(binding.stride));
        HashCombine(hash, static_cast<uint64_t>(binding.inputRate));
    }
    HashCombine(hash, static_cast<uint64_t>(entry_.vertex_input.attributes.size()));
    for (const auto& attribute : entry_.vertex_input.attributes) {
        HashCombine(hash, static_cast<uint64_t>(attribute.location));
        HashCombine(hash, static_cast<uint64_t>(attribute.binding));
        HashCombine(hash, static_cast<uint64_t>(attribute.format));
        HashCombine(hash, static_cast<uint64_t>(attribute.offset));
    }

    HashCombine(hash, static_cast<uint64_t>(entry_.input_assembly.topology));
    HashCombine(hash, static_cast<uint64_t>(entry_.input_assembly.primitive_restart_enable));
    HashCombine(hash, static_cast<uint64_t>(entry_.tessellation.patch_control_points));
    HashCombine(hash, static_cast<uint64_t>(entry_.viewport.viewport_count));
    HashCombine(hash, static_cast<uint64_t>(entry_.viewport.scissor_count));
    HashCombine(hash, static_cast<uint64_t>(entry_.viewport.static_viewports.size()));
    for (const auto& viewport : entry_.viewport.static_viewports) {
        HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(viewport.x)));
        HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(viewport.y)));
        HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(viewport.width)));
        HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(viewport.height)));
        HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(viewport.minDepth)));
        HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(viewport.maxDepth)));
    }
    HashCombine(hash, static_cast<uint64_t>(entry_.viewport.static_scissors.size()));
    for (const auto& scissor : entry_.viewport.static_scissors) {
        HashCombine(hash, static_cast<uint64_t>(static_cast<int64_t>(scissor.offset.x)));
        HashCombine(hash, static_cast<uint64_t>(static_cast<int64_t>(scissor.offset.y)));
        HashCombine(hash, static_cast<uint64_t>(scissor.extent.width));
        HashCombine(hash, static_cast<uint64_t>(scissor.extent.height));
    }

    HashCombine(hash, static_cast<uint64_t>(entry_.rasterization.depth_clamp_enable));
    HashCombine(hash, static_cast<uint64_t>(entry_.rasterization.rasterizer_discard_enable));
    HashCombine(hash, static_cast<uint64_t>(entry_.rasterization.polygon_mode));
    HashCombine(hash, static_cast<uint64_t>(entry_.rasterization.cull_mode));
    HashCombine(hash, static_cast<uint64_t>(entry_.rasterization.front_face));
    HashCombine(hash, static_cast<uint64_t>(entry_.rasterization.depth_bias_enable));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(entry_.rasterization.depth_bias_constant_factor)));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(entry_.rasterization.depth_bias_clamp)));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(entry_.rasterization.depth_bias_slope_factor)));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(entry_.rasterization.line_width)));

    HashCombine(hash, static_cast<uint64_t>(entry_.multisample.rasterization_samples));
    HashCombine(hash, static_cast<uint64_t>(entry_.multisample.sample_shading_enable));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(entry_.multisample.min_sample_shading)));
    HashCombine(hash, static_cast<uint64_t>(entry_.multisample.alpha_to_coverage_enable));
    HashCombine(hash, static_cast<uint64_t>(entry_.multisample.alpha_to_one_enable));
    HashCombine(hash, static_cast<uint64_t>(entry_.multisample.sample_masks.size()));
    for (VkSampleMask sample_mask : entry_.multisample.sample_masks) {
        HashCombine(hash, static_cast<uint64_t>(sample_mask));
    }

    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.depth_test_enable));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.depth_write_enable));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.depth_compare_op));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.depth_bounds_test_enable));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.stencil_test_enable));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.front.failOp));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.front.passOp));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.front.depthFailOp));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.front.compareOp));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.front.compareMask));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.front.writeMask));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.front.reference));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.back.failOp));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.back.passOp));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.back.depthFailOp));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.back.compareOp));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.back.compareMask));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.back.writeMask));
    HashCombine(hash, static_cast<uint64_t>(entry_.depth_stencil.back.reference));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(entry_.depth_stencil.min_depth_bounds)));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(entry_.depth_stencil.max_depth_bounds)));

    HashCombine(hash, static_cast<uint64_t>(entry_.color_blend.logic_op_enable));
    HashCombine(hash, static_cast<uint64_t>(entry_.color_blend.logic_op));
    HashCombine(hash, static_cast<uint64_t>(entry_.color_blend.attachments.size()));
    for (const auto& attachment : entry_.color_blend.attachments) {
        HashCombine(hash, static_cast<uint64_t>(attachment.blendEnable));
        HashCombine(hash, static_cast<uint64_t>(attachment.srcColorBlendFactor));
        HashCombine(hash, static_cast<uint64_t>(attachment.dstColorBlendFactor));
        HashCombine(hash, static_cast<uint64_t>(attachment.colorBlendOp));
        HashCombine(hash, static_cast<uint64_t>(attachment.srcAlphaBlendFactor));
        HashCombine(hash, static_cast<uint64_t>(attachment.dstAlphaBlendFactor));
        HashCombine(hash, static_cast<uint64_t>(attachment.alphaBlendOp));
        HashCombine(hash, static_cast<uint64_t>(attachment.colorWriteMask));
    }
    for (float blend_constant : entry_.color_blend.blend_constants) {
        HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(blend_constant)));
    }

    HashCombine(hash, static_cast<uint64_t>(entry_.dynamic.states.size()));
    for (VkDynamicState dynamic_state : entry_.dynamic.states) {
        HashCombine(hash, static_cast<uint64_t>(dynamic_state));
    }

    HashCombine(hash, static_cast<uint64_t>(entry_.use_dynamic_rendering));
    HashCombine(hash, static_cast<uint64_t>(entry_.rendering.view_mask));
    HashCombine(hash, static_cast<uint64_t>(entry_.rendering.color_attachment_formats.size()));
    for (VkFormat format : entry_.rendering.color_attachment_formats) {
        HashCombine(hash, static_cast<uint64_t>(format));
    }
    HashCombine(hash, static_cast<uint64_t>(entry_.rendering.depth_attachment_format));
    HashCombine(hash, static_cast<uint64_t>(entry_.rendering.stencil_attachment_format));

    HashCombine(hash, HandleBits(entry_.render_pass));
    HashCombine(hash, static_cast<uint64_t>(entry_.subpass));
    HashCombine(hash, HandleBits(entry_.base_pipeline_handle));
    HashCombine(hash, static_cast<uint64_t>(static_cast<uint32_t>(entry_.base_pipeline_index)));

    return hash;
}

uint64_t PipelineHost::HashComputeEntry(const ComputePipelineEntry& entry_) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;
    HashCombine(hash, static_cast<uint64_t>(entry_.flags));
    HashCombine(hash, HandleBits(entry_.layout));
    HashCombine(hash, HashShaderStageEntry(entry_.shader_stage));
    HashCombine(hash, HandleBits(entry_.base_pipeline_handle));
    HashCombine(hash, static_cast<uint64_t>(static_cast<uint32_t>(entry_.base_pipeline_index)));
    return hash;
}

PipelineHost::ShaderStageEntry PipelineHost::NormalizeShaderStage(const PipelineShaderStageDesc& stage_desc_) {
    if (stage_desc_.module == VK_NULL_HANDLE) {
        throw std::runtime_error("Pipeline shader stage requires valid VkShaderModule");
    }

    ShaderStageEntry stage{};
    stage.stage = stage_desc_.stage;
    stage.module = stage_desc_.module;
    stage.flags = stage_desc_.flags;
    stage.entry_name = (stage_desc_.entry_name != nullptr) ? stage_desc_.entry_name : "main";
    if (stage.entry_name.empty()) {
        stage.entry_name = "main";
    }
    stage.specialization_map_entries = stage_desc_.specialization.map_entries;
    stage.specialization_data = stage_desc_.specialization.data;

    if (!stage.specialization_map_entries.empty() && stage.specialization_data.empty()) {
        throw std::runtime_error(
            "Pipeline shader specialization map_entries provided but specialization data is empty");
    }
    for (const auto& map_entry : stage.specialization_map_entries) {
        if (map_entry.size == 0U) {
            throw std::runtime_error("Pipeline shader specialization map entry has zero size");
        }
        const std::size_t entry_offset = static_cast<std::size_t>(map_entry.offset);
        const std::size_t entry_size = map_entry.size;
        if (entry_offset > stage.specialization_data.size() ||
            entry_size > stage.specialization_data.size() - entry_offset) {
            throw std::runtime_error("Pipeline shader specialization map entry range is out of specialization data bounds");
        }
    }

    return stage;
}

PipelineHost::PipelineLayoutEntry PipelineHost::NormalizeLayout(const PipelineLayoutDesc& layout_desc_) {
    PipelineLayoutEntry entry{};
    entry.flags = layout_desc_.flags;
    entry.set_layouts = layout_desc_.set_layouts;
    for (VkDescriptorSetLayout set_layout : entry.set_layouts) {
        if (set_layout == VK_NULL_HANDLE) {
            throw std::runtime_error("Pipeline layout descriptor contains VK_NULL_HANDLE set layout");
        }
    }

    entry.push_constant_ranges.resize(layout_desc_.push_constant_ranges.size());
    for (uint32_t i = 0U; i < layout_desc_.push_constant_ranges.size(); ++i) {
        const PushConstantRangeDesc& src = layout_desc_.push_constant_ranges[i];
        if (src.size == 0U || src.stage_flags == 0U) {
            throw std::runtime_error("Push constant range requires non-zero size and non-zero stage flags");
        }

        VkPushConstantRange dst{};
        dst.stageFlags = src.stage_flags;
        dst.offset = src.offset;
        dst.size = src.size;
        entry.push_constant_ranges[i] = dst;
    }

    std::sort(entry.push_constant_ranges.begin(),
              entry.push_constant_ranges.end(),
              [](const VkPushConstantRange& lhs_, const VkPushConstantRange& rhs_) {
                  if (lhs_.offset != rhs_.offset) {
                      return lhs_.offset < rhs_.offset;
                  }
                  if (lhs_.size != rhs_.size) {
                      return lhs_.size < rhs_.size;
                  }
                  return lhs_.stageFlags < rhs_.stageFlags;
              });

    return entry;
}

PipelineHost::GraphicsPipelineEntry PipelineHost::NormalizeGraphics(const GraphicsPipelineDesc& pipeline_desc_) {
    if (pipeline_desc_.layout == VK_NULL_HANDLE) {
        throw std::runtime_error("Graphics pipeline requires valid VkPipelineLayout");
    }
    if (pipeline_desc_.shader_stages.empty()) {
        throw std::runtime_error("Graphics pipeline requires at least one shader stage");
    }

    GraphicsPipelineEntry entry{};
    entry.flags = pipeline_desc_.flags;
    entry.layout = pipeline_desc_.layout;
    entry.vertex_input = pipeline_desc_.vertex_input;
    entry.input_assembly = pipeline_desc_.input_assembly;
    entry.tessellation = pipeline_desc_.tessellation;
    entry.viewport = pipeline_desc_.viewport;
    entry.rasterization = pipeline_desc_.rasterization;
    entry.multisample = pipeline_desc_.multisample;
    entry.depth_stencil = pipeline_desc_.depth_stencil;
    entry.color_blend = pipeline_desc_.color_blend;
    entry.dynamic = pipeline_desc_.dynamic;
    entry.use_dynamic_rendering = pipeline_desc_.use_dynamic_rendering;
    entry.rendering = pipeline_desc_.rendering;
    entry.render_pass = pipeline_desc_.render_pass;
    entry.subpass = pipeline_desc_.subpass;
    entry.base_pipeline_handle = pipeline_desc_.base_pipeline_handle;
    entry.base_pipeline_index = pipeline_desc_.base_pipeline_index;

    entry.shader_stages.resize(pipeline_desc_.shader_stages.size());
    for (uint32_t i = 0U; i < pipeline_desc_.shader_stages.size(); ++i) {
        entry.shader_stages[i] = NormalizeShaderStage(pipeline_desc_.shader_stages[i]);
        if (entry.shader_stages[i].stage == VK_SHADER_STAGE_COMPUTE_BIT) {
            throw std::runtime_error("Graphics pipeline cannot include VK_SHADER_STAGE_COMPUTE_BIT");
        }
    }

    std::sort(entry.shader_stages.begin(),
              entry.shader_stages.end(),
              [](const ShaderStageEntry& lhs_, const ShaderStageEntry& rhs_) {
                  return lhs_.stage < rhs_.stage;
              });

    for (uint32_t i = 1U; i < entry.shader_stages.size(); ++i) {
        if (entry.shader_stages[i - 1U].stage == entry.shader_stages[i].stage) {
            throw std::runtime_error("Graphics pipeline contains duplicated shader stage bits");
        }
    }

    std::sort(entry.dynamic.states.begin(), entry.dynamic.states.end());
    if (!entry.dynamic.states.empty()) {
        PipelineMcVector<VkDynamicState> unique_states{};
        unique_states.reserve(entry.dynamic.states.size());

        VkDynamicState previous_state = entry.dynamic.states[0];
        unique_states.push_back(previous_state);
        for (uint32_t i = 1U; i < entry.dynamic.states.size(); ++i) {
            const VkDynamicState current_state = entry.dynamic.states[i];
            if (current_state != previous_state) {
                unique_states.push_back(current_state);
                previous_state = current_state;
            }
        }
        entry.dynamic.states = std::move(unique_states);
    }

    if (entry.viewport.viewport_count == 0U) {
        if (!entry.viewport.static_viewports.empty()) {
            if (entry.viewport.static_viewports.size() > std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("Graphics pipeline static viewport count overflow");
            }
            entry.viewport.viewport_count = static_cast<uint32_t>(entry.viewport.static_viewports.size());
        } else {
            entry.viewport.viewport_count = 1U;
        }
    }
    if (entry.viewport.scissor_count == 0U) {
        if (!entry.viewport.static_scissors.empty()) {
            if (entry.viewport.static_scissors.size() > std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("Graphics pipeline static scissor count overflow");
            }
            entry.viewport.scissor_count = static_cast<uint32_t>(entry.viewport.static_scissors.size());
        } else {
            entry.viewport.scissor_count = 1U;
        }
    }

    if (!entry.viewport.static_viewports.empty() &&
        entry.viewport.static_viewports.size() != entry.viewport.viewport_count) {
        throw std::runtime_error(
            "Graphics pipeline static_viewports size must equal viewport.viewport_count");
    }
    if (!entry.viewport.static_scissors.empty() &&
        entry.viewport.static_scissors.size() != entry.viewport.scissor_count) {
        throw std::runtime_error(
            "Graphics pipeline static_scissors size must equal viewport.scissor_count");
    }

    const bool has_dynamic_viewport_state = HasDynamicViewportState(entry.dynamic);
    const bool has_dynamic_scissor_state = HasDynamicScissorState(entry.dynamic);
    if (entry.viewport.static_viewports.empty() && !has_dynamic_viewport_state) {
        throw std::runtime_error(
            "Graphics pipeline missing viewport state: provide static_viewports or enable dynamic viewport state");
    }
    if (entry.viewport.static_scissors.empty() && !has_dynamic_scissor_state) {
        throw std::runtime_error(
            "Graphics pipeline missing scissor state: provide static_scissors or enable dynamic scissor state");
    }

    if (entry.use_dynamic_rendering) {
        if (entry.color_blend.attachments.empty() &&
            !entry.rendering.color_attachment_formats.empty()) {
            entry.color_blend.attachments.resize(entry.rendering.color_attachment_formats.size());
            for (auto& attachment : entry.color_blend.attachments) {
                attachment.blendEnable = VK_FALSE;
                attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
                attachment.colorBlendOp = VK_BLEND_OP_ADD;
                attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                attachment.alphaBlendOp = VK_BLEND_OP_ADD;
                attachment.colorWriteMask =
                    VK_COLOR_COMPONENT_R_BIT |
                    VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT |
                    VK_COLOR_COMPONENT_A_BIT;
            }
        }

        if (entry.color_blend.attachments.size() != entry.rendering.color_attachment_formats.size()) {
            throw std::runtime_error(
                "Graphics dynamic rendering requires color_blend.attachments size == rendering.color_attachment_formats size");
        }
    } else if (entry.render_pass == VK_NULL_HANDLE) {
        throw std::runtime_error("Graphics pipeline without dynamic rendering requires valid VkRenderPass");
    }

    return entry;
}

PipelineHost::ComputePipelineEntry PipelineHost::NormalizeCompute(const ComputePipelineDesc& pipeline_desc_) {
    if (pipeline_desc_.layout == VK_NULL_HANDLE) {
        throw std::runtime_error("Compute pipeline requires valid VkPipelineLayout");
    }

    ComputePipelineEntry entry{};
    entry.flags = pipeline_desc_.flags;
    entry.layout = pipeline_desc_.layout;
    entry.shader_stage = NormalizeShaderStage(pipeline_desc_.shader_stage);
    entry.base_pipeline_handle = pipeline_desc_.base_pipeline_handle;
    entry.base_pipeline_index = pipeline_desc_.base_pipeline_index;

    if (entry.shader_stage.stage != VK_SHADER_STAGE_COMPUTE_BIT) {
        throw std::runtime_error("Compute pipeline requires shader_stage.stage == VK_SHADER_STAGE_COMPUTE_BIT");
    }

    return entry;
}

void PipelineHost::CreatePipelineCache(VulkanContext& context_,
                                       const void* data_,
                                       std::size_t size_) {
    if (!create_info_cache.enable_pipeline_cache) {
        return;
    }

    const VkDevice device = context_.Device();
    if (device == VK_NULL_HANDLE) {
        throw std::runtime_error("CreatePipelineCache requires initialized Vulkan device");
    }

    if (pipeline_cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(device, pipeline_cache, nullptr);
        pipeline_cache = VK_NULL_HANDLE;
    }

    VkPipelineCacheCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    create_info.initialDataSize = (data_ != nullptr) ? size_ : 0U;
    create_info.pInitialData = (data_ != nullptr) ? data_ : nullptr;

    CheckVk("vkCreatePipelineCache",
            vkCreatePipelineCache(device, &create_info, nullptr, &pipeline_cache));
}

} // namespace vr::render
