#pragma once

#include "vr/render_graph/compiled_render_graph.hpp"

#include <compare>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#ifndef VR_ENABLE_DEBUG_OBSERVABILITY
#define VR_ENABLE_DEBUG_OBSERVABILITY 1
#endif

namespace vr::render_graph {

[[nodiscard]] constexpr bool CompiledRenderGraphObservabilityAvailableInBuild() noexcept {
#if VR_ENABLE_DEBUG_OBSERVABILITY
    return true;
#else
    return false;
#endif
}

namespace detail {

#define VR_DEFINE_RENDER_GRAPH_TOPOLOGY_ID_TYPE(type_name_, underlying_type_)                   \
    struct type_name_ final {                                                                   \
        using underlying_type = underlying_type_;                                               \
                                                                                                 \
        static constexpr underlying_type invalid_value =                                        \
            (std::numeric_limits<underlying_type>::max)();                                      \
                                                                                                 \
        underlying_type value = invalid_value;                                                  \
                                                                                                 \
        constexpr type_name_() noexcept = default;                                              \
        constexpr type_name_(const underlying_type value_) noexcept                             \
            : value(value_) {}                                                                  \
                                                                                                 \
        [[nodiscard]] constexpr bool IsValid() const noexcept {                                 \
            return value != invalid_value;                                                      \
        }                                                                                       \
                                                                                                 \
        [[nodiscard]] static constexpr type_name_ Invalid() noexcept {                          \
            return {};                                                                          \
        }                                                                                       \
                                                                                                 \
        constexpr operator underlying_type() const noexcept {                                   \
            return value;                                                                       \
        }                                                                                       \
                                                                                                 \
        friend constexpr bool operator==(const type_name_&,                                     \
                                         const type_name_&) noexcept = default;                 \
        constexpr auto operator<=>(const type_name_&) const noexcept = default;                 \
    };                                                                                          \
    static_assert(std::is_standard_layout_v<type_name_>);                                       \
    static_assert(std::is_trivially_copyable_v<type_name_>);                                    \
    static_assert(sizeof(type_name_) == sizeof(underlying_type_))

} // namespace detail

VR_DEFINE_RENDER_GRAPH_TOPOLOGY_ID_TYPE(RenderGraphQueueBatchTopologyId,
                                        std::uint32_t);
VR_DEFINE_RENDER_GRAPH_TOPOLOGY_ID_TYPE(RenderGraphQueueDependencyTopologyId,
                                        std::uint32_t);
VR_DEFINE_RENDER_GRAPH_TOPOLOGY_ID_TYPE(RenderGraphBarrierBatchTopologyId,
                                        std::uint32_t);
VR_DEFINE_RENDER_GRAPH_TOPOLOGY_ID_TYPE(RenderGraphNativePassGroupTopologyId,
                                        std::uint32_t);
VR_DEFINE_RENDER_GRAPH_TOPOLOGY_ID_TYPE(RenderGraphNativePassBoundaryTopologyId,
                                        std::uint32_t);

#undef VR_DEFINE_RENDER_GRAPH_TOPOLOGY_ID_TYPE

struct RenderGraphResourceVersionTopologyView final {
    ResourceVersionHandle version{};
    std::string debug_name{};
    ResourceKind kind = ResourceKind::buffer;
};

struct RenderGraphAccessTopologyView final {
    RenderGraphResourceVersionTopologyView resource{};
    AccessKind access = AccessKind::none;
    SubresourceRange subresource_range{};
    BufferRange buffer_range{};
};

struct CompiledRenderGraphPassTopologyView final {
    PassHandle pass{};
    std::string debug_name{};
    bool side_effect = false;
    bool executable = false;
    QueueClass queue = QueueClass::graphics;
    bool force_native_pass_split = false;
    bool raster_pass = false;
    std::uint32_t raster_color_attachment_count = 0U;
    bool raster_has_depth_attachment = false;
    std::vector<PassHandle> dependencies{};
    std::vector<std::string> dependency_debug_names{};
    std::vector<RenderGraphAccessTopologyView> reads{};
    std::vector<RenderGraphAccessTopologyView> writes{};
    std::vector<PassDescriptorBindingDesc> descriptor_bindings{};
    std::uint32_t descriptor_binding_count = 0U;
};

struct CompiledRenderGraphResourceTopologyView final {
    ResourceHandle resource{};
    std::string debug_name{};
    ResourceKind kind = ResourceKind::buffer;
    ResourceLifetime lifetime = ResourceLifetime::transient;
};

struct CompiledRenderGraphLivenessTopologyView final {
    RenderGraphResourceVersionTopologyView resource{};
    ResourceLifetime lifetime = ResourceLifetime::transient;
    std::uint32_t first_pass_order = invalid_render_graph_index;
    std::uint32_t last_pass_order = invalid_render_graph_index;
};

struct CompiledRenderGraphTransientAllocationTopologyView final {
    ResourceHandle resource{};
    std::string debug_name{};
    ResourceKind kind = ResourceKind::buffer;
    ResourceLifetime lifetime = ResourceLifetime::transient;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 1U;
    std::uint32_t memory_type_bits = 0U;
    std::uint64_t usage_flags = 0U;
    bool dedicated_required = false;
    bool dedicated_preferred = false;
    bool host_visible = false;
    bool persistently_mapped = false;
    bool lazy_memory_requested = false;
    std::uint32_t first_pass_order = invalid_render_graph_index;
    std::uint32_t last_pass_order = invalid_render_graph_index;
    std::uint32_t page_index = invalid_render_graph_index;
    std::uint64_t page_offset_bytes = 0U;
    std::uint32_t alias_group = invalid_render_graph_index;
    bool eligible = false;
    bool aliased = false;
    std::string rejection_reason{};
};

struct CompiledRenderGraphTransientPageTopologyView final {
    std::uint32_t page_index = invalid_render_graph_index;
    ResourceKind kind = ResourceKind::buffer;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 1U;
    bool lazy_memory_requested = false;
    std::vector<ResourceHandle> resource_ids{};
    std::vector<std::string> resource_debug_names{};
};

struct CompiledRenderGraphTransientTimelineSampleTopologyView final {
    std::uint32_t pass_order = invalid_render_graph_index;
    std::uint64_t logical_live_bytes = 0U;
    std::uint64_t physical_live_bytes = 0U;
    std::uint32_t live_page_count = 0U;
};

struct CompiledRenderGraphAliasCandidateTopologyView final {
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

struct CompiledRenderGraphAliasBarrierTopologyView final {
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

struct CompiledRenderGraphTransientTopologySummaryView final {
    std::uint64_t logical_total_bytes = 0U;
    std::uint64_t physical_total_bytes = 0U;
    std::uint64_t peak_logical_live_bytes = 0U;
    std::uint64_t peak_live_bytes = 0U;
    std::uint64_t saved_bytes = 0U;
    std::uint32_t transient_resource_count = 0U;
    std::uint32_t eligible_resource_count = 0U;
    std::uint32_t aliased_resource_count = 0U;
    std::uint32_t page_count = 0U;
    std::uint32_t alias_candidate_count = 0U;
    std::uint32_t alias_barrier_count = 0U;
    std::uint32_t timeline_sample_count = 0U;
};

struct CompiledRenderGraphTransientTopologyView final {
    CompiledRenderGraphTransientTopologySummaryView summary{};
    std::vector<CompiledRenderGraphTransientAllocationTopologyView> records{};
    std::vector<CompiledRenderGraphTransientPageTopologyView> pages{};
    std::vector<CompiledRenderGraphTransientTimelineSampleTopologyView> timeline_samples{};
    std::vector<CompiledRenderGraphAliasCandidateTopologyView> alias_candidates{};
    std::vector<CompiledRenderGraphAliasBarrierTopologyView> alias_barriers{};
};

struct RenderGraphQueueDependencyTopologyView final {
    RenderGraphQueueDependencyTopologyId dependency_id{};
    QueueClass source_queue = QueueClass::graphics;
    QueueClass target_queue = QueueClass::graphics;
    RenderGraphQueueBatchTopologyId source_batch_id{};
    RenderGraphQueueBatchTopologyId target_batch_id{};
    PassHandle source_pass{};
    PassHandle target_pass{};
    std::string source_pass_debug_name{};
    std::string target_pass_debug_name{};
    std::vector<RenderGraphResourceVersionTopologyView> resources{};
    bool queue_transfer = false;
    bool host_boundary = false;
};

struct RenderGraphQueueBatchTopologyView final {
    RenderGraphQueueBatchTopologyId batch_id{};
    QueueClass queue = QueueClass::graphics;
    std::vector<PassHandle> pass_ids{};
    std::vector<std::string> pass_debug_names{};
    std::vector<RenderGraphQueueDependencyTopologyId> wait_dependency_ids{};
    std::vector<RenderGraphQueueDependencyTopologyId> signal_dependency_ids{};
    std::vector<RenderGraphBarrierBatchTopologyId> barrier_batch_ids{};
    bool contains_host_boundary = false;
};

struct RenderGraphBarrierTopologyView final {
    RenderGraphResourceVersionTopologyView resource{};
    AccessKind before = AccessKind::none;
    AccessKind after = AccessKind::none;
    QueueClass src_queue = QueueClass::graphics;
    QueueClass dst_queue = QueueClass::graphics;
    SubresourceRange subresource_range{};
    BufferRange buffer_range{};
    PassHandle src_pass{};
    PassHandle dst_pass{};
    std::string src_pass_debug_name{};
    std::string dst_pass_debug_name{};
    std::uint32_t src_pass_order = invalid_render_graph_index;
    std::uint32_t dst_pass_order = invalid_render_graph_index;
    bool queue_transfer = false;
    bool host_boundary = false;
    bool aliasing = false;
    bool uav_ordering = false;
};

struct RenderGraphBarrierBatchTopologyView final {
    RenderGraphBarrierBatchTopologyId barrier_batch_id{};
    PassHandle pass{};
    std::string pass_debug_name{};
    QueueClass queue = QueueClass::graphics;
    std::vector<RenderGraphBarrierTopologyView> barriers{};
};

struct RenderGraphNativePassGroupTopologyView final {
    RenderGraphNativePassGroupTopologyId group_id{};
    QueueClass queue = QueueClass::graphics;
    std::uint32_t first_pass_order = invalid_render_graph_index;
    std::uint32_t last_pass_order = invalid_render_graph_index;
    std::vector<PassHandle> pass_ids{};
    std::vector<std::string> pass_debug_names{};
    std::uint32_t color_attachment_count = 0U;
    bool has_depth_attachment = false;
    std::uint32_t layer_count = 1U;
};

struct RenderGraphNativePassBoundaryTopologyView final {
    RenderGraphNativePassBoundaryTopologyId boundary_id{};
    std::uint32_t previous_pass_order = invalid_render_graph_index;
    PassHandle previous_pass{};
    std::string previous_pass_debug_name{};
    std::uint32_t next_pass_order = invalid_render_graph_index;
    PassHandle next_pass{};
    std::string next_pass_debug_name{};
    bool fused = false;
    NativePassFusionBlockReason block_reason = NativePassFusionBlockReason::none;
    bool local_read_candidate = false;
    NativePassLocalReadStatus local_read_status =
        NativePassLocalReadStatus::not_applicable;
    NativePassLocalReadReason local_read_reason =
        NativePassLocalReadReason::none;
    std::string detail{};
};

struct CompiledRenderGraphTopologySummaryView final {
    std::uint32_t execution_order_count = 0U;
    std::uint32_t pass_count = 0U;
    std::uint32_t executable_pass_count = 0U;
    std::uint32_t culled_pass_count = 0U;
    std::uint32_t resource_count = 0U;
    std::uint32_t liveness_range_count = 0U;
    std::uint32_t queue_batch_count = 0U;
    std::uint32_t queue_dependency_count = 0U;
    std::uint32_t queue_batch_host_boundary_count = 0U;
    std::uint32_t queue_dependency_host_boundary_count = 0U;
    std::uint32_t barrier_batch_count = 0U;
    std::uint32_t barrier_count = 0U;
    std::uint32_t alias_candidate_count = 0U;
    std::uint32_t alias_barrier_count = 0U;
    std::uint32_t native_pass_group_count = 0U;
    std::uint32_t native_pass_boundary_count = 0U;
    std::uint32_t logical_raster_pass_count = 0U;
    std::uint32_t fused_raster_pass_count = 0U;
    std::uint32_t store_elision_count = 0U;
    std::uint32_t load_inference_count = 0U;
    std::uint32_t clear_attachment_count = 0U;
    std::uint32_t local_read_candidate_count = 0U;
};

struct CompiledRenderGraphTopologyView final {
    CompiledRenderGraphTopologySummaryView summary{};
    std::vector<PassHandle> execution_order{};
    std::vector<CompiledRenderGraphResourceTopologyView> resources{};
    std::vector<CompiledRenderGraphPassTopologyView> passes{};
    std::vector<CompiledRenderGraphLivenessTopologyView> liveness_ranges{};
    CompiledRenderGraphTransientTopologyView transient_memory{};
    std::vector<RenderGraphQueueBatchTopologyView> queue_batches{};
    std::vector<RenderGraphQueueDependencyTopologyView> queue_dependencies{};
    std::vector<RenderGraphBarrierBatchTopologyView> barrier_batches{};
    NativePassLocalReadDecision native_pass_local_read{};
    std::vector<RenderGraphNativePassGroupTopologyView> native_pass_groups{};
    std::vector<RenderGraphNativePassBoundaryTopologyView> native_pass_boundaries{};
};

[[nodiscard]] CompiledRenderGraphTopologyView BuildCompiledRenderGraphTopologyView(
    const CompiledRenderGraph& compiled_graph_);
[[nodiscard]] std::string BuildCompiledRenderGraphTopologyDebugString(
    const CompiledRenderGraphTopologyView& view_);
[[nodiscard]] std::string BuildCompiledRenderGraphTopologyJson(
    const CompiledRenderGraphTopologyView& view_);

} // namespace vr::render_graph
