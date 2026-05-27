#include "vr/render_graph/compiled_render_graph_observability.hpp"
#include "vr/render_graph/native_pass_plan.hpp"
#include "render_graph_builder_internal.hpp"

#include <sstream>

namespace vr::render_graph {
using namespace builder_detail;

std::string CompiledRenderGraph::BuildDebugString() const {
#if !VR_ENABLE_DEBUG_OBSERVABILITY
    return {};
#else
    const auto topology_view = BuildCompiledRenderGraphTopologyView(*this);
    std::ostringstream oss{};
    oss << BuildCompiledRenderGraphTopologyDebugString(topology_view);
    oss << BuildNativePassPlanDebugString(*this);

    oss << "descriptor_plan_layouts=" << descriptor_plan.pass_layouts.size() << '\n';
    oss << "descriptor_plan_write_batches=" << descriptor_plan.writes.size() << '\n';
    oss << "descriptor_plan_bindless_allocations=" << descriptor_plan.bindless_allocations.size() << '\n';
    return oss.str();
#endif
}

std::string CompiledRenderGraph::BuildDotGraph() const {
#if !VR_ENABLE_DEBUG_OBSERVABILITY
    return {};
#else
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
#endif
}

std::string CompiledRenderGraph::BuildJson() const {
#if !VR_ENABLE_DEBUG_OBSERVABILITY
    return {};
#else
    const auto topology_view = BuildCompiledRenderGraphTopologyView(*this);
    const std::string topology_json =
        BuildCompiledRenderGraphTopologyJson(topology_view);
    std::ostringstream oss{};
    oss << "{\n";
    oss << "  \"topology\": " << topology_json << ",\n";
    oss << "  \"nativePassPlan\": " << BuildNativePassPlanJson(*this) << ",\n";
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
    oss << "  \"barrierPlan\": " << barrier_plan.BuildJson() << "\n";
    oss << "}\n";
    return oss.str();
#endif
}

} // namespace vr::render_graph
