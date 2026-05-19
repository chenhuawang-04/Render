#ifdef FindResource
#undef FindResource
#endif

#include "vr/render_graph/vulkan_resource_table.hpp"

#include "vr/render_graph/alias_allocator.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef FindResource
#undef FindResource
#endif

namespace vr::render_graph {
namespace {

[[nodiscard]] bool TextureDescsEqual(const TextureDesc& lhs_,
                                     const TextureDesc& rhs_) noexcept {
    return lhs_.dimension == rhs_.dimension &&
           lhs_.format == rhs_.format &&
           lhs_.extent.width == rhs_.extent.width &&
           lhs_.extent.height == rhs_.extent.height &&
           lhs_.extent.depth == rhs_.extent.depth &&
           lhs_.usage == rhs_.usage &&
           lhs_.mip_level_count == rhs_.mip_level_count &&
           lhs_.array_layer_count == rhs_.array_layer_count &&
           lhs_.sample_count == rhs_.sample_count &&
           lhs_.allow_alias == rhs_.allow_alias &&
           lhs_.clear_on_first_use == rhs_.clear_on_first_use &&
           lhs_.prefer_lazy_memory == rhs_.prefer_lazy_memory;
}

[[nodiscard]] bool BufferDescsEqual(const BufferDesc& lhs_,
                                    const BufferDesc& rhs_) noexcept {
    return lhs_.size_bytes == rhs_.size_bytes &&
           lhs_.usage == rhs_.usage &&
           lhs_.host_visible == rhs_.host_visible &&
           lhs_.persistently_mapped == rhs_.persistently_mapped &&
           lhs_.allow_alias == rhs_.allow_alias;
}

[[nodiscard]] VkFormat ResolveVkFormat(const TextureFormat format_) noexcept {
    switch (format_) {
    case TextureFormat::r8g8b8a8_unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::r16g16b16a16_sfloat:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::d32_sfloat:
        return VK_FORMAT_D32_SFLOAT;
    case TextureFormat::unknown:
    default:
        break;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkSampleCountFlagBits ResolveSampleCount(const SampleCount sample_count_) noexcept {
    switch (sample_count_) {
    case SampleCount::x2:
        return VK_SAMPLE_COUNT_2_BIT;
    case SampleCount::x4:
        return VK_SAMPLE_COUNT_4_BIT;
    case SampleCount::x8:
        return VK_SAMPLE_COUNT_8_BIT;
    case SampleCount::x1:
    default:
        break;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

[[nodiscard]] VkImageUsageFlags ResolveImageUsage(const TextureDesc& desc_) noexcept {
    VkImageUsageFlags usage = 0U;
    if (HasTextureUsageFlag(desc_.usage, texture_usage_sampled_flag)) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_color_attachment_flag)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_depth_stencil_attachment_flag)) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_storage_flag)) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_transfer_src_flag)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_transfer_dst_flag)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    return usage;
}

[[nodiscard]] bool IsLazyMemoryUsageEligible(const TextureDesc& desc_) noexcept {
    if (!desc_.prefer_lazy_memory) {
        return false;
    }
    const bool has_attachment_usage =
        HasTextureUsageFlag(desc_.usage, texture_usage_color_attachment_flag) ||
        HasTextureUsageFlag(desc_.usage, texture_usage_depth_stencil_attachment_flag);
    if (!has_attachment_usage) {
        return false;
    }
    return !HasTextureUsageFlag(desc_.usage, texture_usage_sampled_flag) &&
           !HasTextureUsageFlag(desc_.usage, texture_usage_storage_flag) &&
           !HasTextureUsageFlag(desc_.usage, texture_usage_transfer_src_flag) &&
           !HasTextureUsageFlag(desc_.usage, texture_usage_transfer_dst_flag) &&
           !HasTextureUsageFlag(desc_.usage, texture_usage_present_flag);
}

[[nodiscard]] std::string BuildLazyMemoryIneligibleReason(const TextureDesc& desc_) {
    if (!desc_.prefer_lazy_memory) {
        return {};
    }
    const bool has_attachment_usage =
        HasTextureUsageFlag(desc_.usage, texture_usage_color_attachment_flag) ||
        HasTextureUsageFlag(desc_.usage, texture_usage_depth_stencil_attachment_flag);
    if (!has_attachment_usage) {
        return "resource usage has no attachment access for lazy memory";
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_sampled_flag)) {
        return "resource usage includes sampled reads";
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_storage_flag)) {
        return "resource usage includes storage access";
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_transfer_src_flag) ||
        HasTextureUsageFlag(desc_.usage, texture_usage_transfer_dst_flag)) {
        return "resource usage includes transfer access";
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_present_flag)) {
        return "resource usage includes present access";
    }
    return "resource usage is not eligible for lazy memory";
}

[[nodiscard]] bool SupportsLazilyAllocatedMemory(const VulkanContext& device_,
                                                 const std::uint32_t memory_type_bits_) noexcept {
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(device_.PhysicalDevice(), &memory_properties);
    for (std::uint32_t memory_type_index = 0U;
         memory_type_index < memory_properties.memoryTypeCount;
         ++memory_type_index) {
        const std::uint32_t mask = (1U << memory_type_index);
        if ((memory_type_bits_ & mask) == 0U) {
            continue;
        }
        const VkMemoryPropertyFlags flags =
            memory_properties.memoryTypes[memory_type_index].propertyFlags;
        if ((flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) != 0U) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] VkBufferUsageFlags ResolveBufferUsage(const BufferDesc& desc_) noexcept {
    VkBufferUsageFlags usage = 0U;
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_vertex_flag)) {
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_index_flag)) {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_uniform_flag)) {
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_storage_flag)) {
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_transfer_src_flag)) {
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_transfer_dst_flag)) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_indirect_flag)) {
        usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    return usage;
}

