#include "render_graph_builder_internal.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace vr::render_graph::builder_detail {

[[nodiscard]] std::string BuildVersionDebugName(const std::string& base_name_,
                                                const std::uint32_t version_) {
    std::ostringstream oss{};
    oss << base_name_ << "#v" << version_;
    return oss.str();
}

[[nodiscard]] const char* ResourceKindToString(const ResourceKind kind_) noexcept {
    switch (kind_) {
    case ResourceKind::texture:
        return "texture";
    case ResourceKind::buffer:
        return "buffer";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] const char* ResourceLifetimeToString(const ResourceLifetime lifetime_) noexcept {
    switch (lifetime_) {
    case ResourceLifetime::imported:
        return "imported";
    case ResourceLifetime::persistent:
        return "persistent";
    case ResourceLifetime::transient:
        return "transient";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] const char* QueueClassToString(const QueueClass queue_) noexcept {
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

[[nodiscard]] const char* DescriptorBindingSourceToString(
    const DescriptorBindingSource source_) noexcept {
    switch (source_) {
    case DescriptorBindingSource::none:
        return "none";
    case DescriptorBindingSource::bindless_table:
        return "bindless_table";
    case DescriptorBindingSource::external_buffer:
        return "external_buffer";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] const char* DescriptorBindingKindToString(
    const DescriptorBindingKind kind_) noexcept {
    switch (kind_) {
    case DescriptorBindingKind::sampled_image_table:
        return "sampled_image_table";
    case DescriptorBindingKind::sampler_table:
        return "sampler_table";
    case DescriptorBindingKind::storage_buffer:
        return "storage_buffer";
    case DescriptorBindingKind::uniform_buffer:
        return "uniform_buffer";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] std::string BuildShaderStageFlagsString(
    const std::uint32_t stage_flags_) {
    std::ostringstream oss{};
    bool first = true;
    const auto append = [&](const char* label_) {
        if (!first) {
            oss << '|';
        }
        oss << label_;
        first = false;
    };
    if (HasShaderStageFlag(stage_flags_, shader_stage_vertex_flag)) {
        append("vertex");
    }
    if (HasShaderStageFlag(stage_flags_, shader_stage_fragment_flag)) {
        append("fragment");
    }
    if (HasShaderStageFlag(stage_flags_, shader_stage_compute_flag)) {
        append("compute");
    }
    if (first) {
        return "none";
    }
    return oss.str();
}

[[nodiscard]] const char* AccessKindToString(const AccessKind access_) noexcept {
    switch (access_) {
    case AccessKind::none:
        return "none";
    case AccessKind::color_attachment_read:
        return "color_attachment_read";
    case AccessKind::color_attachment_write:
        return "color_attachment_write";
    case AccessKind::depth_stencil_read:
        return "depth_stencil_read";
    case AccessKind::depth_stencil_write:
        return "depth_stencil_write";
    case AccessKind::depth_stencil_read_write:
        return "depth_stencil_read_write";
    case AccessKind::shader_sample_read:
        return "shader_sample_read";
    case AccessKind::shader_storage_read:
        return "shader_storage_read";
    case AccessKind::shader_storage_write:
        return "shader_storage_write";
    case AccessKind::shader_storage_read_write:
        return "shader_storage_read_write";
    case AccessKind::uniform_read:
        return "uniform_read";
    case AccessKind::vertex_buffer_read:
        return "vertex_buffer_read";
    case AccessKind::index_buffer_read:
        return "index_buffer_read";
    case AccessKind::indirect_command_read:
        return "indirect_command_read";
    case AccessKind::transfer_read:
        return "transfer_read";
    case AccessKind::transfer_write:
        return "transfer_write";
    case AccessKind::present:
        return "present";
    case AccessKind::host_read:
        return "host_read";
    case AccessKind::host_write:
        return "host_write";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] std::string EscapeJsonString(const std::string& value_) {
    std::ostringstream oss{};
    for (const char character_ : value_) {
        switch (character_) {
        case '\\':
            oss << "\\\\";
            break;
        case '"':
            oss << "\\\"";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            oss << character_;
            break;
        }
    }
    return oss.str();
}

[[nodiscard]] std::string EscapeDotLabel(const std::string& value_) {
    std::ostringstream oss{};
    for (const char character_ : value_) {
        switch (character_) {
        case '\\':
            oss << "\\\\";
            break;
        case '"':
            oss << "\\\"";
            break;
        case '\n':
            oss << "\\n";
            break;
        default:
            oss << character_;
            break;
        }
    }
    return oss.str();
}

[[nodiscard]] std::string MakeResourceNodeId(const ResourceVersionHandle version_) {
    std::ostringstream oss{};
    oss << "resource_" << version_.resource_index << "_v" << version_.version;
    return oss.str();
}

[[nodiscard]] const CompiledResourceVersionLiveness* FindLivenessRange(
    const std::vector<CompiledResourceVersionLiveness>& liveness_ranges_,
    const ResourceVersionHandle version_) {
    for (const auto& range_ : liveness_ranges_) {
        if (range_.version.resource_index == version_.resource_index &&
            range_.version.version == version_.version) {
            return &range_;
        }
    }
    return nullptr;
}

[[nodiscard]] bool HasExplicitSubresourceRange(const SubresourceRange& range_) noexcept {
    return range_.level_count != 0U || range_.layer_count != 0U;
}

[[nodiscard]] bool HasExplicitBufferRange(const BufferRange& range_) noexcept {
    return range_.offset_bytes != 0U || range_.size_bytes != 0U;
}

[[nodiscard]] AccessDesc BindAccessDesc(const ResourceVersionHandle version_,
                                        const ResourceKind kind_,
                                        const AccessDesc& access_) noexcept {
    AccessDesc bound = access_;
    bound.resource = version_;
    if (kind_ == ResourceKind::texture) {
        bound.buffer_range = {};
    } else {
        bound.subresource_range = {};
    }
    return bound;
}

void AppendJsonAccessDesc(std::ostringstream& oss_,
                          const AccessDesc& access_,
                          const ResourceKind kind_) {
    oss_ << "{\"resourceIndex\": " << access_.resource.resource_index
         << ", \"version\": " << access_.resource.version
         << ", \"access\": \"" << AccessKindToString(access_.access) << "\"";
    if (kind_ == ResourceKind::texture) {
        oss_ << ", \"subresourceRange\": {"
             << "\"baseMipLevel\": " << access_.subresource_range.base_mip_level
             << ", \"levelCount\": " << access_.subresource_range.level_count
             << ", \"baseArrayLayer\": " << access_.subresource_range.base_array_layer
             << ", \"layerCount\": " << access_.subresource_range.layer_count << "}";
    } else {
        oss_ << ", \"bufferRange\": {"
             << "\"offsetBytes\": " << access_.buffer_range.offset_bytes
             << ", \"sizeBytes\": " << access_.buffer_range.size_bytes << "}";
    }
    oss_ << '}';
}

void AppendJsonDescriptorBinding(std::ostringstream& oss_,
                                 const PassDescriptorBindingDesc& binding_) {
    oss_ << "{\"set\": " << binding_.set
         << ", \"binding\": " << binding_.binding
         << ", \"source\": \"" << DescriptorBindingSourceToString(binding_.source) << '"'
         << ", \"kind\": \"" << DescriptorBindingKindToString(binding_.kind) << '"'
         << ", \"stageFlags\": \"" << BuildShaderStageFlagsString(binding_.stage_flags) << '"'
         << ", \"sourceId\": " << binding_.source_id << '}';
}

void AppendJsonDescriptorWrite(std::ostringstream& oss_,
                               const DescriptorWriteDesc& write_) {
    oss_ << "{\"set\": " << write_.set
         << ", \"binding\": " << write_.binding
         << ", \"source\": \"" << DescriptorBindingSourceToString(write_.source) << '"'
         << ", \"kind\": \"" << DescriptorBindingKindToString(write_.kind) << '"'
         << ", \"stageFlags\": \"" << BuildShaderStageFlagsString(write_.stage_flags) << '"'
         << ", \"sourceId\": " << write_.source_id << '}';
}

void AppendJsonBindlessAllocation(std::ostringstream& oss_,
                                  const BindlessAllocation& allocation_) {
    oss_ << "{\"tableId\": " << allocation_.table_id
         << ", \"kind\": \"" << DescriptorBindingKindToString(allocation_.kind) << '"'
         << ", \"stageFlags\": \"" << BuildShaderStageFlagsString(allocation_.stage_flags) << "\"}";
}

[[nodiscard]] std::string BuildDescriptorLayoutDotLabel(const PassDescriptorBindingDesc& binding_) {
    std::ostringstream oss{};
    oss << "set " << binding_.set
        << " binding " << binding_.binding
        << "\\n" << DescriptorBindingKindToString(binding_.kind);
    return oss.str();
}

[[nodiscard]] std::string BuildAccessDotLabel(const AccessDesc& access_,
                                              const ResourceKind kind_) {
    std::ostringstream oss{};
    oss << AccessKindToString(access_.access);
    if (kind_ == ResourceKind::texture && HasExplicitSubresourceRange(access_.subresource_range)) {
        oss << "\\nmip=" << access_.subresource_range.base_mip_level
            << "+" << access_.subresource_range.level_count
            << " layer=" << access_.subresource_range.base_array_layer
            << "+" << access_.subresource_range.layer_count;
    }
    if (kind_ == ResourceKind::buffer && HasExplicitBufferRange(access_.buffer_range)) {
        oss << "\\noffset=" << access_.buffer_range.offset_bytes
            << " size=" << access_.buffer_range.size_bytes;
    }
    return oss.str();
}

void AppendTransientAllocationJson(std::ostringstream& oss_,
                                   const TransientAllocationPlan& plan_) {
    oss_ << "{\n";
    oss_ << "    \"records\": [";
    for (std::size_t record_index = 0; record_index < plan_.records.size(); ++record_index) {
        if (record_index != 0U) {
            oss_ << ", ";
        }
        const auto& record_ = plan_.records[record_index];
        oss_ << "{\"resourceIndex\": " << record_.resource.index
             << ", \"name\": \"" << EscapeJsonString(record_.debug_name) << "\""
             << ", \"kind\": \"" << ResourceKindToString(record_.kind) << "\""
             << ", \"lifetime\": \"" << ResourceLifetimeToString(record_.lifetime) << "\""
             << ", \"eligible\": " << (record_.eligible ? "true" : "false")
             << ", \"firstPassOrder\": " << record_.first_pass_order
             << ", \"lastPassOrder\": " << record_.last_pass_order
             << ", \"pageIndex\": " << record_.page_index
             << ", \"pageOffsetBytes\": " << record_.page_offset_bytes
             << ", \"aliasGroup\": " << record_.alias_group
             << ", \"aliased\": " << (record_.aliased ? "true" : "false")
             << ", \"footprint\": {"
             << "\"sizeBytes\": " << record_.footprint.size_bytes
             << ", \"alignmentBytes\": " << record_.footprint.alignment_bytes
             << ", \"memoryTypeBits\": " << record_.footprint.memory_type_bits
             << "}"
             << ", \"rejectionReason\": \"" << EscapeJsonString(record_.rejection_reason) << "\"}";
    }
    oss_ << "],\n";

    oss_ << "    \"pages\": [";
    for (std::size_t page_index = 0; page_index < plan_.pages.size(); ++page_index) {
        if (page_index != 0U) {
            oss_ << ", ";
        }
        const auto& page_ = plan_.pages[page_index];
        oss_ << "{\"pageIndex\": " << page_.page_index
             << ", \"kind\": \"" << ResourceKindToString(page_.kind) << "\""
             << ", \"sizeBytes\": " << page_.size_bytes
             << ", \"alignmentBytes\": " << page_.alignment_bytes
             << ", \"resources\": [";
        for (std::size_t resource_index = 0; resource_index < page_.resources.size(); ++resource_index) {
            if (resource_index != 0U) {
                oss_ << ", ";
            }
            oss_ << page_.resources[resource_index].index;
        }
        oss_ << "]}";
    }
    oss_ << "],\n";

    oss_ << "    \"timeline\": {\n";
    oss_ << "      \"logicalTotalBytes\": " << plan_.timeline.logical_total_bytes << ",\n";
    oss_ << "      \"physicalTotalBytes\": " << plan_.timeline.physical_total_bytes << ",\n";
    oss_ << "      \"peakLogicalLiveBytes\": " << plan_.timeline.peak_logical_live_bytes << ",\n";
    oss_ << "      \"peakLiveBytes\": " << plan_.timeline.peak_live_bytes << ",\n";
    oss_ << "      \"savedBytes\": " << plan_.timeline.saved_bytes << ",\n";
    oss_ << "      \"transientResourceCount\": " << plan_.timeline.transient_resource_count << ",\n";
    oss_ << "      \"eligibleResourceCount\": " << plan_.timeline.eligible_resource_count << ",\n";
    oss_ << "      \"aliasedResourceCount\": " << plan_.timeline.aliased_resource_count << ",\n";
    oss_ << "      \"pageCount\": " << plan_.timeline.page_count << ",\n";
    oss_ << "      \"aliasBarrierCount\": " << plan_.timeline.alias_barrier_count << ",\n";
    oss_ << "      \"samples\": [";
    for (std::size_t sample_index = 0; sample_index < plan_.timeline.samples.size(); ++sample_index) {
        if (sample_index != 0U) {
            oss_ << ", ";
        }
        const auto& sample_ = plan_.timeline.samples[sample_index];
        oss_ << "{\"passOrder\": " << sample_.pass_order
             << ", \"logicalLiveBytes\": " << sample_.logical_live_bytes
             << ", \"physicalLiveBytes\": " << sample_.physical_live_bytes
             << ", \"livePageCount\": " << sample_.live_page_count << "}";
    }
    oss_ << "]\n";
    oss_ << "    },\n";

    oss_ << "    \"aliasCandidates\": [";
    for (std::size_t candidate_index = 0; candidate_index < plan_.alias_candidates.size(); ++candidate_index) {
        if (candidate_index != 0U) {
            oss_ << ", ";
        }
        const auto& candidate_ = plan_.alias_candidates[candidate_index];
        oss_ << "{\"firstResourceIndex\": " << candidate_.first.index
             << ", \"secondResourceIndex\": " << candidate_.second.index
             << ", \"aliasable\": " << (candidate_.aliasable ? "true" : "false")
             << ", \"sameCompatibilityClass\": " << (candidate_.same_compatibility_class ? "true" : "false")
             << ", \"overlappingLiveness\": " << (candidate_.overlapping_liveness ? "true" : "false")
             << ", \"nonAliasReason\": \"" << EscapeJsonString(candidate_.non_alias_reason) << "\"}";
    }
    oss_ << "],\n";

    oss_ << "    \"aliasBarriers\": [";
    for (std::size_t barrier_index = 0; barrier_index < plan_.alias_barriers.size(); ++barrier_index) {
        if (barrier_index != 0U) {
            oss_ << ", ";
        }
        const auto& barrier_ = plan_.alias_barriers[barrier_index];
        oss_ << "{\"previousResourceIndex\": " << barrier_.previous.index
             << ", \"nextResourceIndex\": " << barrier_.next.index
             << ", \"pageIndex\": " << barrier_.page_index
             << ", \"previousLastPassOrder\": " << barrier_.previous_last_pass_order
             << ", \"nextFirstPassOrder\": " << barrier_.next_first_pass_order
             << ", \"required\": " << (barrier_.required ? "true" : "false")
             << ", \"realized\": " << (barrier_.realized ? "true" : "false") << "}";
    }
    oss_ << "]\n";
    oss_ << "  }";
}

} // namespace vr::render_graph::builder_detail

namespace vr::render_graph {
const CompiledPass* CompiledRenderGraph::FindPass(const PassHandle handle_) const noexcept {
    for (const auto& pass_ : passes) {
        if (pass_.handle.index == handle_.index) {
            return &pass_;
        }
    }
    return nullptr;
}

const CompiledResource* CompiledRenderGraph::FindResource(const ResourceHandle handle_) const noexcept {
    for (const auto& resource_ : resources) {
        if (resource_.handle.index == handle_.index) {
            return &resource_;
        }
    }
    return nullptr;
}

void RenderGraphBuilder::Reset() noexcept {
    resources.clear();
    passes.clear();
    external_buffer_binding_resolvers.clear();
}

RenderGraphBuilder::ResourceNode& RenderGraphBuilder::RequireResource(
    const ResourceHandle handle_) {
    if (!IsValidResourceHandle(handle_) || handle_.index >= resources.size()) {
        throw std::out_of_range("RenderGraphBuilder resource handle is out of range");
    }
    return resources[handle_.index];
}

const RenderGraphBuilder::ResourceNode& RenderGraphBuilder::RequireResource(
    const ResourceHandle handle_) const {
    if (!IsValidResourceHandle(handle_) || handle_.index >= resources.size()) {
        throw std::out_of_range("RenderGraphBuilder resource handle is out of range");
    }
    return resources[handle_.index];
}

const RenderGraphBuilder::ResourceNode& RenderGraphBuilder::RequireVersion(
    const ResourceVersionHandle handle_) const {
    if (!IsValidResourceVersionHandle(handle_) ||
        handle_.resource_index >= resources.size()) {
        throw std::out_of_range("RenderGraphBuilder resource version handle is out of range");
    }
    const auto& resource = resources[handle_.resource_index];
    if (handle_.version >= resource.versions.size()) {
        throw std::out_of_range("RenderGraphBuilder resource version is out of range");
    }
    return resource;
}

RenderGraphBuilder::PassNode& RenderGraphBuilder::RequirePass(const PassHandle handle_) {
    if (!IsValidPassHandle(handle_) || handle_.index >= passes.size()) {
        throw std::out_of_range("RenderGraphBuilder pass handle is out of range");
    }
    return passes[handle_.index];
}

const RenderGraphBuilder::PassNode& RenderGraphBuilder::RequirePass(
    const PassHandle handle_) const {
    if (!IsValidPassHandle(handle_) || handle_.index >= passes.size()) {
        throw std::out_of_range("RenderGraphBuilder pass handle is out of range");
    }
    return passes[handle_.index];
}

void RenderGraphBuilder::AppendUnique(std::vector<PassHandle>& values_,
                                      const PassHandle value_) {
    const auto existing = std::find_if(values_.begin(),
                                       values_.end(),
                                       [&](const PassHandle current_) {
                                           return current_.index == value_.index;
                                       });
    if (existing == values_.end()) {
        values_.push_back(value_);
    }
}

void RenderGraphBuilder::AppendUnique(std::vector<AccessDesc>& values_,
                                      const AccessDesc& value_) {
    const auto existing = std::find_if(values_.begin(),
                                       values_.end(),
                                       [&](const AccessDesc& current_) {
                                           return current_.resource.resource_index == value_.resource.resource_index &&
                                                  current_.resource.version == value_.resource.version &&
                                                  current_.access == value_.access &&
                                                  current_.subresource_range.base_mip_level == value_.subresource_range.base_mip_level &&
                                                  current_.subresource_range.level_count == value_.subresource_range.level_count &&
                                                  current_.subresource_range.base_array_layer == value_.subresource_range.base_array_layer &&
                                                  current_.subresource_range.layer_count == value_.subresource_range.layer_count &&
                                                  current_.buffer_range.offset_bytes == value_.buffer_range.offset_bytes &&
                                                  current_.buffer_range.size_bytes == value_.buffer_range.size_bytes;
                                       });
    if (existing == values_.end()) {
        values_.push_back(value_);
    }
}

} // namespace vr::render_graph
