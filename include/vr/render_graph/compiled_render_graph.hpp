#pragma once

#include "vr/render_graph/alias_allocator.hpp"
#include "vr/render_graph/barrier_plan.hpp"
#include "vr/render_graph/render_graph_types.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vr::render_graph {

class GraphCommandContext;
using PassExecutionThunk = std::function<void(GraphCommandContext&)>;

struct CompiledPass final {
    PassHandle handle{};
    std::string debug_name{};
    bool side_effect = false;
    bool executable = false;
    QueueClass queue = QueueClass::graphics;
    std::optional<RasterPassDesc> raster_pass{};
    PassExecutionThunk execute{};
    std::vector<PassHandle> dependencies{};
    std::vector<AccessDesc> reads{};
    std::vector<AccessDesc> writes{};
    std::vector<PassDescriptorBindingDesc> descriptor_bindings{};
};

struct CompiledResource final {
    ResourceHandle handle{};
    std::string debug_name{};
    ResourceKind kind = ResourceKind::buffer;
    ResourceLifetime lifetime = ResourceLifetime::transient;
    TextureDesc texture{};
    BufferDesc buffer{};
};

struct CompiledResourceVersionLiveness final {
    ResourceVersionHandle version{};
    std::string debug_name{};
    ResourceKind kind = ResourceKind::buffer;
    ResourceLifetime lifetime = ResourceLifetime::transient;
    std::uint32_t first_pass_order = invalid_render_graph_index;
    std::uint32_t last_pass_order = invalid_render_graph_index;
};

class CompiledRenderGraph final {
public:
    [[nodiscard]] bool Empty() const noexcept {
        return execution_order.empty();
    }

    [[nodiscard]] const std::vector<CompiledPass>& Passes() const noexcept {
        return passes;
    }

    [[nodiscard]] const std::vector<CompiledResource>& Resources() const noexcept {
        return resources;
    }

    [[nodiscard]] const std::vector<PassHandle>& ExecutionOrder() const noexcept {
        return execution_order;
    }

    [[nodiscard]] const std::vector<CompiledResourceVersionLiveness>& LivenessRanges() const noexcept {
        return liveness_ranges;
    }

    [[nodiscard]] const BarrierPlan& PlannedBarriers() const noexcept {
        return barrier_plan;
    }

    [[nodiscard]] const TransientAllocationPlan& TransientAllocations() const noexcept {
        return transient_allocation_plan;
    }

    [[nodiscard]] const DescriptorBindingPlan& DescriptorPlan() const noexcept {
        return descriptor_plan;
    }

    [[nodiscard]] const ExternalBufferBindingResolver* FindExternalBufferBindingResolver(
        const std::uint32_t resolver_id_) const noexcept {
        if (resolver_id_ == 0U) {
            return nullptr;
        }
        const std::size_t resolver_index = static_cast<std::size_t>(resolver_id_ - 1U);
        if (resolver_index >= external_buffer_binding_resolvers.size()) {
            return nullptr;
        }
        return &external_buffer_binding_resolvers[resolver_index];
    }

    [[nodiscard]] bool HasExecutablePasses() const noexcept {
        for (const auto& pass_ : passes) {
            if (pass_.executable) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] const CompiledPass* FindPass(PassHandle handle_) const noexcept;
    [[nodiscard]] const CompiledResource* FindResource(ResourceHandle handle_) const noexcept;
    [[nodiscard]] std::string BuildDebugString() const;
    [[nodiscard]] std::string BuildDotGraph() const;
    [[nodiscard]] std::string BuildJson() const;

private:
    friend class RenderGraphBuilder;

    std::vector<CompiledPass> passes{};
    std::vector<CompiledResource> resources{};
    std::vector<PassHandle> execution_order{};
    std::vector<CompiledResourceVersionLiveness> liveness_ranges{};
    TransientAllocationPlan transient_allocation_plan{};
    BarrierPlan barrier_plan{};
    DescriptorBindingPlan descriptor_plan{};
    std::vector<ExternalBufferBindingResolver> external_buffer_binding_resolvers{};
};

} // namespace vr::render_graph
