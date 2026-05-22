#include "render_graph_builder_internal.hpp"

#include <sstream>

namespace vr::render_graph {
using namespace builder_detail;

std::string CompiledRenderGraph::BuildDebugString() const {
    std::ostringstream oss{};
    oss << "execution_order=" << execution_order.size() << '\n';
    for (std::size_t index = 0; index < passes.size(); ++index) {
        const auto& pass_ = passes[index];
        oss << '[' << index << "] pass=" << pass_.debug_name;
        if (pass_.side_effect) {
            oss << " side_effect";
        }
        oss << " queue=" << QueueClassToString(pass_.queue);
        if (pass_.compile_hints.force_native_pass_split) {
            oss << " force_native_pass_split";
        }
        oss << " deps=";
        for (std::size_t dep_index = 0; dep_index < pass_.dependencies.size(); ++dep_index) {
            if (dep_index != 0U) {
                oss << ',';
            }
            oss << pass_.dependencies[dep_index].index;
        }
        oss << " reads=" << pass_.reads.size();
        oss << " writes=" << pass_.writes.size();
        if (pass_.raster_pass.has_value()) {
            oss << " raster_colors=" << pass_.raster_pass->color_attachments.size();
            if (pass_.raster_pass->has_depth_attachment) {
                oss << " depth";
            }
        }
        if (pass_.executable) {
            oss << " executable";
        }
        if (!pass_.descriptor_bindings.empty()) {
            oss << " descriptor_bindings=" << pass_.descriptor_bindings.size();
        }
        oss << '\n';
    }

    oss << "liveness=" << liveness_ranges.size() << '\n';
    oss << BuildNativePassPlanDebugString(*this);
    for (const auto& range_ : liveness_ranges) {
        oss << "resource=" << range_.debug_name
            << " first=" << range_.first_pass_order
            << " last=" << range_.last_pass_order << '\n';
    }

    oss << "transient_allocation_records=" << transient_allocation_plan.records.size() << '\n';
    oss << "transient_allocation_pages=" << transient_allocation_plan.pages.size() << '\n';
    oss << "transient_logical_total_bytes=" << transient_allocation_plan.timeline.logical_total_bytes << '\n';
    oss << "transient_physical_total_bytes=" << transient_allocation_plan.timeline.physical_total_bytes << '\n';
    oss << "transient_peak_live_bytes=" << transient_allocation_plan.timeline.peak_live_bytes << '\n';
    oss << "transient_saved_bytes=" << transient_allocation_plan.timeline.saved_bytes << '\n';
    for (const auto& page_ : transient_allocation_plan.pages) {
        oss << "transient_page=" << page_.page_index
            << " kind=" << ResourceKindToString(page_.kind)
            << " size=" << page_.size_bytes
            << " alignment=" << page_.alignment_bytes
            << " resources=";
        for (std::size_t index = 0; index < page_.resources.size(); ++index) {
            if (index != 0U) {
                oss << ',';
            }
            oss << page_.resources[index].index;
        }
        oss << '\n';
    }

    oss << barrier_plan.BuildDebugString();
    oss << "descriptor_plan_layouts=" << descriptor_plan.pass_layouts.size() << '\n';
    oss << "descriptor_plan_write_batches=" << descriptor_plan.writes.size() << '\n';
    oss << "descriptor_plan_bindless_allocations=" << descriptor_plan.bindless_allocations.size() << '\n';
    return oss.str();
}

std::string CompiledRenderGraph::BuildDotGraph() const {
    std::ostringstream oss{};
    oss << "digraph RenderGraph {\n";
    oss << "  rankdir=LR;\n";
    oss << "  node [fontsize=10];\n";

    for (const auto& pass_ : passes) {
        oss << "  pass_" << pass_.handle.index << " [shape=box,label=\""
            << EscapeDotLabel(pass_.debug_name);
        if (pass_.side_effect) {
            oss << "\\nside_effect";
        }
        oss << "\\nqueue=" << QueueClassToString(pass_.queue);
        if (!pass_.descriptor_bindings.empty()) {
            oss << "\\ndescriptors=" << pass_.descriptor_bindings.size();
        }
        oss << "\"];\n";
    }

    for (const auto& range_ : liveness_ranges) {
        oss << "  " << MakeResourceNodeId(range_.version)
            << " [shape=ellipse,label=\""
            << EscapeDotLabel(range_.debug_name)
            << "\\n" << ResourceKindToString(range_.kind)
            << " " << ResourceLifetimeToString(range_.lifetime)
            << "\\n" << range_.first_pass_order << "->" << range_.last_pass_order
            << "\"];\n";
    }

    for (const auto& pass_ : passes) {
        for (const auto dependency_ : pass_.dependencies) {
            oss << "  pass_" << dependency_.index << " -> pass_" << pass_.handle.index
                << " [style=dashed,label=\"dep\"];\n";
        }
        for (const auto& read_ : pass_.reads) {
            if (const auto* liveness_ = FindLivenessRange(liveness_ranges, read_.resource);
                liveness_ != nullptr) {
                oss << "  " << MakeResourceNodeId(read_.resource) << " -> pass_" << pass_.handle.index
                    << " [label=\"" << BuildAccessDotLabel(read_, liveness_->kind) << "\"];\n";
            }
        }
        for (const auto& write_ : pass_.writes) {
            if (const auto* liveness_ = FindLivenessRange(liveness_ranges, write_.resource);
                liveness_ != nullptr) {
                oss << "  pass_" << pass_.handle.index << " -> " << MakeResourceNodeId(write_.resource)
                    << " [label=\"" << BuildAccessDotLabel(write_, liveness_->kind) << "\"];\n";
            }
        }
        for (const auto& binding_ : pass_.descriptor_bindings) {
            oss << "  pass_" << pass_.handle.index << " -> pass_" << pass_.handle.index
                << " [style=dotted,label=\"" << BuildDescriptorLayoutDotLabel(binding_) << "\"];\n";
        }
    }

    oss << "}\n";
    return oss.str();
}

std::string CompiledRenderGraph::BuildJson() const {
    std::ostringstream oss{};
    oss << "{\n";
    oss << "  \"executionOrder\": [";
    for (std::size_t index = 0; index < execution_order.size(); ++index) {
        if (index != 0U) {
            oss << ", ";
        }
        oss << execution_order[index].index;
    }
    oss << "],\n";

    oss << "  \"resources\": [\n";
    for (std::size_t index = 0; index < resources.size(); ++index) {
        const auto& resource_ = resources[index];
        oss << "    {\n";
        oss << "      \"index\": " << resource_.handle.index << ",\n";
        oss << "      \"name\": \"" << EscapeJsonString(resource_.debug_name) << "\",\n";
        oss << "      \"kind\": \"" << ResourceKindToString(resource_.kind) << "\",\n";
        oss << "      \"lifetime\": \"" << ResourceLifetimeToString(resource_.lifetime) << "\"\n";
        oss << "    }";
        if (index + 1U != resources.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"passes\": [\n";
    for (std::size_t index = 0; index < passes.size(); ++index) {
        const auto& pass_ = passes[index];
        oss << "    {\n";
        oss << "      \"index\": " << pass_.handle.index << ",\n";
        oss << "      \"name\": \"" << EscapeJsonString(pass_.debug_name) << "\",\n";
        oss << "      \"sideEffect\": " << (pass_.side_effect ? "true" : "false") << ",\n";
        oss << "      \"executable\": " << (pass_.executable ? "true" : "false") << ",\n";
        oss << "      \"queue\": \"" << QueueClassToString(pass_.queue) << "\",\n";
        oss << "      \"compileHints\": {\"forceNativePassSplit\": "
            << (pass_.compile_hints.force_native_pass_split ? "true" : "false")
            << "},\n";

        oss << "      \"dependencies\": [";
        for (std::size_t dep_index = 0; dep_index < pass_.dependencies.size(); ++dep_index) {
            if (dep_index != 0U) {
                oss << ", ";
            }
            oss << pass_.dependencies[dep_index].index;
        }
        oss << "],\n";

        oss << "      \"reads\": [";
        for (std::size_t read_index = 0; read_index < pass_.reads.size(); ++read_index) {
            const auto& read_ = pass_.reads[read_index];
            if (read_index != 0U) {
                oss << ", ";
            }
            const auto* resource_ = FindResource(ResourceHandle{
                .index = read_.resource.resource_index,
                .generation = 1U,
            });
            const ResourceKind kind = (resource_ != nullptr) ? resource_->kind : ResourceKind::buffer;
            AppendJsonAccessDesc(oss, read_, kind);
        }
        oss << "],\n";

        oss << "      \"writes\": [";
        for (std::size_t write_index = 0; write_index < pass_.writes.size(); ++write_index) {
            const auto& write_ = pass_.writes[write_index];
            if (write_index != 0U) {
                oss << ", ";
            }
            const auto* resource_ = FindResource(ResourceHandle{
                .index = write_.resource.resource_index,
                .generation = 1U,
            });
            const ResourceKind kind = (resource_ != nullptr) ? resource_->kind : ResourceKind::buffer;
            AppendJsonAccessDesc(oss, write_, kind);
        }
        oss << "],\n";

        oss << "      \"descriptorBindings\": [";
        for (std::size_t binding_index = 0; binding_index < pass_.descriptor_bindings.size(); ++binding_index) {
            if (binding_index != 0U) {
                oss << ", ";
            }
            AppendJsonDescriptorBinding(oss, pass_.descriptor_bindings[binding_index]);
        }
        oss << "]\n";
        oss << "    }";
        if (index + 1U != passes.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";
    oss << "  \"nativePassPlan\": " << BuildNativePassPlanJson(*this) << ",\n";

    oss << "  \"livenessRanges\": [\n";
    for (std::size_t index = 0; index < liveness_ranges.size(); ++index) {
        const auto& range_ = liveness_ranges[index];
        oss << "    {\n";
        oss << "      \"resourceIndex\": " << range_.version.resource_index << ",\n";
        oss << "      \"version\": " << range_.version.version << ",\n";
        oss << "      \"name\": \"" << EscapeJsonString(range_.debug_name) << "\",\n";
        oss << "      \"kind\": \"" << ResourceKindToString(range_.kind) << "\",\n";
        oss << "      \"lifetime\": \"" << ResourceLifetimeToString(range_.lifetime) << "\",\n";
        oss << "      \"firstPassOrder\": " << range_.first_pass_order << ",\n";
        oss << "      \"lastPassOrder\": " << range_.last_pass_order << '\n';
        oss << "    }";
        if (index + 1U != liveness_ranges.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";
    oss << "  \"descriptorPlan\": {\n";
    oss << "    \"passLayouts\": [";
    for (std::size_t layout_index = 0; layout_index < descriptor_plan.pass_layouts.size(); ++layout_index) {
        if (layout_index != 0U) {
            oss << ", ";
        }
        const auto& layout_ = descriptor_plan.pass_layouts[layout_index];
        oss << "{\"passIndex\": " << layout_.pass.index << ", \"bindings\": [";
        for (std::size_t binding_index = 0; binding_index < layout_.bindings.size(); ++binding_index) {
            if (binding_index != 0U) {
                oss << ", ";
            }
            AppendJsonDescriptorBinding(oss, layout_.bindings[binding_index]);
        }
        oss << "]}";
    }
    oss << "],\n";
    oss << "    \"writeBatches\": [";
    for (std::size_t batch_index = 0; batch_index < descriptor_plan.writes.size(); ++batch_index) {
        if (batch_index != 0U) {
            oss << ", ";
        }
        const auto& batch_ = descriptor_plan.writes[batch_index];
        oss << "{\"passIndex\": " << batch_.pass.index << ", \"writes\": [";
        for (std::size_t write_index = 0; write_index < batch_.writes.size(); ++write_index) {
            if (write_index != 0U) {
                oss << ", ";
            }
            AppendJsonDescriptorWrite(oss, batch_.writes[write_index]);
        }
        oss << "]}";
    }
    oss << "],\n";
    oss << "    \"bindlessAllocations\": [";
    for (std::size_t allocation_index = 0; allocation_index < descriptor_plan.bindless_allocations.size(); ++allocation_index) {
        if (allocation_index != 0U) {
            oss << ", ";
        }
        AppendJsonBindlessAllocation(oss, descriptor_plan.bindless_allocations[allocation_index]);
    }
    oss << "]\n";
    oss << "  },\n";
    oss << "  \"transientAllocationPlan\": ";
    AppendTransientAllocationJson(oss, transient_allocation_plan);
    oss << ",\n";
    oss << "  \"barrierPlan\": " << barrier_plan.BuildJson() << "\n";
    oss << "}\n";
    return oss.str();
}

} // namespace vr::render_graph
