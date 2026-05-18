#pragma once

#include "vr/render_graph/render_graph_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vr::render_graph {

class CompiledRenderGraph;
struct CompiledResource;

struct ResourceFootprint final {
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 1U;
    std::uint32_t memory_type_bits = 0xFFFFFFFFU;
    std::uint64_t usage_flags = 0U;
    bool dedicated_required = false;
    bool dedicated_preferred = false;
    bool host_visible = false;
    bool persistently_mapped = false;
    bool lazy_memory_requested = false;
};

struct TransientCompatibilityKey final {
    ResourceKind kind = ResourceKind::buffer;
    std::uint64_t usage_flags = 0U;
    std::uint32_t memory_type_bits = 0xFFFFFFFFU;
    TextureDimension dimension = TextureDimension::image_2d;
    TextureFormat format = TextureFormat::unknown;
    Extent3D extent{};
    std::uint32_t mip_level_count = 1U;
    std::uint32_t array_layer_count = 1U;
    SampleCount sample_count = SampleCount::x1;
    bool host_visible = false;
    bool persistently_mapped = false;
    bool dedicated_required = false;
    bool lazy_memory_requested = false;
};

struct AliasCandidate final {
    ResourceHandle first{};
    ResourceHandle second{};
    std::string first_debug_name{};
    std::string second_debug_name{};
    ResourceKind kind = ResourceKind::buffer;
    bool same_compatibility_class = false;
    bool overlapping_liveness = false;
    bool aliasable = false;
    std::string non_alias_reason{};
};

struct AliasBarrierDecision final {
    ResourceHandle previous{};
    ResourceHandle next{};
    std::string previous_debug_name{};
    std::string next_debug_name{};
    std::uint32_t previous_last_pass_order = invalid_render_graph_index;
    std::uint32_t next_first_pass_order = invalid_render_graph_index;
    std::uint32_t page_index = invalid_render_graph_index;
    bool required = false;
    bool realized = false;
};

struct TransientAllocationRecord final {
    ResourceHandle resource{};
    std::string debug_name{};
    ResourceKind kind = ResourceKind::buffer;
    ResourceLifetime lifetime = ResourceLifetime::transient;
    ResourceFootprint footprint{};
    TransientCompatibilityKey compatibility{};
    std::uint32_t first_pass_order = invalid_render_graph_index;
    std::uint32_t last_pass_order = invalid_render_graph_index;
    std::uint32_t page_index = invalid_render_graph_index;
    std::uint64_t page_offset_bytes = 0U;
    std::uint32_t alias_group = invalid_render_graph_index;
    bool eligible = false;
    bool aliased = false;
    std::string rejection_reason{};
};

struct TransientMemoryPage final {
    std::uint32_t page_index = invalid_render_graph_index;
    ResourceKind kind = ResourceKind::buffer;
    TransientCompatibilityKey compatibility{};
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 1U;
    std::vector<ResourceHandle> resources{};
};

struct TransientMemoryTimelineSample final {
    std::uint32_t pass_order = invalid_render_graph_index;
    std::uint64_t logical_live_bytes = 0U;
    std::uint64_t physical_live_bytes = 0U;
    std::uint32_t live_page_count = 0U;
};

struct TransientMemoryTimeline final {
    std::vector<TransientMemoryTimelineSample> samples{};
    std::uint64_t logical_total_bytes = 0U;
    std::uint64_t physical_total_bytes = 0U;
    std::uint64_t peak_logical_live_bytes = 0U;
    std::uint64_t peak_live_bytes = 0U;
    std::uint64_t saved_bytes = 0U;
    std::uint32_t transient_resource_count = 0U;
    std::uint32_t eligible_resource_count = 0U;
    std::uint32_t aliased_resource_count = 0U;
    std::uint32_t page_count = 0U;
    std::uint32_t alias_barrier_count = 0U;
};

struct TransientAllocationPlan final {
    std::vector<TransientAllocationRecord> records{};
    std::vector<TransientMemoryPage> pages{};
    std::vector<AliasCandidate> alias_candidates{};
    std::vector<AliasBarrierDecision> alias_barriers{};
    TransientMemoryTimeline timeline{};

    [[nodiscard]] bool Empty() const noexcept {
        return records.empty() &&
               pages.empty() &&
               alias_candidates.empty() &&
               alias_barriers.empty() &&
               timeline.samples.empty() &&
               timeline.logical_total_bytes == 0U &&
               timeline.physical_total_bytes == 0U;
    }
};

using ResolveTransientFootprintFn =
    bool (*)(const CompiledResource& resource_,
             ResourceFootprint& footprint_,
             const void* user_data_,
             std::string& error_message_);

struct TransientFootprintProvider final {
    const void* user_data = nullptr;
    ResolveTransientFootprintFn resolve_fn = nullptr;
};

[[nodiscard]] bool TransientCompatibilityKeysEqual(const TransientCompatibilityKey& lhs_,
                                                   const TransientCompatibilityKey& rhs_) noexcept;
[[nodiscard]] TransientAllocationPlan BuildTransientAllocationPlan(
    const CompiledRenderGraph& compiled_graph_,
    const TransientFootprintProvider& footprint_provider_ = {});

} // namespace vr::render_graph
