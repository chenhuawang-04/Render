#include "vr/render_graph/barrier_plan.hpp"

#include "vr/render_graph/compiled_render_graph.hpp"

#include <algorithm>
#include <sstream>

namespace vr::render_graph {
namespace {

struct LastAccessState final {
    AccessDesc access{};
    QueueClass queue = QueueClass::graphics;
    PassHandle pass{};
    std::uint32_t pass_order = invalid_render_graph_index;
    bool valid = false;
};

struct ResourceAggregateLiveness final {
    std::uint32_t first_pass_order = invalid_render_graph_index;
    std::uint32_t last_pass_order = invalid_render_graph_index;
    bool valid = false;
};

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

[[nodiscard]] bool IsWriteAccess(const AccessKind access_) noexcept {
    switch (access_) {
    case AccessKind::color_attachment_write:
    case AccessKind::depth_stencil_write:
    case AccessKind::depth_stencil_read_write:
    case AccessKind::shader_storage_write:
    case AccessKind::shader_storage_read_write:
    case AccessKind::transfer_write:
    case AccessKind::present:
    case AccessKind::host_write:
        return true;
    default:
        break;
    }
    return false;
}

[[nodiscard]] bool IsStorageAccess(const AccessKind access_) noexcept {
    switch (access_) {
    case AccessKind::shader_storage_read:
    case AccessKind::shader_storage_write:
    case AccessKind::shader_storage_read_write:
        return true;
    default:
        break;
    }
    return false;
}

[[nodiscard]] bool HasStorageWrite(const AccessKind access_) noexcept {
    return access_ == AccessKind::shader_storage_write ||
           access_ == AccessKind::shader_storage_read_write;
}

[[nodiscard]] bool IsHostAccess(const AccessKind access_) noexcept {
    return access_ == AccessKind::host_read || access_ == AccessKind::host_write;
}

[[nodiscard]] bool IntervalsOverlap(const std::uint64_t lhs_begin_,
                                    const std::uint64_t lhs_end_,
                                    const std::uint64_t rhs_begin_,
                                    const std::uint64_t rhs_end_) noexcept {
    return lhs_begin_ < rhs_end_ && rhs_begin_ < lhs_end_;
}

[[nodiscard]] bool TextureRangesOverlap(const SubresourceRange& lhs_,
                                        const SubresourceRange& rhs_) noexcept {
    if (lhs_.level_count == 0U || lhs_.layer_count == 0U ||
        rhs_.level_count == 0U || rhs_.layer_count == 0U) {
        return true;
    }

    const auto lhs_mip_end = static_cast<std::uint64_t>(lhs_.base_mip_level) + lhs_.level_count;
    const auto rhs_mip_end = static_cast<std::uint64_t>(rhs_.base_mip_level) + rhs_.level_count;
    const auto lhs_layer_end = static_cast<std::uint64_t>(lhs_.base_array_layer) + lhs_.layer_count;
    const auto rhs_layer_end = static_cast<std::uint64_t>(rhs_.base_array_layer) + rhs_.layer_count;
    return IntervalsOverlap(lhs_.base_mip_level, lhs_mip_end,
                            rhs_.base_mip_level, rhs_mip_end) &&
           IntervalsOverlap(lhs_.base_array_layer, lhs_layer_end,
                            rhs_.base_array_layer, rhs_layer_end);
}

[[nodiscard]] bool BufferRangesOverlap(const BufferRange& lhs_,
                                       const BufferRange& rhs_) noexcept {
    if (lhs_.size_bytes == 0U || rhs_.size_bytes == 0U) {
        return true;
    }

    const auto lhs_end = lhs_.offset_bytes + lhs_.size_bytes;
    const auto rhs_end = rhs_.offset_bytes + rhs_.size_bytes;
    return IntervalsOverlap(lhs_.offset_bytes, lhs_end, rhs_.offset_bytes, rhs_end);
}

[[nodiscard]] bool RangesOverlap(const ResourceKind kind_,
                                 const AccessDesc& lhs_,
                                 const AccessDesc& rhs_) noexcept {
    if (kind_ == ResourceKind::texture) {
        return TextureRangesOverlap(lhs_.subresource_range, rhs_.subresource_range);
    }
    return BufferRangesOverlap(lhs_.buffer_range, rhs_.buffer_range);
}

[[nodiscard]] bool RequiresBarrier(const LastAccessState& previous_,
                                   const AccessDesc& current_,
                                   const QueueClass current_queue_,
                                   const ResourceKind kind_) noexcept {
    if (!previous_.valid ||
        previous_.access.access == AccessKind::none ||
        current_.access == AccessKind::none) {
        return false;
    }
    if (!RangesOverlap(kind_, previous_.access, current_)) {
        return false;
    }
    if (previous_.queue != current_queue_) {
        return true;
    }
    if (previous_.access.access != current_.access) {
        return true;
    }
    return IsWriteAccess(previous_.access.access) ||
           IsWriteAccess(current_.access) ||
           IsHostAccess(previous_.access.access) ||
           IsHostAccess(current_.access);
}

[[nodiscard]] ResourceKind ResolveResourceKind(const CompiledRenderGraph& compiled_graph_,
                                               const std::uint32_t resource_index_) {
    if (const auto* resource_ = compiled_graph_.FindResource(ResourceHandle{
            .index = resource_index_,
            .generation = 1U,
        });
        resource_ != nullptr) {
        return resource_->kind;
    }
    return ResourceKind::buffer;
}

[[nodiscard]] bool PassHasHostBoundary(const CompiledPass& pass_) noexcept {
    for (const auto& read_ : pass_.reads) {
        if (IsHostAccess(read_.access)) {
            return true;
        }
    }
    for (const auto& write_ : pass_.writes) {
        if (IsHostAccess(write_.access)) {
            return true;
        }
    }
    return false;
}

void AppendUniqueResourceVersion(std::vector<ResourceVersionHandle>& values_,
                                 const ResourceVersionHandle value_) {
    const auto existing = std::find_if(values_.begin(),
                                       values_.end(),
                                       [&](const ResourceVersionHandle current_) {
                                           return current_.resource_index == value_.resource_index &&
                                                  current_.version == value_.version;
                                       });
    if (existing == values_.end()) {
        values_.push_back(value_);
    }
}

void AppendUniqueIndex(std::vector<std::uint32_t>& values_,
                       const std::uint32_t value_) {
    if (std::find(values_.begin(), values_.end(), value_) == values_.end()) {
        values_.push_back(value_);
    }
}

[[nodiscard]] bool TextureCompatibilityClassMatches(const CompiledResource& lhs_,
                                                    const CompiledResource& rhs_) noexcept {
    return lhs_.texture.dimension == rhs_.texture.dimension &&
           lhs_.texture.format == rhs_.texture.format &&
           lhs_.texture.extent.width == rhs_.texture.extent.width &&
           lhs_.texture.extent.height == rhs_.texture.extent.height &&
           lhs_.texture.extent.depth == rhs_.texture.extent.depth &&
           lhs_.texture.mip_level_count == rhs_.texture.mip_level_count &&
           lhs_.texture.array_layer_count == rhs_.texture.array_layer_count &&
           lhs_.texture.sample_count == rhs_.texture.sample_count;
}

[[nodiscard]] bool BufferCompatibilityClassMatches(const CompiledResource& lhs_,
                                                   const CompiledResource& rhs_) noexcept {
    return lhs_.buffer.size_bytes == rhs_.buffer.size_bytes;
}

[[nodiscard]] bool CompatibilityClassMatches(const CompiledResource& lhs_,
                                             const CompiledResource& rhs_) noexcept {
    if (lhs_.kind != rhs_.kind) {
        return false;
    }
    if (lhs_.kind == ResourceKind::texture) {
        return TextureCompatibilityClassMatches(lhs_, rhs_);
    }
    return BufferCompatibilityClassMatches(lhs_, rhs_);
}

[[nodiscard]] bool ResourceAllowsAlias(const CompiledResource& resource_) noexcept {
    if (resource_.kind == ResourceKind::texture) {
        return resource_.texture.allow_alias;
    }
    return resource_.buffer.allow_alias;
}

[[nodiscard]] ResourceAggregateLiveness ResolveAggregateLiveness(
    const CompiledRenderGraph& compiled_graph_,
    const ResourceHandle resource_) noexcept {
    ResourceAggregateLiveness aggregate{};
    for (const auto& range_ : compiled_graph_.LivenessRanges()) {
        if (range_.version.resource_index != resource_.index) {
            continue;
        }
        if (!aggregate.valid) {
            aggregate.first_pass_order = range_.first_pass_order;
            aggregate.last_pass_order = range_.last_pass_order;
            aggregate.valid = true;
            continue;
        }
        aggregate.first_pass_order = (std::min)(aggregate.first_pass_order, range_.first_pass_order);
        aggregate.last_pass_order = (std::max)(aggregate.last_pass_order, range_.last_pass_order);
    }
    return aggregate;
}

[[nodiscard]] bool LivenessOverlaps(const ResourceAggregateLiveness& lhs_,
                                    const ResourceAggregateLiveness& rhs_) noexcept {
    if (!lhs_.valid || !rhs_.valid) {
        return false;
    }
    return !(lhs_.last_pass_order < rhs_.first_pass_order ||
             rhs_.last_pass_order < lhs_.first_pass_order);
}

} // namespace

std::string BarrierPlan::BuildDebugString() const {
    std::ostringstream oss{};
    oss << "queue_batches=" << queue_batches.size() << '\n';
    for (std::size_t batch_index = 0; batch_index < queue_batches.size(); ++batch_index) {
        const auto& queue_batch_ = queue_batches[batch_index];
        oss << "batch=" << batch_index
            << " queue=" << QueueClassToString(queue_batch_.queue)
            << " passes=";
        for (std::size_t index = 0; index < queue_batch_.passes.size(); ++index) {
            if (index != 0U) {
                oss << ',';
            }
            oss << queue_batch_.passes[index].index;
        }
        oss << " waits=" << queue_batch_.wait_dependency_indices.size()
            << " signals=" << queue_batch_.signal_dependency_indices.size()
            << " barriers=" << queue_batch_.barrier_batch_indices.size();
        if (queue_batch_.contains_host_boundary) {
            oss << " host_boundary";
        }
        oss << '\n';
    }

    oss << "queue_dependencies=" << queue_dependencies.size() << '\n';
    for (std::size_t dependency_index = 0; dependency_index < queue_dependencies.size(); ++dependency_index) {
        const auto& dependency_ = queue_dependencies[dependency_index];
        oss << "dependency=" << dependency_index
            << " " << QueueClassToString(dependency_.source_queue)
            << "[" << dependency_.source_batch_index << "] -> "
            << QueueClassToString(dependency_.target_queue)
            << "[" << dependency_.target_batch_index << "]"
            << " resources=" << dependency_.resources.size()
            << " queue_transfer=" << (dependency_.queue_transfer ? 1 : 0)
            << " host_boundary=" << (dependency_.host_boundary ? 1 : 0) << '\n';
    }

    oss << "barrier_batches=" << barrier_batches.size() << '\n';
    for (const auto& batch_ : barrier_batches) {
        oss << "pass=" << batch_.pass.index
            << " queue=" << QueueClassToString(batch_.queue)
            << " barriers=" << batch_.barriers.size() << '\n';
        for (const auto& barrier_ : batch_.barriers) {
            oss << "  resource=" << barrier_.resource.resource_index << "#v" << barrier_.resource.version
                << " before=" << AccessKindToString(barrier_.before)
                << " after=" << AccessKindToString(barrier_.after)
                << " queues=" << QueueClassToString(barrier_.src_queue)
                << "->" << QueueClassToString(barrier_.dst_queue)
                << " queue_transfer=" << (barrier_.queue_transfer ? 1 : 0)
                << " host_boundary=" << (barrier_.host_boundary ? 1 : 0)
                << " uav=" << (barrier_.uav_ordering ? 1 : 0) << '\n';
        }
    }

    oss << "alias_candidates=" << alias_candidates.size() << '\n';
    for (const auto& candidate_ : alias_candidates) {
        oss << "alias_candidate=" << candidate_.first_debug_name
            << " <-> " << candidate_.second_debug_name
            << " kind=" << ResourceKindToString(candidate_.kind)
            << " same_class=" << (candidate_.same_compatibility_class ? 1 : 0)
            << " overlap=" << (candidate_.overlapping_liveness ? 1 : 0)
            << " aliasable=" << (candidate_.aliasable ? 1 : 0) << '\n';
    }

    oss << "alias_barriers=" << alias_barriers.size() << '\n';
    for (const auto& alias_barrier_ : alias_barriers) {
        oss << "alias_barrier=" << alias_barrier_.previous_debug_name
            << " -> " << alias_barrier_.next_debug_name
            << " required=" << (alias_barrier_.required ? 1 : 0)
            << " realized=" << (alias_barrier_.realized ? 1 : 0) << '\n';
    }
    return oss.str();
}

std::string BarrierPlan::BuildJson() const {
    std::ostringstream oss{};
    oss << "{\n";

    oss << "  \"queueBatches\": [\n";
    for (std::size_t batch_index = 0; batch_index < queue_batches.size(); ++batch_index) {
        const auto& batch_ = queue_batches[batch_index];
        oss << "    {\n";
        oss << "      \"index\": " << batch_index << ",\n";
        oss << "      \"queue\": \"" << QueueClassToString(batch_.queue) << "\",\n";
        oss << "      \"passes\": [";
        for (std::size_t pass_index = 0; pass_index < batch_.passes.size(); ++pass_index) {
            if (pass_index != 0U) {
                oss << ", ";
            }
            oss << batch_.passes[pass_index].index;
        }
        oss << "],\n";

        oss << "      \"waitDependencyIndices\": [";
        for (std::size_t dependency_index = 0; dependency_index < batch_.wait_dependency_indices.size(); ++dependency_index) {
            if (dependency_index != 0U) {
                oss << ", ";
            }
            oss << batch_.wait_dependency_indices[dependency_index];
        }
        oss << "],\n";

        oss << "      \"signalDependencyIndices\": [";
        for (std::size_t dependency_index = 0; dependency_index < batch_.signal_dependency_indices.size(); ++dependency_index) {
            if (dependency_index != 0U) {
                oss << ", ";
            }
            oss << batch_.signal_dependency_indices[dependency_index];
        }
        oss << "],\n";

        oss << "      \"barrierBatchIndices\": [";
        for (std::size_t barrier_index = 0; barrier_index < batch_.barrier_batch_indices.size(); ++barrier_index) {
            if (barrier_index != 0U) {
                oss << ", ";
            }
            oss << batch_.barrier_batch_indices[barrier_index];
        }
        oss << "],\n";
        oss << "      \"containsHostBoundary\": " << (batch_.contains_host_boundary ? "true" : "false") << '\n';
        oss << "    }";
        if (batch_index + 1U != queue_batches.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"queueDependencies\": [\n";
    for (std::size_t dependency_index = 0; dependency_index < queue_dependencies.size(); ++dependency_index) {
        const auto& dependency_ = queue_dependencies[dependency_index];
        oss << "    {\n";
        oss << "      \"index\": " << dependency_index << ",\n";
        oss << "      \"sourceQueue\": \"" << QueueClassToString(dependency_.source_queue) << "\",\n";
        oss << "      \"targetQueue\": \"" << QueueClassToString(dependency_.target_queue) << "\",\n";
        oss << "      \"sourceBatchIndex\": " << dependency_.source_batch_index << ",\n";
        oss << "      \"targetBatchIndex\": " << dependency_.target_batch_index << ",\n";
        oss << "      \"sourcePass\": " << dependency_.source_pass.index << ",\n";
        oss << "      \"targetPass\": " << dependency_.target_pass.index << ",\n";
        oss << "      \"resources\": [";
        for (std::size_t resource_index = 0; resource_index < dependency_.resources.size(); ++resource_index) {
            const auto resource_ = dependency_.resources[resource_index];
            if (resource_index != 0U) {
                oss << ", ";
            }
            oss << "{\"resourceIndex\": " << resource_.resource_index
                << ", \"version\": " << resource_.version << '}';
        }
        oss << "],\n";
        oss << "      \"queueTransfer\": " << (dependency_.queue_transfer ? "true" : "false") << ",\n";
        oss << "      \"hostBoundary\": " << (dependency_.host_boundary ? "true" : "false") << '\n';
        oss << "    }";
        if (dependency_index + 1U != queue_dependencies.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"barrierBatches\": [\n";
    for (std::size_t batch_index = 0; batch_index < barrier_batches.size(); ++batch_index) {
        const auto& batch_ = barrier_batches[batch_index];
        oss << "    {\n";
        oss << "      \"pass\": " << batch_.pass.index << ",\n";
        oss << "      \"queue\": \"" << QueueClassToString(batch_.queue) << "\",\n";
        oss << "      \"barriers\": [";
        for (std::size_t barrier_index = 0; barrier_index < batch_.barriers.size(); ++barrier_index) {
            const auto& barrier_ = batch_.barriers[barrier_index];
            if (barrier_index != 0U) {
                oss << ", ";
            }
            oss << "{\"resourceIndex\": " << barrier_.resource.resource_index
                << ", \"version\": " << barrier_.resource.version
                << ", \"kind\": \"" << ResourceKindToString(barrier_.kind) << "\""
                << ", \"before\": \"" << AccessKindToString(barrier_.before) << "\""
                << ", \"after\": \"" << AccessKindToString(barrier_.after) << "\""
                << ", \"srcQueue\": \"" << QueueClassToString(barrier_.src_queue) << "\""
                << ", \"dstQueue\": \"" << QueueClassToString(barrier_.dst_queue) << "\""
                << ", \"queueTransfer\": " << (barrier_.queue_transfer ? "true" : "false")
                << ", \"hostBoundary\": " << (barrier_.host_boundary ? "true" : "false")
                << ", \"uavOrdering\": " << (barrier_.uav_ordering ? "true" : "false");
            if (barrier_.kind == ResourceKind::texture) {
                oss << ", \"subresourceRange\": {"
                    << "\"baseMipLevel\": " << barrier_.subresource_range.base_mip_level
                    << ", \"levelCount\": " << barrier_.subresource_range.level_count
                    << ", \"baseArrayLayer\": " << barrier_.subresource_range.base_array_layer
                    << ", \"layerCount\": " << barrier_.subresource_range.layer_count << "}";
            } else {
                oss << ", \"bufferRange\": {"
                    << "\"offsetBytes\": " << barrier_.buffer_range.offset_bytes
                    << ", \"sizeBytes\": " << barrier_.buffer_range.size_bytes << "}";
            }
            oss << '}';
        }
        oss << "]\n";
        oss << "    }";
        if (batch_index + 1U != barrier_batches.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"aliasCandidates\": [\n";
    for (std::size_t candidate_index = 0; candidate_index < alias_candidates.size(); ++candidate_index) {
        const auto& candidate_ = alias_candidates[candidate_index];
        oss << "    {\n";
        oss << "      \"firstResourceIndex\": " << candidate_.first.index << ",\n";
        oss << "      \"secondResourceIndex\": " << candidate_.second.index << ",\n";
        oss << "      \"firstName\": \"" << candidate_.first_debug_name << "\",\n";
        oss << "      \"secondName\": \"" << candidate_.second_debug_name << "\",\n";
        oss << "      \"kind\": \"" << ResourceKindToString(candidate_.kind) << "\",\n";
        oss << "      \"sameCompatibilityClass\": " << (candidate_.same_compatibility_class ? "true" : "false") << ",\n";
        oss << "      \"overlappingLiveness\": " << (candidate_.overlapping_liveness ? "true" : "false") << ",\n";
        oss << "      \"aliasable\": " << (candidate_.aliasable ? "true" : "false") << '\n';
        oss << "    }";
        if (candidate_index + 1U != alias_candidates.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"aliasBarriers\": [\n";
    for (std::size_t barrier_index = 0; barrier_index < alias_barriers.size(); ++barrier_index) {
        const auto& alias_barrier_ = alias_barriers[barrier_index];
        oss << "    {\n";
        oss << "      \"previousResourceIndex\": " << alias_barrier_.previous.index << ",\n";
        oss << "      \"nextResourceIndex\": " << alias_barrier_.next.index << ",\n";
        oss << "      \"previousName\": \"" << alias_barrier_.previous_debug_name << "\",\n";
        oss << "      \"nextName\": \"" << alias_barrier_.next_debug_name << "\",\n";
        oss << "      \"required\": " << (alias_barrier_.required ? "true" : "false") << ",\n";
        oss << "      \"realized\": " << (alias_barrier_.realized ? "true" : "false") << '\n';
        oss << "    }";
        if (barrier_index + 1U != alias_barriers.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ]\n";
    oss << "}\n";
    return oss.str();
}

BarrierPlan BuildBarrierPlan(const CompiledRenderGraph& compiled_graph_) {
    BarrierPlan plan{};
    if (compiled_graph_.Passes().empty() || compiled_graph_.Resources().empty()) {
        return plan;
    }

    std::uint32_t max_resource_index = 0U;
    for (const auto& resource_ : compiled_graph_.Resources()) {
        max_resource_index = (std::max)(max_resource_index, resource_.handle.index);
    }
    std::vector<LastAccessState> last_accesses(static_cast<std::size_t>(max_resource_index) + 1U);
    std::vector<std::uint32_t> batch_index_by_pass(compiled_graph_.Passes().size(), invalid_render_graph_index);

    bool previous_pass_has_host_boundary = false;
    for (std::uint32_t pass_order = 0U;
         pass_order < static_cast<std::uint32_t>(compiled_graph_.Passes().size());
         ++pass_order) {
        const auto& pass_ = compiled_graph_.Passes()[pass_order];
        const bool has_host_boundary = PassHasHostBoundary(pass_);

        const bool start_new_batch = plan.queue_batches.empty() ||
                                     plan.queue_batches.back().queue != pass_.queue ||
                                     previous_pass_has_host_boundary ||
                                     has_host_boundary;
        if (start_new_batch) {
            plan.queue_batches.push_back(QueueSubmitBatch{.queue = pass_.queue});
        }
        auto& queue_batch = plan.queue_batches.back();
        if (has_host_boundary) {
            queue_batch.contains_host_boundary = true;
        }
        queue_batch.passes.push_back(pass_.handle);
        batch_index_by_pass[pass_.handle.index] = static_cast<std::uint32_t>(plan.queue_batches.size() - 1U);

        CompiledBarrierBatch batch{
            .pass = pass_.handle,
            .queue = pass_.queue,
        };

        const auto process_access = [&](const AccessDesc& access_) {
            if (!IsValidResourceVersionHandle(access_.resource) ||
                access_.resource.resource_index >= last_accesses.size()) {
                return;
            }

            const ResourceKind kind = ResolveResourceKind(compiled_graph_, access_.resource.resource_index);
            auto& previous_ = last_accesses[access_.resource.resource_index];
            if (RequiresBarrier(previous_, access_, pass_.queue, kind)) {
                batch.barriers.push_back(LogicalBarrier{
                    .resource = access_.resource,
                    .kind = kind,
                    .before = previous_.access.access,
                    .after = access_.access,
                    .src_queue = previous_.queue,
                    .dst_queue = pass_.queue,
                    .subresource_range = access_.subresource_range,
                    .buffer_range = access_.buffer_range,
                    .src_pass = previous_.pass,
                    .dst_pass = pass_.handle,
                    .src_pass_order = previous_.pass_order,
                    .dst_pass_order = pass_order,
                    .queue_transfer = previous_.queue != pass_.queue,
                    .host_boundary = IsHostAccess(previous_.access.access) || IsHostAccess(access_.access),
                    .aliasing = false,
                    .uav_ordering = HasStorageWrite(previous_.access.access) && IsStorageAccess(access_.access),
                });
            }

            previous_ = LastAccessState{
                .access = access_,
                .queue = pass_.queue,
                .pass = pass_.handle,
                .pass_order = pass_order,
                .valid = true,
            };
        };

        for (const auto& read_ : pass_.reads) {
            process_access(read_);
        }
        for (const auto& write_ : pass_.writes) {
            process_access(write_);
        }

        if (!batch.barriers.empty()) {
            queue_batch.barrier_batch_indices.push_back(static_cast<std::uint32_t>(plan.barrier_batches.size()));
            plan.barrier_batches.push_back(std::move(batch));
        }

        previous_pass_has_host_boundary = has_host_boundary;
    }

    for (const auto& barrier_batch_ : plan.barrier_batches) {
        for (const auto& barrier_ : barrier_batch_.barriers) {
            if (!IsValidPassHandle(barrier_.src_pass) || !IsValidPassHandle(barrier_.dst_pass)) {
                continue;
            }
            if (barrier_.src_pass.index >= batch_index_by_pass.size() ||
                barrier_.dst_pass.index >= batch_index_by_pass.size()) {
                continue;
            }
            const std::uint32_t source_batch_index = batch_index_by_pass[barrier_.src_pass.index];
            const std::uint32_t target_batch_index = batch_index_by_pass[barrier_.dst_pass.index];
            if (source_batch_index == invalid_render_graph_index ||
                target_batch_index == invalid_render_graph_index) {
                continue;
            }
            if (source_batch_index == target_batch_index) {
                continue;
            }

            auto dependency_it = std::find_if(
                plan.queue_dependencies.begin(),
                plan.queue_dependencies.end(),
                [&](const QueueDependencyPlan& dependency_) {
                    return dependency_.source_batch_index == source_batch_index &&
                           dependency_.target_batch_index == target_batch_index &&
                           dependency_.source_queue == barrier_.src_queue &&
                           dependency_.target_queue == barrier_.dst_queue &&
                           dependency_.queue_transfer == barrier_.queue_transfer &&
                           dependency_.host_boundary == barrier_.host_boundary;
                });
            if (dependency_it == plan.queue_dependencies.end()) {
                plan.queue_dependencies.push_back(QueueDependencyPlan{
                    .source_queue = barrier_.src_queue,
                    .target_queue = barrier_.dst_queue,
                    .source_batch_index = source_batch_index,
                    .target_batch_index = target_batch_index,
                    .source_pass = barrier_.src_pass,
                    .target_pass = barrier_.dst_pass,
                    .queue_transfer = barrier_.queue_transfer,
                    .host_boundary = barrier_.host_boundary,
                });
                dependency_it = plan.queue_dependencies.end() - 1;
            }
            AppendUniqueResourceVersion(dependency_it->resources, barrier_.resource);
        }
    }

    for (std::uint32_t dependency_index = 0U;
         dependency_index < static_cast<std::uint32_t>(plan.queue_dependencies.size());
         ++dependency_index) {
        const auto& dependency_ = plan.queue_dependencies[dependency_index];
        if (dependency_.source_batch_index < plan.queue_batches.size()) {
            AppendUniqueIndex(plan.queue_batches[dependency_.source_batch_index].signal_dependency_indices,
                              dependency_index);
        }
        if (dependency_.target_batch_index < plan.queue_batches.size()) {
            AppendUniqueIndex(plan.queue_batches[dependency_.target_batch_index].wait_dependency_indices,
                              dependency_index);
        }
    }

    for (std::size_t lhs_index = 0U; lhs_index < compiled_graph_.Resources().size(); ++lhs_index) {
        const auto& lhs_resource = compiled_graph_.Resources()[lhs_index];
        if (lhs_resource.lifetime != ResourceLifetime::transient ||
            !ResourceAllowsAlias(lhs_resource)) {
            continue;
        }
        const auto lhs_liveness = ResolveAggregateLiveness(compiled_graph_, lhs_resource.handle);
        for (std::size_t rhs_index = lhs_index + 1U;
             rhs_index < compiled_graph_.Resources().size();
             ++rhs_index) {
            const auto& rhs_resource = compiled_graph_.Resources()[rhs_index];
            if (rhs_resource.lifetime != ResourceLifetime::transient ||
                !ResourceAllowsAlias(rhs_resource)) {
                continue;
            }

            const bool same_class = CompatibilityClassMatches(lhs_resource, rhs_resource);
            const bool overlapping = LivenessOverlaps(lhs_liveness,
                                                      ResolveAggregateLiveness(compiled_graph_, rhs_resource.handle));
            const bool aliasable = same_class && !overlapping;
            plan.alias_candidates.push_back(AliasCandidate{
                .first = lhs_resource.handle,
                .second = rhs_resource.handle,
                .first_debug_name = lhs_resource.debug_name,
                .second_debug_name = rhs_resource.debug_name,
                .kind = lhs_resource.kind,
                .same_compatibility_class = same_class,
                .overlapping_liveness = overlapping,
                .aliasable = aliasable,
            });
            if (aliasable) {
                plan.alias_barriers.push_back(AliasBarrierDecision{
                    .previous = lhs_resource.handle,
                    .next = rhs_resource.handle,
                    .previous_debug_name = lhs_resource.debug_name,
                    .next_debug_name = rhs_resource.debug_name,
                    .required = true,
                    .realized = false,
                });
            }
        }
    }

    return plan;
}

} // namespace vr::render_graph