[[nodiscard]] VkImageAspectFlags ResolveImageAspect(const TextureDesc& desc_) noexcept {
    if (HasTextureUsageFlag(desc_.usage, texture_usage_depth_stencil_attachment_flag) ||
        desc_.format == TextureFormat::d32_sfloat) {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

[[nodiscard]] render::RenderTargetLifetime ResolveRenderTargetLifetime(
    const ResourceLifetime lifetime_) noexcept {
    switch (lifetime_) {
    case ResourceLifetime::imported:
        return render::RenderTargetLifetime::imported;
    case ResourceLifetime::transient:
        return render::RenderTargetLifetime::transient;
    case ResourceLifetime::persistent:
    default:
        break;
    }
    return render::RenderTargetLifetime::persistent;
}

[[nodiscard]] VkImageType ResolveImageType(const TextureDimension dimension_) noexcept {
    switch (dimension_) {
    case TextureDimension::image_3d:
        return VK_IMAGE_TYPE_3D;
    case TextureDimension::image_2d_array:
    case TextureDimension::cube:
    case TextureDimension::cube_array:
    case TextureDimension::image_2d:
    default:
        break;
    }
    return VK_IMAGE_TYPE_2D;
}

[[nodiscard]] VkImageViewType ResolveImageViewType(const TextureDesc& desc_) noexcept {
    switch (desc_.dimension) {
    case TextureDimension::image_2d_array:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::image_3d:
        return VK_IMAGE_VIEW_TYPE_3D;
    case TextureDimension::cube:
        return VK_IMAGE_VIEW_TYPE_CUBE;
    case TextureDimension::cube_array:
        return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    case TextureDimension::image_2d:
    default:
        break;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}

[[nodiscard]] auto FindTextureRecord(std::vector<PhysicalTextureRecord>& records_,
                                     const ResourceHandle logical_) {
    return std::find_if(records_.begin(),
                        records_.end(),
                        [&](const PhysicalTextureRecord& record_) {
                            return record_.logical.index == logical_.index;
                        });
}

[[nodiscard]] auto FindTextureRecord(const std::vector<PhysicalTextureRecord>& records_,
                                     const ResourceHandle logical_) {
    return std::find_if(records_.begin(),
                        records_.end(),
                        [&](const PhysicalTextureRecord& record_) {
                            return record_.logical.index == logical_.index;
                        });
}

[[nodiscard]] auto FindBufferRecord(std::vector<PhysicalBufferRecord>& records_,
                                    const ResourceHandle logical_) {
    return std::find_if(records_.begin(),
                        records_.end(),
                        [&](const PhysicalBufferRecord& record_) {
                            return record_.logical.index == logical_.index;
                        });
}

[[nodiscard]] auto FindBufferRecord(const std::vector<PhysicalBufferRecord>& records_,
                                    const ResourceHandle logical_) {
    return std::find_if(records_.begin(),
                        records_.end(),
                        [&](const PhysicalBufferRecord& record_) {
                            return record_.logical.index == logical_.index;
                        });
}

[[nodiscard]] bool HasImportedTextureBinding(const std::vector<render::RenderTargetHandle>& bindings_,
                                             const ResourceHandle logical_) noexcept {
    return logical_.index < bindings_.size() &&
           render::IsValidRenderTargetHandle(bindings_[logical_.index]);
}

[[nodiscard]] bool HasImportedBufferBinding(const std::vector<ImportedBufferBinding>& bindings_,
                                            const ResourceHandle logical_) noexcept {
    return logical_.index < bindings_.size() &&
           bindings_[logical_.index].buffer != VK_NULL_HANDLE;
}

struct PreparedTransientBuffer final {
    const CompiledResource* resource = nullptr;
    const TransientAllocationRecord* allocation_record = nullptr;
    resource::BufferCreateInfo create_info{};
    resource::BufferResource buffer_object{};
    VkMemoryRequirements memory_requirements{};
    bool dedicated_required = false;
    bool dedicated_preferred = false;
};

struct PreparedTransientImage final {
    const CompiledResource* resource = nullptr;
    const TransientAllocationRecord* allocation_record = nullptr;
    resource::ImageCreateInfo create_info{};
    resource::ImageResource image_object{};
    VkMemoryRequirements memory_requirements{};
    bool dedicated_required = false;
    bool dedicated_preferred = false;
};

struct ExactVulkanRequirements final {
    VkMemoryRequirements memory_requirements{};
    bool dedicated_required = false;
    bool dedicated_preferred = false;
};

struct ExactFootprintSlot final {
    bool valid = false;
    ResourceFootprint footprint{};
};

struct ExactFootprintProviderData final {
    const std::vector<ExactFootprintSlot>* footprints = nullptr;
};

struct StagedTextureRecord final {
    PhysicalTextureRecord record{};
    std::optional<std::size_t> reuse_previous_index{};
};

struct StagedBufferRecord final {
    PhysicalBufferRecord record{};
    std::optional<std::size_t> reuse_previous_index{};
};

struct CommittedTextureRecords final {
    std::vector<PhysicalTextureRecord> records{};
    std::vector<std::optional<std::size_t>> reuse_previous_indices{};
};

struct CommittedBufferRecords final {
    std::vector<PhysicalBufferRecord> records{};
    std::vector<std::optional<std::size_t>> reuse_previous_indices{};
};

struct LazyMemoryAllocationAttempt final {
    bool requested = false;
    bool should_attempt = false;
    bool realized = false;
    std::string unavailable_reason{};
};

struct LazyMemoryResolutionSlot final {
    bool valid = false;
    VulkanLazyMemoryResolution resolution{};
};

bool g_fail_resolve_before_publish_for_testing = false;

[[nodiscard]] const TransientAllocationRecord* FindTransientAllocationRecord(
    const TransientAllocationPlan& plan_,
    const ResourceHandle logical_) noexcept {
    const auto existing = std::find_if(plan_.records.begin(),
                                       plan_.records.end(),
                                       [&](const TransientAllocationRecord& record_) {
                                           return record_.resource.index == logical_.index;
                                       });
    return existing != plan_.records.end() ? &(*existing) : nullptr;
}

[[nodiscard]] auto FindPreparedTransientBuffer(std::vector<PreparedTransientBuffer>& prepared_,
                                               const ResourceHandle logical_) {
    return std::find_if(prepared_.begin(),
                        prepared_.end(),
                        [&](const PreparedTransientBuffer& entry_) {
                            return entry_.resource != nullptr &&
                                   entry_.resource->handle.index == logical_.index;
                        });
}

[[nodiscard]] auto FindPreparedTransientImage(std::vector<PreparedTransientImage>& prepared_,
                                              const ResourceHandle logical_) {
    return std::find_if(prepared_.begin(),
                        prepared_.end(),
                        [&](const PreparedTransientImage& entry_) {
                            return entry_.resource != nullptr &&
                                   entry_.resource->handle.index == logical_.index;
                        });
}

[[nodiscard]] auto FindTransientBufferPageRecord(
    std::vector<TransientBufferPageRecord>& pages_,
    const std::uint32_t page_index_) {
    return std::find_if(pages_.begin(),
                        pages_.end(),
                        [&](const TransientBufferPageRecord& page_) {
                            return page_.page_index == page_index_;
                        });
}

[[nodiscard]] auto FindTransientImagePageRecord(
    std::vector<TransientImagePageRecord>& pages_,
    const std::uint32_t page_index_) {
    return std::find_if(pages_.begin(),
                        pages_.end(),
                        [&](const TransientImagePageRecord& page_) {
                            return page_.page_index == page_index_;
                        });
}

[[nodiscard]] ExactVulkanRequirements QueryBufferRequirements(VulkanContext& device_,
                                                              const VkBuffer buffer_) {
    ExactVulkanRequirements result{};
    VkBufferMemoryRequirementsInfo2 requirements_info{};
    requirements_info.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
    requirements_info.buffer = buffer_;

    VkMemoryDedicatedRequirements dedicated_requirements{};
    dedicated_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;

    VkMemoryRequirements2 requirements{};
    requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    requirements.pNext = &dedicated_requirements;

    vkGetBufferMemoryRequirements2(device_.Device(), &requirements_info, &requirements);
    result.memory_requirements = requirements.memoryRequirements;
    result.dedicated_required = dedicated_requirements.requiresDedicatedAllocation == VK_TRUE;
    result.dedicated_preferred = dedicated_requirements.prefersDedicatedAllocation == VK_TRUE;
    return result;
}

[[nodiscard]] ExactVulkanRequirements QueryImageRequirements(VulkanContext& device_,
                                                             const VkImage image_) {
    ExactVulkanRequirements result{};
    VkImageMemoryRequirementsInfo2 requirements_info{};
    requirements_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    requirements_info.image = image_;

    VkMemoryDedicatedRequirements dedicated_requirements{};
    dedicated_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;

    VkMemoryRequirements2 requirements{};
    requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    requirements.pNext = &dedicated_requirements;

    vkGetImageMemoryRequirements2(device_.Device(), &requirements_info, &requirements);
    result.memory_requirements = requirements.memoryRequirements;
    result.dedicated_required = dedicated_requirements.requiresDedicatedAllocation == VK_TRUE;
    result.dedicated_preferred = dedicated_requirements.prefersDedicatedAllocation == VK_TRUE;
    return result;
}

[[nodiscard]] LazyMemoryAllocationAttempt EvaluateLazyMemoryAttempt(
    const CompiledResource& resource_,
    VulkanContext& device_,
    const VkMemoryRequirements& requirements_) {
    LazyMemoryAllocationAttempt attempt{};
    attempt.requested = resource_.kind == ResourceKind::texture &&
                        resource_.texture.prefer_lazy_memory;
    if (!attempt.requested) {
        return attempt;
    }
    if (!IsLazyMemoryUsageEligible(resource_.texture)) {
        attempt.unavailable_reason = BuildLazyMemoryIneligibleReason(resource_.texture);
        return attempt;
    }
    if (!SupportsLazilyAllocatedMemory(device_, requirements_.memoryTypeBits)) {
        attempt.unavailable_reason = "no compatible lazily allocated memory type";
        return attempt;
    }
    attempt.should_attempt = true;
    return attempt;
}

void SetLazyMemoryResolution(std::vector<LazyMemoryResolutionSlot>& resolution_slots_,
                             const CompiledResource& resource_,
                             const LazyMemoryAllocationAttempt& attempt_) {
    if (resource_.kind != ResourceKind::texture || !attempt_.requested) {
        return;
    }
    if (resource_.handle.index >= resolution_slots_.size()) {
        resolution_slots_.resize(resource_.handle.index + 1U);
    }
    auto& slot = resolution_slots_[resource_.handle.index];
    slot.valid = true;
    slot.resolution.logical = resource_.handle;
    slot.resolution.debug_name = resource_.debug_name;
    slot.resolution.requested = true;
    slot.resolution.realized = attempt_.realized;
    slot.resolution.unavailable_reason = attempt_.realized
        ? std::string{}
        : attempt_.unavailable_reason;
}

[[nodiscard]] ResourceFootprint BuildExactBufferFootprint(const CompiledResource& resource_,
                                                          const PreparedTransientBuffer& prepared_) {
    ResourceFootprint footprint{};
    footprint.size_bytes = prepared_.memory_requirements.size;
    footprint.alignment_bytes = prepared_.memory_requirements.alignment;
    footprint.memory_type_bits = prepared_.memory_requirements.memoryTypeBits;
    footprint.usage_flags = resource_.buffer.usage;
    footprint.dedicated_required = prepared_.dedicated_required;
    footprint.dedicated_preferred = prepared_.dedicated_preferred;
    footprint.host_visible = resource_.buffer.host_visible;
    footprint.persistently_mapped = resource_.buffer.persistently_mapped;
    return footprint;
}

[[nodiscard]] ResourceFootprint BuildExactImageFootprint(const CompiledResource& resource_,
                                                         const PreparedTransientImage& prepared_) {
    ResourceFootprint footprint{};
    footprint.size_bytes = prepared_.memory_requirements.size;
    footprint.alignment_bytes = prepared_.memory_requirements.alignment;
    footprint.memory_type_bits = prepared_.memory_requirements.memoryTypeBits;
    footprint.usage_flags = resource_.texture.usage;
    footprint.dedicated_required = prepared_.dedicated_required;
    footprint.dedicated_preferred = prepared_.dedicated_preferred;
    footprint.lazy_memory_requested = resource_.texture.prefer_lazy_memory;
    return footprint;
}

[[nodiscard]] bool ResolveExactFootprint(const CompiledResource& resource_,
                                         ResourceFootprint& footprint_,
                                         const void* user_data_,
                                         std::string& error_message_) {
    const auto* provider = static_cast<const ExactFootprintProviderData*>(user_data_);
    if (provider == nullptr || provider->footprints == nullptr) {
        error_message_ = "exact footprint provider is not initialized";
        return false;
    }
    if (resource_.handle.index >= provider->footprints->size()) {
        error_message_ = "exact footprint provider is missing a resource slot";
        return false;
    }

    const auto& slot = (*provider->footprints)[resource_.handle.index];
    if (!slot.valid) {
        error_message_ = "exact footprint provider is missing resource footprint";
        return false;
    }

    footprint_ = slot.footprint;
    error_message_.clear();
    return true;
}

void DestroyPreparedTransientBuffers(VulkanContext& device_,
                                     std::vector<PreparedTransientBuffer>& prepared_) noexcept {
    for (auto& prepared : prepared_) {
        if (prepared.buffer_object.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(device_, prepared.buffer_object);
        }
    }
    prepared_.clear();
}

void DestroyPreparedTransientImages(VulkanContext& device_,
                                    std::vector<PreparedTransientImage>& prepared_) noexcept {
    for (auto& prepared : prepared_) {
        if (prepared.image_object.image != VK_NULL_HANDLE) {
            resource::ImageHost::DestroyImage(device_, prepared.image_object);
        }
    }
    prepared_.clear();
}

void DestroyTransientBufferPages(std::vector<TransientBufferPageRecord>& pages_) noexcept {
    for (auto& page_ : pages_) {
        if (page_.memory_host != nullptr && page_.allocation_slice.valid()) {
            page_.memory_host->Deallocate(page_.allocation_slice);
            page_.allocation_slice = {};
        }
        page_.memory_host = nullptr;
    }
    pages_.clear();
}

void DestroyTransientImagePages(std::vector<TransientImagePageRecord>& pages_) noexcept {
    for (auto& page_ : pages_) {
        if (page_.memory_host != nullptr && page_.allocation_slice.valid()) {
            page_.memory_host->Deallocate(page_.allocation_slice);
            page_.allocation_slice = {};
        }
        page_.memory_host = nullptr;
    }
    pages_.clear();
}

void DestroyTextureRecords(VulkanContext& device_,
                           render::RenderTargetHost& render_target_host_,
                           std::vector<PhysicalTextureRecord>& records_,
                           const std::uint64_t last_submitted_value_,
                           const std::uint64_t completed_submit_value_) noexcept {
    for (auto& record_ : records_) {
        if (!record_.imported && render::IsValidRenderTargetHandle(record_.render_target)) {
            (void)render_target_host_.DestroyTarget(device_,
                                                    record_.render_target,
                                                    last_submitted_value_,
                                                    completed_submit_value_);
            record_.render_target = render::invalid_render_target_handle;
        }
        if (record_.owned_resource.image != VK_NULL_HANDLE) {
            resource::ImageHost::DestroyImage(device_, record_.owned_resource);
        }
    }
    records_.clear();
}

void DestroyBufferRecords(VulkanContext& device_,
                          std::vector<PhysicalBufferRecord>& records_) noexcept {
    for (auto& record_ : records_) {
        if (!record_.imported && record_.owned_resource.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(device_, record_.owned_resource);
        }
    }
    records_.clear();
}

void DestroyStagedTextureRecords(VulkanContext& device_,
                                 render::RenderTargetHost& render_target_host_,
                                 std::vector<StagedTextureRecord>& staged_records_,
                                 const std::uint64_t last_submitted_value_,
                                 const std::uint64_t completed_submit_value_) noexcept {
    for (auto& staged_ : staged_records_) {
        if (staged_.reuse_previous_index.has_value()) {
            continue;
        }
        if (!staged_.record.imported && render::IsValidRenderTargetHandle(staged_.record.render_target)) {
            (void)render_target_host_.DestroyTarget(device_,
                                                    staged_.record.render_target,
                                                    last_submitted_value_,
                                                    completed_submit_value_);
            staged_.record.render_target = render::invalid_render_target_handle;
        }
        if (staged_.record.owned_resource.image != VK_NULL_HANDLE) {
            resource::ImageHost::DestroyImage(device_, staged_.record.owned_resource);
        }
    }
    staged_records_.clear();
}

void DestroyStagedBufferRecords(VulkanContext& device_,
                                std::vector<StagedBufferRecord>& staged_records_) noexcept {
    for (auto& staged_ : staged_records_) {
        if (staged_.reuse_previous_index.has_value()) {
            continue;
        }
        if (!staged_.record.imported && staged_.record.owned_resource.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(device_, staged_.record.owned_resource);
        }
    }
    staged_records_.clear();
}

[[nodiscard]] const char* QueueClassToStringLocal(const QueueClass queue_) noexcept {
    switch (queue_) {
    case QueueClass::graphics:
        return "graphics";
    case QueueClass::compute:
        return "compute";
    case QueueClass::transfer:
        return "transfer";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] const CompiledResource* FindCompiledResourceByHandle(
    const CompiledRenderGraph& compiled_graph_,
    const ResourceHandle handle_) noexcept {
    const auto resource_it = std::find_if(
        compiled_graph_.Resources().begin(),
        compiled_graph_.Resources().end(),
        [&](const CompiledResource& resource_) {
            return resource_.handle.index == handle_.index;
        });
    return resource_it != compiled_graph_.Resources().end() ? &(*resource_it) : nullptr;
}

[[nodiscard]] std::string BuildUnsupportedCrossQueueAliasMessage(
    const CompiledRenderGraph& compiled_graph_,
    const ResourceHandle previous_handle_,
    const ResourceHandle next_handle_,
    const LogicalBarrier& barrier_) {
    const auto* previous_resource = FindCompiledResourceByHandle(compiled_graph_, previous_handle_);
    const auto* next_resource = FindCompiledResourceByHandle(compiled_graph_, next_handle_);
    const std::string previous_name =
        previous_resource != nullptr ? previous_resource->debug_name : std::string("unknown_previous_resource");
    const std::string next_name =
        next_resource != nullptr ? next_resource->debug_name : std::string("unknown_next_resource");

    std::string message = "VulkanResourceTable::Resolve does not support cross-queue alias realization";
    message += " from ";
    message += previous_name;
    message += " to ";
    message += next_name;
    message += " (";
    message += QueueClassToStringLocal(barrier_.src_queue);
    message += " -> ";
    message += QueueClassToStringLocal(barrier_.dst_queue);
    message += ")";
    return message;
}

void ValidateAliasRealizationSupport(const CompiledRenderGraph& compiled_graph_,
                                     const BarrierPlan& barrier_plan_,
                                     const RenderGraphExecutorCapabilities& executor_capabilities_) {
    if (executor_capabilities_.supports_queue_transfer_batches) {
        return;
    }

    for (const auto& batch_ : barrier_plan_.barrier_batches) {
        for (const auto& barrier_ : batch_.barriers) {
            if (!barrier_.aliasing || !barrier_.queue_transfer) {
                continue;
            }

            ResourceHandle previous_handle{
                .index = barrier_.resource.resource_index,
                .generation = 1U,
            };
            ResourceHandle next_handle{
                .index = barrier_.resource.resource_index,
                .generation = 1U,
            };
            const auto alias_decision = std::find_if(
                barrier_plan_.alias_barriers.begin(),
                barrier_plan_.alias_barriers.end(),
                [&](const AliasBarrierDecision& decision_) {
                    return decision_.required &&
                           decision_.realized &&
                           decision_.next.index == barrier_.resource.resource_index &&
                           decision_.previous_last_pass_order == barrier_.src_pass_order &&
                           decision_.next_first_pass_order == barrier_.dst_pass_order;
                });
            if (alias_decision != barrier_plan_.alias_barriers.end()) {
                previous_handle = alias_decision->previous;
                next_handle = alias_decision->next;
            }
            throw VulkanResourceResolveError(
                VulkanResourceResolveErrorCode::unsupported_cross_queue_alias,
                previous_handle,
                next_handle,
                barrier_.src_queue,
                barrier_.dst_queue,
                BuildUnsupportedCrossQueueAliasMessage(compiled_graph_,
                                                      previous_handle,
                                                      next_handle,
                                                      barrier_));
        }
    }
}

void CommitTextureRecords(PhysicalTextureRecord& destination_,
                          PhysicalTextureRecord& source_) noexcept {
    destination_.logical = source_.logical;
    destination_.debug_name.swap(source_.debug_name);
    destination_.lifetime = source_.lifetime;
    destination_.desc = source_.desc;
    destination_.render_target = source_.render_target;
    destination_.owned_resource = source_.owned_resource;
    destination_.alias_page_index = source_.alias_page_index;
    destination_.aliased = source_.aliased;
    destination_.imported = source_.imported;

    source_.render_target = render::invalid_render_target_handle;
    source_.owned_resource = {};
    source_.alias_page_index = invalid_render_graph_index;
    source_.aliased = false;
    source_.imported = false;
}

void CommitBufferRecords(PhysicalBufferRecord& destination_,
                         PhysicalBufferRecord& source_) noexcept {
    destination_.logical = source_.logical;
    destination_.debug_name.swap(source_.debug_name);
    destination_.lifetime = source_.lifetime;
    destination_.desc = source_.desc;
    destination_.owned_resource = source_.owned_resource;
    destination_.imported_buffer = source_.imported_buffer;
    destination_.alias_page_index = source_.alias_page_index;
    destination_.aliased = source_.aliased;
    destination_.imported = source_.imported;

    source_.owned_resource = {};
    source_.imported_buffer = {};
    source_.alias_page_index = invalid_render_graph_index;
    source_.aliased = false;
    source_.imported = false;
}

[[nodiscard]] CommittedTextureRecords BuildCommittedTextureRecords(
    std::vector<StagedTextureRecord>& staged_records_) {
    CommittedTextureRecords committed{};
    committed.records.resize(staged_records_.size());
    committed.reuse_previous_indices.resize(staged_records_.size());
    for (std::size_t index = 0U; index < staged_records_.size(); ++index) {
        committed.reuse_previous_indices[index] = staged_records_[index].reuse_previous_index;
        CommitTextureRecords(committed.records[index], staged_records_[index].record);
    }
    staged_records_.clear();
    return committed;
}

[[nodiscard]] CommittedBufferRecords BuildCommittedBufferRecords(
    std::vector<StagedBufferRecord>& staged_records_) {
    CommittedBufferRecords committed{};
    committed.records.resize(staged_records_.size());
    committed.reuse_previous_indices.resize(staged_records_.size());
    for (std::size_t index = 0U; index < staged_records_.size(); ++index) {
        committed.reuse_previous_indices[index] = staged_records_[index].reuse_previous_index;
        CommitBufferRecords(committed.records[index], staged_records_[index].record);
    }
    staged_records_.clear();
    return committed;
}

void AttachReusedTextureRecords(CommittedTextureRecords& committed_,
                                std::vector<PhysicalTextureRecord>& previous_records_) noexcept {
    for (std::size_t index = 0U; index < committed_.records.size(); ++index) {
        const auto previous_index = committed_.reuse_previous_indices[index];
        if (!previous_index.has_value()) {
            continue;
        }

        auto& record = committed_.records[index];
        auto& previous = previous_records_[*previous_index];
        record.render_target = previous.render_target;
        previous.render_target = render::invalid_render_target_handle;
        if (record.owned_resource.image == VK_NULL_HANDLE &&
            previous.owned_resource.image != VK_NULL_HANDLE) {
            record.owned_resource = previous.owned_resource;
            previous.owned_resource = {};
        }
    }
}

void AttachReusedBufferRecords(CommittedBufferRecords& committed_,
                               std::vector<PhysicalBufferRecord>& previous_records_) noexcept {
    for (std::size_t index = 0U; index < committed_.records.size(); ++index) {
        const auto previous_index = committed_.reuse_previous_indices[index];
        if (!previous_index.has_value()) {
            continue;
        }

        auto& record = committed_.records[index];
        auto& previous = previous_records_[*previous_index];
        record.owned_resource = previous.owned_resource;
        previous.owned_resource = {};
    }
}

} // namespace

void SetVulkanResourceTableResolveFailureBeforePublishForTesting(const bool enabled_) noexcept {
    g_fail_resolve_before_publish_for_testing = enabled_;
}

VulkanResourceResolveError::VulkanResourceResolveError(
    const VulkanResourceResolveErrorCode code_,
    const ResourceHandle previous_resource_,
    const ResourceHandle next_resource_,
    const QueueClass source_queue_,
    const QueueClass target_queue_,
    std::string message_)
    : std::runtime_error(std::move(message_))
    , code(code_)
    , previous_resource(previous_resource_)
    , next_resource(next_resource_)
    , source_queue(source_queue_)
    , target_queue(target_queue_) {}

void VulkanResourceTable::BeginFrame(VulkanContext& device_,
                                     render::RenderTargetHost& render_target_host_,
                                     const std::uint64_t last_submitted_value_,
                                     const std::uint64_t completed_submit_value_) {
    (void)retired_transient_images.Collect(
        completed_submit_value_,
        [&](RetiredTransientImagePayload& payload_) {
            if (payload_.owned_resource.image != VK_NULL_HANDLE) {
                resource::ImageHost::DestroyImage(device_, payload_.owned_resource);
            }
        });
    (void)retired_transient_image_pages.Collect(
        completed_submit_value_,
        [&](TransientImagePageRecord& page_) {
            if (page_.memory_host != nullptr && page_.allocation_slice.valid()) {
                page_.memory_host->Deallocate(page_.allocation_slice);
                page_.allocation_slice = {};
            }
            page_.memory_host = nullptr;
        });

    DestroyTransientRecords(device_,
                            render_target_host_,
                            last_submitted_value_,
                            completed_submit_value_);
    imported_textures.clear();
    imported_buffers.clear();
    lazy_memory_resolutions.clear();
    RefreshStats();
}

void VulkanResourceTable::RegisterImportedTexture(const ResourceHandle logical_,
                                                  const render::RenderTargetHandle render_target_) noexcept {
    if (logical_.index >= imported_textures.size()) {
        imported_textures.resize(logical_.index + 1U);
    }
    imported_textures[logical_.index] = render_target_;
}

void VulkanResourceTable::RegisterImportedBuffer(const ResourceHandle logical_,
                                                 const ImportedBufferBinding& imported_buffer_) noexcept {
    if (logical_.index >= imported_buffers.size()) {
        imported_buffers.resize(logical_.index + 1U);
    }
    imported_buffers[logical_.index] = imported_buffer_;
}

void VulkanResourceTable::Resolve(VulkanContext& device_,
                                  resource::GpuMemoryHost& gpu_memory_host_,
                                  render::RenderTargetHost& render_target_host_,
                                  CompiledRenderGraph& compiled_graph_,
                                  const std::uint64_t last_submitted_value_,
                                  const std::uint64_t completed_submit_value_,
                                  const RenderGraphExecutorCapabilities& executor_capabilities_) {
    std::vector<PreparedTransientBuffer> prepared_transient_buffers{};
    std::vector<PreparedTransientImage> prepared_transient_images{};
    std::vector<StagedTextureRecord> staged_textures{};
    std::vector<StagedBufferRecord> staged_buffers{};
    std::vector<TransientBufferPageRecord> staged_transient_buffer_pages{};
    std::vector<TransientImagePageRecord> staged_transient_image_pages{};
    CommittedTextureRecords committed_textures{};
    CommittedBufferRecords committed_buffers{};
    TransientAllocationPlan realized_transient_plan{};
    BarrierPlan realized_barrier_plan{};
    std::vector<VulkanLazyMemoryResolution> staged_lazy_memory_resolutions{};

    try {
        prepared_transient_buffers.reserve(compiled_graph_.Resources().size());
        prepared_transient_images.reserve(compiled_graph_.Resources().size());
        staged_textures.reserve(compiled_graph_.Resources().size());
        staged_buffers.reserve(compiled_graph_.Resources().size());

        std::uint32_t max_resource_index = 0U;
        for (const auto& resource_ : compiled_graph_.Resources()) {
            max_resource_index = (std::max)(max_resource_index, resource_.handle.index);
        }
        std::vector<ExactFootprintSlot> exact_footprints(static_cast<std::size_t>(max_resource_index) + 1U);
        std::vector<LazyMemoryResolutionSlot> lazy_memory_resolution_slots(
            static_cast<std::size_t>(max_resource_index) + 1U);

        for (const auto& resource_ : compiled_graph_.Resources()) {
            if (resource_.lifetime != ResourceLifetime::transient) {
                continue;
            }

            if (resource_.kind == ResourceKind::buffer) {
                PreparedTransientBuffer prepared{};
                prepared.resource = &resource_;
                prepared.create_info = BuildBufferCreateInfo(resource_.buffer);
                prepared.buffer_object = resource::BufferHost::CreateBufferObject(device_,
                                                                                  prepared.create_info);
                const auto exact_requirements = QueryBufferRequirements(device_,
                                                                        prepared.buffer_object.buffer);
                prepared.memory_requirements = exact_requirements.memory_requirements;
                prepared.dedicated_required = exact_requirements.dedicated_required;
                prepared.dedicated_preferred = exact_requirements.dedicated_preferred;
                prepared_transient_buffers.push_back(std::move(prepared));

                auto& footprint_slot = exact_footprints[resource_.handle.index];
                footprint_slot.valid = true;
                footprint_slot.footprint = BuildExactBufferFootprint(
                    resource_,
                    prepared_transient_buffers.back());
                continue;
            }

            PreparedTransientImage prepared{};
            prepared.resource = &resource_;
            prepared.create_info = BuildImageCreateInfo(resource_.texture);
            prepared.create_info.create_default_view = false;
            prepared.image_object = resource::ImageHost::CreateImageObject(device_,
                                                                           prepared.create_info);
            const auto exact_requirements = QueryImageRequirements(device_,
                                                                   prepared.image_object.image);
            prepared.memory_requirements = exact_requirements.memory_requirements;
            prepared.dedicated_required = exact_requirements.dedicated_required;
            prepared.dedicated_preferred = exact_requirements.dedicated_preferred;
            prepared_transient_images.push_back(std::move(prepared));

            auto& footprint_slot = exact_footprints[resource_.handle.index];
            footprint_slot.valid = true;
            footprint_slot.footprint = BuildExactImageFootprint(
                resource_,
                prepared_transient_images.back());
        }

        ExactFootprintProviderData exact_provider_data{
            .footprints = &exact_footprints,
        };
        realized_transient_plan = BuildTransientAllocationPlan(
            compiled_graph_,
            TransientFootprintProvider{
                .user_data = &exact_provider_data,
                .resolve_fn = &ResolveExactFootprint,
            });
        realized_barrier_plan = BuildBarrierPlan(compiled_graph_,
                                                 &realized_transient_plan);
        ValidateAliasRealizationSupport(compiled_graph_,
                                        realized_barrier_plan,
                                        executor_capabilities_);

        const auto allocate_image_memory_with_lazy_fallback =
            [&](const VkMemoryRequirements& requirements_,
                const VkImageTiling tiling_,
                const bool dedicated_required_,
                const bool dedicated_preferred_,
                const VkImage dedicated_image_,
                LazyMemoryAllocationAttempt& lazy_attempt_) {
                if (lazy_attempt_.should_attempt) {
                    try {
                        const auto slice = gpu_memory_host_.AllocateImageMemory(
                            requirements_,
                            tiling_,
                            VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
                            VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT |
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            false,
                            Center::Memory::Vulkan::LifetimeHint::transient,
                            Center::Memory::Vulkan::HostAccess::none,
                            dedicated_required_,
                            dedicated_preferred_,
                            dedicated_image_);
                        lazy_attempt_.realized = true;
                        lazy_attempt_.unavailable_reason.clear();
                        return slice;
                    } catch (const std::exception& exception_) {
                        std::ostringstream reason_stream{};
                        reason_stream << "lazy allocation failed: " << exception_.what();
                        lazy_attempt_.realized = false;
                        lazy_attempt_.unavailable_reason = reason_stream.str();
                    }
                }

                return gpu_memory_host_.AllocateImageMemory(
                    requirements_,
                    tiling_,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    false,
                    Center::Memory::Vulkan::LifetimeHint::transient,
                    Center::Memory::Vulkan::HostAccess::none,
                    dedicated_required_,
                    dedicated_preferred_,
                    dedicated_image_);
            };

        for (const auto& page_ : realized_transient_plan.pages) {
            if (page_.kind != ResourceKind::buffer || page_.resources.empty()) {
                if (page_.kind != ResourceKind::texture || page_.resources.empty()) {
                    continue;
                }

                VkDeviceSize page_size = 0U;
                VkDeviceSize page_alignment = 1U;
                std::uint32_t memory_type_bits = 0xFFFFFFFFU;
                bool have_resource = false;
                VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
                for (const auto handle_ : page_.resources) {
                    const auto prepared = FindPreparedTransientImage(prepared_transient_images, handle_);
                    if (prepared == prepared_transient_images.end()) {
                        continue;
                    }
                    page_size = (std::max)(page_size, prepared->memory_requirements.size);
                    page_alignment = (std::max)(page_alignment, prepared->memory_requirements.alignment);
                    memory_type_bits &= prepared->memory_requirements.memoryTypeBits;
                    tiling = prepared->create_info.tiling;
                    have_resource = true;
                }

                if (!have_resource) {
                    continue;
                }
                if (memory_type_bits == 0U) {
                    throw std::runtime_error(
                        "VulkanResourceTable transient image alias page has no shared memory type bits");
                }

                VkMemoryRequirements aggregate_requirements{};
                aggregate_requirements.size = page_size;
                aggregate_requirements.alignment = page_alignment;
                aggregate_requirements.memoryTypeBits = memory_type_bits;

                const auto* representative_resource =
                    FindCompiledResourceByHandle(compiled_graph_, page_.resources.front());
                LazyMemoryAllocationAttempt lazy_attempt{};
                if (representative_resource != nullptr) {
                    lazy_attempt = EvaluateLazyMemoryAttempt(*representative_resource,
                                                             device_,
                                                             aggregate_requirements);
                }

                TransientImagePageRecord page_record{};
                page_record.page_index = page_.page_index;
                page_record.size_bytes = page_size;
                page_record.alignment_bytes = page_alignment;
                page_record.memory_type_bits = memory_type_bits;
                page_record.tiling = tiling;
                page_record.resources = page_.resources;
                page_record.allocation_slice = representative_resource != nullptr
                    ? allocate_image_memory_with_lazy_fallback(aggregate_requirements,
                                                               tiling,
                                                               false,
                                                               false,
                                                               VK_NULL_HANDLE,
                                                               lazy_attempt)
                    : gpu_memory_host_.AllocateImageMemory(
                          aggregate_requirements,
                          tiling,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          false,
                          Center::Memory::Vulkan::LifetimeHint::transient,
                          Center::Memory::Vulkan::HostAccess::none,
                          false,
                          false);
                page_record.memory_host = &gpu_memory_host_;
                if (representative_resource != nullptr && lazy_attempt.requested) {
                    for (const auto handle_ : page_.resources) {
                        const auto* page_resource = FindCompiledResourceByHandle(compiled_graph_, handle_);
                        if (page_resource == nullptr) {
                            continue;
                        }
                        SetLazyMemoryResolution(lazy_memory_resolution_slots,
                                                *page_resource,
                                                lazy_attempt);
                    }
                }
                staged_transient_image_pages.push_back(std::move(page_record));
                continue;
            }

            VkDeviceSize page_size = 0U;
            VkDeviceSize page_alignment = 1U;
            std::uint32_t memory_type_bits = 0xFFFFFFFFU;
            bool have_resource = false;
            for (const auto handle_ : page_.resources) {
                const auto prepared = FindPreparedTransientBuffer(prepared_transient_buffers, handle_);
                if (prepared == prepared_transient_buffers.end()) {
                    continue;
                }
                page_size = (std::max)(page_size, prepared->memory_requirements.size);
                page_alignment = (std::max)(page_alignment, prepared->memory_requirements.alignment);
                memory_type_bits &= prepared->memory_requirements.memoryTypeBits;
                have_resource = true;
            }

            if (!have_resource) {
                continue;
            }
            if (memory_type_bits == 0U) {
                throw std::runtime_error(
                    "VulkanResourceTable transient buffer alias page has no shared memory type bits");
            }

            VkMemoryRequirements aggregate_requirements{};
            aggregate_requirements.size = page_size;
            aggregate_requirements.alignment = page_alignment;
            aggregate_requirements.memoryTypeBits = memory_type_bits;

            TransientBufferPageRecord page_record{};
            page_record.page_index = page_.page_index;
            page_record.size_bytes = page_size;
            page_record.alignment_bytes = page_alignment;
            page_record.memory_type_bits = memory_type_bits;
            page_record.resources = page_.resources;
            page_record.allocation_slice = gpu_memory_host_.AllocateBufferMemory(
                aggregate_requirements,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                false,
                Center::Memory::Vulkan::LifetimeHint::transient,
                Center::Memory::Vulkan::HostAccess::none,
                false,
                false);
            page_record.memory_host = &gpu_memory_host_;
            staged_transient_buffer_pages.push_back(std::move(page_record));
        }

        for (const auto& resource_ : compiled_graph_.Resources()) {
            if (resource_.kind == ResourceKind::texture) {
                const auto previous = FindTextureRecord(textures, resource_.handle);
                StagedTextureRecord staged{};
                staged.record.logical = resource_.handle;
                staged.record.debug_name = resource_.debug_name;
                staged.record.lifetime = resource_.lifetime;
                staged.record.desc = resource_.texture;

                if (resource_.lifetime == ResourceLifetime::imported) {
                    if (!HasImportedTextureBinding(imported_textures, resource_.handle)) {
                        throw std::invalid_argument(
                            "VulkanResourceTable missing imported texture binding for logical resource");
                    }
                    staged.record.render_target = imported_textures[resource_.handle.index];
                    staged.record.imported = true;
                } else if (resource_.lifetime == ResourceLifetime::persistent &&
                           previous != textures.end() &&
                           !previous->imported &&
                           TextureDescsEqual(previous->desc, resource_.texture) &&
                           render::IsValidRenderTargetHandle(previous->render_target)) {
                    staged.reuse_previous_index = static_cast<std::size_t>(
                        std::distance(textures.begin(), previous));
                } else {
                    if (resource_.lifetime == ResourceLifetime::persistent) {
                        auto desc = BuildRenderTargetDesc(resource_.texture);
                        desc.debug_name = staged.record.debug_name.c_str();
                        desc.lifetime = ResolveRenderTargetLifetime(resource_.lifetime);
                        staged.record.render_target = render_target_host_.CreatePersistentTarget(device_, desc);
                    } else {
                        const auto prepared = FindPreparedTransientImage(prepared_transient_images, resource_.handle);
                        if (prepared == prepared_transient_images.end()) {
                            throw std::runtime_error(
                                "VulkanResourceTable missing prepared transient image");
                        }

                        staged.record.owned_resource = prepared->image_object;
                        prepared->image_object = {};

                        const auto* allocation_record = FindTransientAllocationRecord(
                            realized_transient_plan,
                            resource_.handle);
                        const bool page_backed = allocation_record != nullptr &&
                                                 allocation_record->eligible &&
                                                 allocation_record->page_index != invalid_render_graph_index;
                        if (page_backed) {
                            const auto page_it = FindTransientImagePageRecord(
                                staged_transient_image_pages,
                                allocation_record->page_index);
                            if (page_it == staged_transient_image_pages.end()) {
                                throw std::runtime_error(
                                    "VulkanResourceTable transient image page assignment missing page record");
                            }
                            resource::ImageHost::BindAllocation(staged.record.owned_resource,
                                                                gpu_memory_host_,
                                                                page_it->allocation_slice,
                                                                false,
                                                                allocation_record->page_offset_bytes);
                            staged.record.alias_page_index = page_it->page_index;
                            staged.record.aliased = allocation_record->aliased;
                        } else {
                            auto lazy_attempt = EvaluateLazyMemoryAttempt(resource_,
                                                                          device_,
                                                                          prepared->memory_requirements);
                            const auto allocation_slice = allocate_image_memory_with_lazy_fallback(
                                prepared->memory_requirements,
                                prepared->create_info.tiling,
                                prepared->dedicated_required,
                                prepared->dedicated_preferred,
                                staged.record.owned_resource.image,
                                lazy_attempt);
                            SetLazyMemoryResolution(lazy_memory_resolution_slots,
                                                    resource_,
                                                    lazy_attempt);
                            resource::ImageHost::BindAllocation(staged.record.owned_resource,
                                                                gpu_memory_host_,
                                                                allocation_slice,
                                                                true,
                                                                0U);
                        }

                        render::ImportedRenderTargetDesc imported_desc{};
                        imported_desc.debug_name = staged.record.debug_name.c_str();
                        imported_desc.ownership = render::RenderTargetOwnership::imported_image_owned_view;
                        imported_desc.dimension = static_cast<render::RenderTargetDimension>(resource_.texture.dimension);
                        imported_desc.image = staged.record.owned_resource.image;
                        imported_desc.image_view = VK_NULL_HANDLE;
                        imported_desc.format = staged.record.owned_resource.format;
                        imported_desc.extent = staged.record.owned_resource.extent;
                        imported_desc.samples = staged.record.owned_resource.samples;
                        imported_desc.usage = staged.record.owned_resource.usage;
                        imported_desc.aspect = ResolveImageAspect(resource_.texture);
                        imported_desc.mip_levels = staged.record.owned_resource.mip_levels;
                        imported_desc.array_layers = staged.record.owned_resource.array_layers;
                        imported_desc.color_encoding = render::RenderTargetColorEncoding::linear;
                        imported_desc.initial_state = render::RenderTargetStateKind::undefined;
                        staged.record.render_target = render_target_host_.ImportTarget(device_, imported_desc);
                    }
                }

                staged_textures.push_back(std::move(staged));
                continue;
            }

            const auto previous = FindBufferRecord(buffers, resource_.handle);
            StagedBufferRecord staged{};
            staged.record.logical = resource_.handle;
            staged.record.debug_name = resource_.debug_name;
            staged.record.lifetime = resource_.lifetime;
            staged.record.desc = resource_.buffer;

            if (resource_.lifetime == ResourceLifetime::imported) {
                if (!HasImportedBufferBinding(imported_buffers, resource_.handle)) {
                    throw std::invalid_argument(
                        "VulkanResourceTable missing imported buffer binding for logical resource");
                }
                staged.record.imported = true;
                staged.record.imported_buffer = imported_buffers[resource_.handle.index];
            } else if (resource_.lifetime == ResourceLifetime::persistent &&
                       previous != buffers.end() &&
                       !previous->imported &&
                       BufferDescsEqual(previous->desc, resource_.buffer) &&
                       previous->owned_resource.buffer != VK_NULL_HANDLE) {
                staged.reuse_previous_index = static_cast<std::size_t>(
                    std::distance(buffers.begin(), previous));
            } else if (resource_.lifetime == ResourceLifetime::transient) {
                const auto prepared = FindPreparedTransientBuffer(prepared_transient_buffers, resource_.handle);
                if (prepared == prepared_transient_buffers.end()) {
                    throw std::runtime_error(
                        "VulkanResourceTable missing prepared transient buffer");
                }

                staged.record.owned_resource = prepared->buffer_object;
                prepared->buffer_object = {};

                const auto* allocation_record = FindTransientAllocationRecord(
                    realized_transient_plan,
                    resource_.handle);
                const bool page_backed = allocation_record != nullptr &&
                                         allocation_record->eligible &&
                                         allocation_record->page_index != invalid_render_graph_index;
                if (page_backed) {
                    const auto page_it = FindTransientBufferPageRecord(staged_transient_buffer_pages,
                                                                       allocation_record->page_index);
                    if (page_it == staged_transient_buffer_pages.end()) {
                        throw std::runtime_error(
                            "VulkanResourceTable transient buffer page assignment missing page record");
                    }
                    resource::BufferHost::BindAllocation(staged.record.owned_resource,
                                                         gpu_memory_host_,
                                                         page_it->allocation_slice,
                                                         false,
                                                         allocation_record->page_offset_bytes);
                    staged.record.alias_page_index = page_it->page_index;
                    staged.record.aliased = allocation_record->aliased;
                } else {
                    const bool host_visible_requested =
                        (prepared->create_info.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U;
                    const bool persistent_map =
                        host_visible_requested || prepared->create_info.persistently_mapped;
                    const auto allocation_slice = gpu_memory_host_.AllocateBufferMemory(
                        prepared->memory_requirements,
                        prepared->create_info.memory_properties,
                        prepared->create_info.memory_properties,
                        persistent_map,
                        Center::Memory::Vulkan::LifetimeHint::transient,
                        host_visible_requested
                            ? Center::Memory::Vulkan::HostAccess::sequential_write
                            : Center::Memory::Vulkan::HostAccess::none,
                        prepared->dedicated_required,
                        prepared->dedicated_preferred,
                        staged.record.owned_resource.buffer);
                    resource::BufferHost::BindAllocation(staged.record.owned_resource,
                                                         gpu_memory_host_,
                                                         allocation_slice,
                                                         true,
                                                         0U);
                }
            } else {
                staged.record.owned_resource = resource::BufferHost::CreateBuffer(
                    device_,
                    BuildBufferCreateInfo(resource_.buffer),
                    gpu_memory_host_);
            }

            staged_buffers.push_back(std::move(staged));
        }

        DestroyPreparedTransientBuffers(device_, prepared_transient_buffers);
        DestroyPreparedTransientImages(device_, prepared_transient_images);

        committed_textures = BuildCommittedTextureRecords(staged_textures);
        committed_buffers = BuildCommittedBufferRecords(staged_buffers);
        staged_lazy_memory_resolutions.clear();
        for (auto& slot_ : lazy_memory_resolution_slots) {
            if (!slot_.valid || !slot_.resolution.requested) {
                continue;
            }
            staged_lazy_memory_resolutions.push_back(std::move(slot_.resolution));
        }

        if (g_fail_resolve_before_publish_for_testing) {
            throw std::runtime_error(
                "VulkanResourceTable::Resolve injected failure before publish");
        }

        auto previous_textures = std::move(textures);
        auto previous_buffers = std::move(buffers);
        auto previous_transient_buffer_pages = std::move(transient_buffer_pages);
        auto previous_transient_image_pages = std::move(transient_image_pages);

        AttachReusedTextureRecords(committed_textures, previous_textures);
        AttachReusedBufferRecords(committed_buffers, previous_buffers);

        textures.swap(committed_textures.records);
        buffers.swap(committed_buffers.records);
        transient_buffer_pages.swap(staged_transient_buffer_pages);
        transient_image_pages.swap(staged_transient_image_pages);
        lazy_memory_resolutions.swap(staged_lazy_memory_resolutions);
        compiled_graph_.OverrideTransientPlanning(std::move(realized_transient_plan),
                                                  std::move(realized_barrier_plan));

        DestroyTextureRecords(device_,
                              render_target_host_,
                              previous_textures,
                              last_submitted_value_,
                              completed_submit_value_);
        DestroyBufferRecords(device_, previous_buffers);
        DestroyTransientBufferPages(previous_transient_buffer_pages);
        DestroyTransientImagePages(previous_transient_image_pages);
    } catch (...) {
        DestroyStagedTextureRecords(device_,
                                    render_target_host_,
                                    staged_textures,
                                    last_submitted_value_,
                                    completed_submit_value_);
        DestroyStagedBufferRecords(device_, staged_buffers);
        DestroyTransientBufferPages(staged_transient_buffer_pages);
        DestroyTransientImagePages(staged_transient_image_pages);
        DestroyTextureRecords(device_,
                              render_target_host_,
                              committed_textures.records,
                              last_submitted_value_,
                              completed_submit_value_);
        DestroyBufferRecords(device_, committed_buffers.records);
        DestroyPreparedTransientBuffers(device_, prepared_transient_buffers);
        DestroyPreparedTransientImages(device_, prepared_transient_images);
        throw;
    }

    RefreshStats();
}

void VulkanResourceTable::Shutdown(VulkanContext& device_,
                                   render::RenderTargetHost& render_target_host_,
                                   const std::uint64_t last_submitted_value_,
                                   const std::uint64_t completed_submit_value_) noexcept {
    DestroyAllRecords(device_,
                      render_target_host_,
                      last_submitted_value_,
                      completed_submit_value_);
    imported_textures.clear();
    imported_buffers.clear();
    lazy_memory_resolutions.clear();
    RefreshStats();
}

const PhysicalTextureRecord* VulkanResourceTable::FindTexture(const ResourceHandle logical_) const noexcept {
    const auto existing = std::find_if(textures.begin(),
                                       textures.end(),
                                       [&](const PhysicalTextureRecord& record_) {
                                           return record_.logical.index == logical_.index;
                                       });
    return (existing != textures.end()) ? &(*existing) : nullptr;
}

const PhysicalBufferRecord* VulkanResourceTable::FindBuffer(const ResourceHandle logical_) const noexcept {
    const auto existing = std::find_if(buffers.begin(),
                                       buffers.end(),
                                       [&](const PhysicalBufferRecord& record_) {
                                           return record_.logical.index == logical_.index;
                                       });
    return (existing != buffers.end()) ? &(*existing) : nullptr;
}

const std::vector<PhysicalTextureRecord>& VulkanResourceTable::Textures() const noexcept {
    return textures;
}

const std::vector<PhysicalBufferRecord>& VulkanResourceTable::Buffers() const noexcept {
    return buffers;
}

const std::vector<VulkanLazyMemoryResolution>& VulkanResourceTable::LazyMemoryResolutions() const noexcept {
    return lazy_memory_resolutions;
}

const VulkanResourceTableStats& VulkanResourceTable::Stats() const noexcept {
    return stats;
}

render::RenderTargetDesc VulkanResourceTable::BuildRenderTargetDesc(
    const TextureDesc& desc_) noexcept {
    render::RenderTargetDesc target_desc{};
    target_desc.dimension = static_cast<render::RenderTargetDimension>(desc_.dimension);
    target_desc.lifetime = ResolveRenderTargetLifetime(ResourceLifetime::persistent);
    target_desc.scale_mode = render::RenderTargetScaleMode::absolute;
    target_desc.width = desc_.extent.width;
    target_desc.height = desc_.extent.height;
    target_desc.depth = desc_.extent.depth;
    target_desc.format = ResolveVkFormat(desc_.format);
    target_desc.samples = ResolveSampleCount(desc_.sample_count);
    target_desc.usage = ResolveImageUsage(desc_);
    target_desc.aspect = ResolveImageAspect(desc_);
    target_desc.mip_levels = desc_.mip_level_count;
    target_desc.array_layers = desc_.array_layer_count;
    target_desc.color_encoding = render::RenderTargetColorEncoding::linear;
    // prefer_lazy_memory currently stays as render-graph allocation intent only.
    // Do not lower it into backend memory_policy until RenderTargetHost can
    // realize or explicitly diagnose that policy during actual image allocation.
    target_desc.memory_policy = render::RenderTargetMemoryPolicy::auto_select;
    target_desc.allow_uav = HasTextureUsageFlag(desc_.usage, texture_usage_storage_flag);
    target_desc.allow_alias = desc_.allow_alias;
    return target_desc;
}

resource::BufferCreateInfo VulkanResourceTable::BuildBufferCreateInfo(
    const BufferDesc& desc_) noexcept {
    resource::BufferCreateInfo create_info{};
    create_info.size = desc_.size_bytes;
    create_info.usage = ResolveBufferUsage(desc_);
    create_info.memory_properties = desc_.host_visible
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    create_info.persistently_mapped = desc_.persistently_mapped;
    return create_info;
}

resource::ImageCreateInfo VulkanResourceTable::BuildImageCreateInfo(
    const TextureDesc& desc_) noexcept {
    resource::ImageCreateInfo create_info{};
    create_info.image_type = ResolveImageType(desc_.dimension);
    create_info.format = ResolveVkFormat(desc_.format);
    create_info.extent = VkExtent3D{desc_.extent.width, desc_.extent.height, desc_.extent.depth};
    create_info.mip_levels = desc_.mip_level_count;
    create_info.array_layers = desc_.array_layer_count;
    create_info.samples = ResolveSampleCount(desc_.sample_count);
    create_info.usage = ResolveImageUsage(desc_);
    if (IsLazyMemoryUsageEligible(desc_)) {
        create_info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    }
    create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    create_info.create_default_view = true;
    create_info.default_view_type = ResolveImageViewType(desc_);
    create_info.default_view_aspect = ResolveImageAspect(desc_);
    return create_info;
}

void VulkanResourceTable::DestroyTransientRecords(VulkanContext& device_,
                                                  render::RenderTargetHost& render_target_host_,
                                                  const std::uint64_t last_submitted_value_,
                                                  const std::uint64_t completed_submit_value_) noexcept {
    auto texture_it = textures.begin();
    while (texture_it != textures.end()) {
        if (texture_it->lifetime == ResourceLifetime::transient &&
            !texture_it->imported &&
            render::IsValidRenderTargetHandle(texture_it->render_target)) {
            (void)render_target_host_.DestroyTarget(device_,
                                                    texture_it->render_target,
                                                    last_submitted_value_,
                                                    completed_submit_value_);
            texture_it->render_target = render::invalid_render_target_handle;
            if (texture_it->owned_resource.image != VK_NULL_HANDLE) {
                retired_transient_images.Retire(
                    RetiredTransientImagePayload{
                        .owned_resource = std::move(texture_it->owned_resource),
                    },
                    last_submitted_value_);
            }
            texture_it = textures.erase(texture_it);
            continue;
        }
        ++texture_it;
    }

    auto buffer_it = buffers.begin();
    while (buffer_it != buffers.end()) {
        if (buffer_it->lifetime == ResourceLifetime::transient &&
            !buffer_it->imported &&
            buffer_it->owned_resource.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(device_, buffer_it->owned_resource);
            buffer_it = buffers.erase(buffer_it);
            continue;
        }
        ++buffer_it;
    }

    DestroyTransientBufferPages(transient_buffer_pages);
    for (auto& page_ : transient_image_pages) {
        if (page_.allocation_slice.valid()) {
            retired_transient_image_pages.Retire(std::move(page_), last_submitted_value_);
        }
    }
    transient_image_pages.clear();
}

void VulkanResourceTable::DestroyAllRecords(VulkanContext& device_,
                                            render::RenderTargetHost& render_target_host_,
                                            const std::uint64_t last_submitted_value_,
                                            const std::uint64_t completed_submit_value_) noexcept {
    for (auto& texture_ : textures) {
        if (!texture_.imported && render::IsValidRenderTargetHandle(texture_.render_target)) {
            (void)render_target_host_.DestroyTarget(device_,
                                                    texture_.render_target,
                                                    last_submitted_value_,
                                                    completed_submit_value_);
            texture_.render_target = render::invalid_render_target_handle;
        }
        if (texture_.owned_resource.image != VK_NULL_HANDLE) {
            resource::ImageHost::DestroyImage(device_, texture_.owned_resource);
        }
    }
    textures.clear();

    for (auto& buffer_ : buffers) {
        if (!buffer_.imported && buffer_.owned_resource.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(device_, buffer_.owned_resource);
            buffer_.owned_resource = {};
        }
    }
    buffers.clear();
    DestroyTransientBufferPages(transient_buffer_pages);
    DestroyTransientImagePages(transient_image_pages);
    (void)retired_transient_images.Flush([&](RetiredTransientImagePayload& payload_) {
        if (payload_.owned_resource.image != VK_NULL_HANDLE) {
            resource::ImageHost::DestroyImage(device_, payload_.owned_resource);
        }
    });
    (void)retired_transient_image_pages.Flush([&](TransientImagePageRecord& page_) {
        if (page_.memory_host != nullptr && page_.allocation_slice.valid()) {
            page_.memory_host->Deallocate(page_.allocation_slice);
            page_.allocation_slice = {};
        }
        page_.memory_host = nullptr;
    });
}

void VulkanResourceTable::RefreshStats() noexcept {
    stats = {};
    for (const auto& texture_ : textures) {
        if (texture_.imported) {
            stats.imported_texture_count += 1U;
        } else if (texture_.lifetime == ResourceLifetime::persistent) {
            stats.persistent_texture_count += 1U;
        } else if (texture_.lifetime == ResourceLifetime::transient) {
            stats.transient_texture_count += 1U;
            stats.transient_aliased_texture_count += texture_.aliased ? 1U : 0U;
        }
    }
    for (const auto& buffer_ : buffers) {
        if (buffer_.imported) {
            stats.imported_buffer_count += 1U;
        } else if (buffer_.lifetime == ResourceLifetime::persistent) {
            stats.persistent_buffer_count += 1U;
        } else if (buffer_.lifetime == ResourceLifetime::transient) {
            stats.transient_buffer_count += 1U;
            stats.transient_aliased_buffer_count += buffer_.aliased ? 1U : 0U;
        }
    }
    stats.transient_buffer_page_count = static_cast<std::uint32_t>(transient_buffer_pages.size());
    stats.transient_image_page_count = static_cast<std::uint32_t>(transient_image_pages.size());
    for (const auto& resolution_ : lazy_memory_resolutions) {
        if (!resolution_.requested) {
            continue;
        }
        stats.lazy_memory_requested_count += 1U;
        if (resolution_.realized) {
            stats.lazy_memory_realized_count += 1U;
        } else {
            stats.lazy_memory_unavailable_count += 1U;
        }
    }
}

} // namespace vr::render_graph
