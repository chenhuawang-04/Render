#include "vr/render_graph/alias_allocator.hpp"

#include "vr/render_graph/compiled_render_graph.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>

namespace vr::render_graph {
namespace {

struct AggregateLiveness final {
    std::uint32_t first_pass_order = invalid_render_graph_index;
    std::uint32_t last_pass_order = invalid_render_graph_index;
    bool valid = false;
};

struct EligibilityResult final {
    bool eligible = false;
    std::string rejection_reason{};
    ResourceFootprint footprint{};
    TransientCompatibilityKey compatibility{};
};

struct PageAssignmentState final {
    TransientMemoryPage page{};
    std::vector<std::size_t> record_indices{};
    std::uint32_t available_after_pass_order = invalid_render_graph_index;
};

[[nodiscard]] std::uint32_t TextureBytesPerTexel(const TextureFormat format_) noexcept {
    switch (format_) {
    case TextureFormat::r8g8b8a8_unorm:
        return 4U;
    case TextureFormat::r16g16b16a16_sfloat:
        return 8U;
    case TextureFormat::d32_sfloat:
        return 4U;
    case TextureFormat::unknown:
    default:
        break;
    }
    return 0U;
}

[[nodiscard]] std::uint64_t EstimateTextureFootprintBytes(const TextureDesc& desc_) noexcept {
    const std::uint32_t bytes_per_texel = TextureBytesPerTexel(desc_.format);
    if (bytes_per_texel == 0U) {
        return 0U;
    }

    std::uint64_t total_bytes = 0U;
    for (std::uint32_t mip = 0U; mip < desc_.mip_level_count; ++mip) {
        const std::uint32_t width = (std::max)(1U, desc_.extent.width >> mip);
        const std::uint32_t height = (std::max)(1U, desc_.extent.height >> mip);
        const std::uint32_t depth = (std::max)(1U, desc_.extent.depth >> mip);
        total_bytes += static_cast<std::uint64_t>(width) *
                       static_cast<std::uint64_t>(height) *
                       static_cast<std::uint64_t>(depth) *
                       static_cast<std::uint64_t>((std::max)(1U, desc_.array_layer_count)) *
                       static_cast<std::uint64_t>(bytes_per_texel);
    }
    return total_bytes;
}

[[nodiscard]] std::string BuildLifetimeRejectReason(const ResourceLifetime lifetime_) {
    switch (lifetime_) {
    case ResourceLifetime::imported:
        return "imported resources are not graph-owned transient allocations";
    case ResourceLifetime::persistent:
        return "persistent resources do not participate in transient alias allocation";
    case ResourceLifetime::transient:
    default:
        break;
    }
    return "resource is not eligible for transient alias allocation";
}

[[nodiscard]] AggregateLiveness ResolveAggregateLiveness(const CompiledRenderGraph& compiled_graph_,
                                                         const ResourceHandle resource_) noexcept {
    AggregateLiveness aggregate{};
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

[[nodiscard]] bool LivenessOverlaps(const AggregateLiveness& lhs_,
                                    const AggregateLiveness& rhs_) noexcept {
    if (!lhs_.valid || !rhs_.valid) {
        return false;
    }
    return !(lhs_.last_pass_order < rhs_.first_pass_order ||
             rhs_.last_pass_order < lhs_.first_pass_order);
}

[[nodiscard]] bool ResolveFallbackFootprint(const CompiledResource& resource_,
                                            ResourceFootprint& footprint_,
                                            std::string& error_message_) {
    footprint_ = {};
    if (resource_.kind == ResourceKind::buffer) {
        if (resource_.buffer.size_bytes == 0U) {
            error_message_ = "buffer size_bytes must be > 0";
            return false;
        }
        footprint_.size_bytes = resource_.buffer.size_bytes;
        footprint_.alignment_bytes = 1U;
        footprint_.memory_type_bits = 0xFFFFFFFFU;
        footprint_.usage_flags = resource_.buffer.usage;
        footprint_.host_visible = resource_.buffer.host_visible;
        footprint_.persistently_mapped = resource_.buffer.persistently_mapped;
        return true;
    }

    const std::uint64_t estimated_bytes = EstimateTextureFootprintBytes(resource_.texture);
    if (estimated_bytes == 0U) {
        error_message_ = "texture footprint estimate failed";
        return false;
    }
    footprint_.size_bytes = estimated_bytes;
    footprint_.alignment_bytes = 1U;
    footprint_.memory_type_bits = 0xFFFFFFFFU;
    footprint_.usage_flags = resource_.texture.usage;
    footprint_.lazy_memory_requested = resource_.texture.prefer_lazy_memory;
    return true;
}

[[nodiscard]] bool ResolveFootprint(const CompiledResource& resource_,
                                    const TransientFootprintProvider& footprint_provider_,
                                    ResourceFootprint& footprint_,
                                    std::string& error_message_) {
    error_message_.clear();
    if (footprint_provider_.resolve_fn != nullptr) {
        if (!footprint_provider_.resolve_fn(resource_,
                                            footprint_,
                                            footprint_provider_.user_data,
                                            error_message_)) {
            if (error_message_.empty()) {
                error_message_ = "footprint provider rejected resource";
            }
            return false;
        }
        if (footprint_.size_bytes == 0U) {
            error_message_ = "footprint provider returned size_bytes == 0";
            return false;
        }
        if (footprint_.alignment_bytes == 0U) {
            error_message_ = "footprint provider returned alignment_bytes == 0";
            return false;
        }
        return true;
    }
    return ResolveFallbackFootprint(resource_, footprint_, error_message_);
}

[[nodiscard]] EligibilityResult EvaluateEligibility(const CompiledResource& resource_,
                                                    const TransientFootprintProvider& footprint_provider_) {
    EligibilityResult result{};
    if (resource_.lifetime != ResourceLifetime::transient) {
        result.rejection_reason = BuildLifetimeRejectReason(resource_.lifetime);
        return result;
    }

    if (resource_.kind == ResourceKind::buffer) {
        if (!resource_.buffer.allow_alias) {
            result.rejection_reason = "buffer allow_alias == false";
            return result;
        }
        if (resource_.buffer.host_visible) {
            result.rejection_reason = "host-visible buffers do not alias";
            return result;
        }
        if (resource_.buffer.persistently_mapped) {
            result.rejection_reason = "persistently mapped buffers do not alias";
            return result;
        }
        if (resource_.buffer.usage == 0U) {
            result.rejection_reason = "buffer usage must be non-zero";
            return result;
        }
    } else {
        if (!resource_.texture.allow_alias) {
            result.rejection_reason = "texture alias requires explicit opt-in";
            return result;
        }
        if (resource_.texture.format == TextureFormat::unknown) {
            result.rejection_reason = "texture format must be known for alias allocation";
            return result;
        }
        if (HasTextureUsageFlag(resource_.texture.usage, texture_usage_depth_stencil_attachment_flag)) {
            result.rejection_reason = "depth-stencil textures stay conservative in phase 9";
            return result;
        }
        if (HasTextureUsageFlag(resource_.texture.usage, texture_usage_storage_flag)) {
            result.rejection_reason = "storage textures stay conservative in phase 9";
            return result;
        }
        if (resource_.texture.sample_count != SampleCount::x1) {
            result.rejection_reason = "MSAA textures stay conservative in phase 9";
            return result;
        }
    }

    if (!ResolveFootprint(resource_,
                          footprint_provider_,
                          result.footprint,
                          result.rejection_reason)) {
        return result;
    }

    if (result.footprint.dedicated_required) {
        result.rejection_reason = "dedicated allocation requirement prevents aliasing";
        return result;
    }
    if (result.footprint.memory_type_bits == 0U) {
        result.rejection_reason = "resource footprint has no compatible memory type bits";
        return result;
    }
    if (resource_.kind == ResourceKind::buffer) {
        if (result.footprint.host_visible) {
            result.rejection_reason = "host-visible buffers do not alias";
            return result;
        }
        if (result.footprint.persistently_mapped) {
            result.rejection_reason = "persistently mapped buffers do not alias";
            return result;
        }
    }

    result.compatibility.kind = resource_.kind;
    result.compatibility.usage_flags = result.footprint.usage_flags;
    result.compatibility.memory_type_bits = result.footprint.memory_type_bits;
    result.compatibility.dimension = resource_.texture.dimension;
    result.compatibility.format = resource_.texture.format;
    result.compatibility.extent = resource_.texture.extent;
    result.compatibility.mip_level_count = resource_.texture.mip_level_count;
    result.compatibility.array_layer_count = resource_.texture.array_layer_count;
    result.compatibility.sample_count = resource_.texture.sample_count;
    result.compatibility.host_visible = result.footprint.host_visible;
    result.compatibility.persistently_mapped = result.footprint.persistently_mapped;
    result.compatibility.dedicated_required = result.footprint.dedicated_required;
    result.compatibility.lazy_memory_requested = result.footprint.lazy_memory_requested;
    result.eligible = true;
    return result;
}

[[nodiscard]] std::uint64_t PassLiveBytes(const TransientAllocationPlan& plan_,
                                          const std::uint32_t pass_order_,
                                          const bool physical_) noexcept {
    std::uint64_t live_bytes = 0U;
    if (physical_) {
        for (const auto& page_ : plan_.pages) {
            bool page_live = false;
            for (const auto handle_ : page_.resources) {
                const auto record_it = std::find_if(plan_.records.begin(),
                                                    plan_.records.end(),
                                                    [&](const TransientAllocationRecord& record_) {
                                                        return record_.resource.index == handle_.index;
                                                    });
                if (record_it == plan_.records.end()) {
                    continue;
                }
                if (!record_it->eligible) {
                    continue;
                }
                if (record_it->first_pass_order <= pass_order_ &&
                    pass_order_ <= record_it->last_pass_order) {
                    page_live = true;
                    break;
                }
            }
            if (page_live) {
                live_bytes += page_.size_bytes;
            }
        }
        return live_bytes;
    }

    for (const auto& record_ : plan_.records) {
        if (!record_.eligible) {
            continue;
        }
        if (record_.first_pass_order <= pass_order_ &&
            pass_order_ <= record_.last_pass_order) {
            live_bytes += record_.footprint.size_bytes;
        }
    }
    return live_bytes;
}

[[nodiscard]] std::uint32_t PassLivePageCount(const TransientAllocationPlan& plan_,
                                              const std::uint32_t pass_order_) noexcept {
    std::uint32_t live_page_count = 0U;
    for (const auto& page_ : plan_.pages) {
        bool page_live = false;
        for (const auto handle_ : page_.resources) {
            const auto record_it = std::find_if(plan_.records.begin(),
                                                plan_.records.end(),
                                                [&](const TransientAllocationRecord& record_) {
                                                    return record_.resource.index == handle_.index;
                                                });
            if (record_it == plan_.records.end()) {
                continue;
            }
            if (!record_it->eligible) {
                continue;
            }
            if (record_it->first_pass_order <= pass_order_ &&
                pass_order_ <= record_it->last_pass_order) {
                page_live = true;
                break;
            }
        }
        if (page_live) {
            live_page_count += 1U;
        }
    }
    return live_page_count;
}

[[nodiscard]] bool CompareCompatibilityKeys(const TransientCompatibilityKey& lhs_,
                                            const TransientCompatibilityKey& rhs_) noexcept {
    if (lhs_.kind != rhs_.kind) {
        return static_cast<std::uint32_t>(lhs_.kind) < static_cast<std::uint32_t>(rhs_.kind);
    }
    if (lhs_.usage_flags != rhs_.usage_flags) {
        return lhs_.usage_flags < rhs_.usage_flags;
    }
    if (lhs_.memory_type_bits != rhs_.memory_type_bits) {
        return lhs_.memory_type_bits < rhs_.memory_type_bits;
    }
    if (lhs_.dimension != rhs_.dimension) {
        return static_cast<std::uint32_t>(lhs_.dimension) < static_cast<std::uint32_t>(rhs_.dimension);
    }
    if (lhs_.format != rhs_.format) {
        return static_cast<std::uint32_t>(lhs_.format) < static_cast<std::uint32_t>(rhs_.format);
    }
    if (lhs_.extent.width != rhs_.extent.width) {
        return lhs_.extent.width < rhs_.extent.width;
    }
    if (lhs_.extent.height != rhs_.extent.height) {
        return lhs_.extent.height < rhs_.extent.height;
    }
    if (lhs_.extent.depth != rhs_.extent.depth) {
        return lhs_.extent.depth < rhs_.extent.depth;
    }
    if (lhs_.mip_level_count != rhs_.mip_level_count) {
        return lhs_.mip_level_count < rhs_.mip_level_count;
    }
    if (lhs_.array_layer_count != rhs_.array_layer_count) {
        return lhs_.array_layer_count < rhs_.array_layer_count;
    }
    if (lhs_.sample_count != rhs_.sample_count) {
        return static_cast<std::uint32_t>(lhs_.sample_count) < static_cast<std::uint32_t>(rhs_.sample_count);
    }
    if (lhs_.host_visible != rhs_.host_visible) {
        return lhs_.host_visible < rhs_.host_visible;
    }
    if (lhs_.persistently_mapped != rhs_.persistently_mapped) {
        return lhs_.persistently_mapped < rhs_.persistently_mapped;
    }
    if (lhs_.dedicated_required != rhs_.dedicated_required) {
        return lhs_.dedicated_required < rhs_.dedicated_required;
    }
    if (lhs_.lazy_memory_requested != rhs_.lazy_memory_requested) {
        return lhs_.lazy_memory_requested < rhs_.lazy_memory_requested;
    }
    return false;
}

void FinalizeAliasGroups(TransientAllocationPlan& plan_) {
    for (auto& page_ : plan_.pages) {
        if (page_.resources.size() <= 1U) {
            continue;
        }
        for (auto& record_ : plan_.records) {
            const bool on_page = std::find_if(page_.resources.begin(),
                                              page_.resources.end(),
                                              [&](const ResourceHandle resource_) {
                                                  return resource_.index == record_.resource.index;
                                              }) != page_.resources.end();
            if (!on_page) {
                continue;
            }
            record_.aliased = true;
            record_.alias_group = page_.page_index;
        }
    }
}

void BuildAliasBarriers(TransientAllocationPlan& plan_) {
    for (const auto& page_ : plan_.pages) {
        if (page_.resources.size() <= 1U) {
            continue;
        }

        std::vector<const TransientAllocationRecord*> sorted_records{};
        sorted_records.reserve(page_.resources.size());
        for (const auto handle_ : page_.resources) {
            const auto record_it = std::find_if(plan_.records.begin(),
                                                plan_.records.end(),
                                                [&](const TransientAllocationRecord& record_) {
                                                    return record_.resource.index == handle_.index;
                                                });
            if (record_it != plan_.records.end()) {
                sorted_records.push_back(&(*record_it));
            }
        }

        std::sort(sorted_records.begin(),
                  sorted_records.end(),
                  [](const TransientAllocationRecord* lhs_,
                     const TransientAllocationRecord* rhs_) {
                      if (lhs_->first_pass_order != rhs_->first_pass_order) {
                          return lhs_->first_pass_order < rhs_->first_pass_order;
                      }
                      if (lhs_->last_pass_order != rhs_->last_pass_order) {
                          return lhs_->last_pass_order < rhs_->last_pass_order;
                      }
                      return lhs_->resource.index < rhs_->resource.index;
                  });

        for (std::size_t index = 1U; index < sorted_records.size(); ++index) {
            const auto* previous = sorted_records[index - 1U];
            const auto* next = sorted_records[index];
            plan_.alias_barriers.push_back(AliasBarrierDecision{
                .previous = previous->resource,
                .next = next->resource,
                .previous_debug_name = previous->debug_name,
                .next_debug_name = next->debug_name,
                .previous_last_pass_order = previous->last_pass_order,
                .next_first_pass_order = next->first_pass_order,
                .page_index = page_.page_index,
                .required = true,
                .realized = false,
            });
        }
    }
}

} // namespace

bool TransientCompatibilityKeysEqual(const TransientCompatibilityKey& lhs_,
                                     const TransientCompatibilityKey& rhs_) noexcept {
    return !CompareCompatibilityKeys(lhs_, rhs_) &&
           !CompareCompatibilityKeys(rhs_, lhs_);
}

TransientAllocationPlan BuildTransientAllocationPlan(const CompiledRenderGraph& compiled_graph_,
                                                     const TransientFootprintProvider& footprint_provider_) {
    TransientAllocationPlan plan{};
    if (compiled_graph_.Resources().empty()) {
        return plan;
    }

    struct ResourceState final {
        AggregateLiveness liveness{};
        EligibilityResult eligibility{};
    };

    std::vector<ResourceState> states(compiled_graph_.Resources().size());
    plan.records.reserve(compiled_graph_.Resources().size());

    for (std::size_t resource_index = 0U;
         resource_index < compiled_graph_.Resources().size();
         ++resource_index) {
        const auto& resource_ = compiled_graph_.Resources()[resource_index];
        states[resource_index].liveness = ResolveAggregateLiveness(compiled_graph_, resource_.handle);
        states[resource_index].eligibility = EvaluateEligibility(resource_, footprint_provider_);

        plan.records.push_back(TransientAllocationRecord{
            .resource = resource_.handle,
            .debug_name = resource_.debug_name,
            .kind = resource_.kind,
            .lifetime = resource_.lifetime,
            .footprint = states[resource_index].eligibility.footprint,
            .compatibility = states[resource_index].eligibility.compatibility,
            .first_pass_order = states[resource_index].liveness.first_pass_order,
            .last_pass_order = states[resource_index].liveness.last_pass_order,
            .eligible = states[resource_index].eligibility.eligible,
            .rejection_reason = states[resource_index].eligibility.rejection_reason,
        });

        if (resource_.lifetime == ResourceLifetime::transient) {
            plan.timeline.transient_resource_count += 1U;
        }
        if (states[resource_index].eligibility.eligible) {
            plan.timeline.eligible_resource_count += 1U;
            plan.timeline.logical_total_bytes += states[resource_index].eligibility.footprint.size_bytes;
        }
    }

    for (std::size_t lhs_index = 0U; lhs_index < compiled_graph_.Resources().size(); ++lhs_index) {
        const auto& lhs_resource = compiled_graph_.Resources()[lhs_index];
        if (lhs_resource.lifetime != ResourceLifetime::transient) {
            continue;
        }
        for (std::size_t rhs_index = lhs_index + 1U;
             rhs_index < compiled_graph_.Resources().size();
             ++rhs_index) {
            const auto& rhs_resource = compiled_graph_.Resources()[rhs_index];
            if (rhs_resource.lifetime != ResourceLifetime::transient) {
                continue;
            }

            const bool same_class =
                states[lhs_index].eligibility.eligible &&
                states[rhs_index].eligibility.eligible &&
                TransientCompatibilityKeysEqual(states[lhs_index].eligibility.compatibility,
                                                states[rhs_index].eligibility.compatibility);
            const bool overlapping = LivenessOverlaps(states[lhs_index].liveness, states[rhs_index].liveness);
            const bool aliasable = same_class && !overlapping;

            std::string non_alias_reason{};
            if (!states[lhs_index].eligibility.eligible) {
                non_alias_reason = states[lhs_index].eligibility.rejection_reason;
            } else if (!states[rhs_index].eligibility.eligible) {
                non_alias_reason = states[rhs_index].eligibility.rejection_reason;
            } else if (!same_class) {
                non_alias_reason = "incompatible compatibility class";
            } else if (overlapping) {
                non_alias_reason = "overlapping liveness";
            }

            plan.alias_candidates.push_back(AliasCandidate{
                .first = lhs_resource.handle,
                .second = rhs_resource.handle,
                .first_debug_name = lhs_resource.debug_name,
                .second_debug_name = rhs_resource.debug_name,
                .kind = lhs_resource.kind,
                .same_compatibility_class = same_class,
                .overlapping_liveness = overlapping,
                .aliasable = aliasable,
                .non_alias_reason = std::move(non_alias_reason),
            });
        }
    }

    std::vector<std::size_t> eligible_indices{};
    eligible_indices.reserve(plan.records.size());
    for (std::size_t record_index = 0U; record_index < plan.records.size(); ++record_index) {
        if (plan.records[record_index].eligible) {
            eligible_indices.push_back(record_index);
        }
    }

    std::sort(eligible_indices.begin(),
              eligible_indices.end(),
              [&](const std::size_t lhs_index_,
                  const std::size_t rhs_index_) {
                  const auto& lhs_ = plan.records[lhs_index_];
                  const auto& rhs_ = plan.records[rhs_index_];
                  if (CompareCompatibilityKeys(lhs_.compatibility, rhs_.compatibility)) {
                      return true;
                  }
                  if (CompareCompatibilityKeys(rhs_.compatibility, lhs_.compatibility)) {
                      return false;
                  }
                  if (lhs_.footprint.size_bytes != rhs_.footprint.size_bytes) {
                      return lhs_.footprint.size_bytes > rhs_.footprint.size_bytes;
                  }
                  if (lhs_.first_pass_order != rhs_.first_pass_order) {
                      return lhs_.first_pass_order < rhs_.first_pass_order;
                  }
                  return lhs_.resource.index < rhs_.resource.index;
              });

    std::vector<PageAssignmentState> page_states{};
    for (const auto record_index : eligible_indices) {
        auto& record_ = plan.records[record_index];

        PageAssignmentState* selected_page = nullptr;
        for (auto& page_state_ : page_states) {
            if (!TransientCompatibilityKeysEqual(page_state_.page.compatibility, record_.compatibility)) {
                continue;
            }
            if (page_state_.available_after_pass_order < record_.first_pass_order) {
                selected_page = &page_state_;
                break;
            }
        }

        if (selected_page == nullptr) {
            PageAssignmentState page_state{};
            page_state.page.page_index = static_cast<std::uint32_t>(page_states.size());
            page_state.page.kind = record_.kind;
            page_state.page.compatibility = record_.compatibility;
            page_state.page.size_bytes = record_.footprint.size_bytes;
            page_state.page.alignment_bytes = record_.footprint.alignment_bytes;
            page_state.record_indices.push_back(record_index);
            page_state.page.resources.push_back(record_.resource);
            page_state.available_after_pass_order = record_.last_pass_order;
            page_states.push_back(std::move(page_state));
            selected_page = &page_states.back();
        } else {
            selected_page->page.size_bytes = (std::max)(selected_page->page.size_bytes,
                                                        record_.footprint.size_bytes);
            selected_page->page.alignment_bytes = (std::max)(selected_page->page.alignment_bytes,
                                                             record_.footprint.alignment_bytes);
            selected_page->record_indices.push_back(record_index);
            selected_page->page.resources.push_back(record_.resource);
            selected_page->available_after_pass_order = record_.last_pass_order;
        }

        record_.page_index = selected_page->page.page_index;
        record_.page_offset_bytes = 0U;
        record_.alias_group = selected_page->page.page_index;
    }

    plan.pages.reserve(page_states.size());
    for (auto& page_state_ : page_states) {
        plan.timeline.physical_total_bytes += page_state_.page.size_bytes;
        plan.pages.push_back(std::move(page_state_.page));
    }
    plan.timeline.page_count = static_cast<std::uint32_t>(plan.pages.size());
    plan.timeline.saved_bytes = plan.timeline.logical_total_bytes >= plan.timeline.physical_total_bytes
        ? plan.timeline.logical_total_bytes - plan.timeline.physical_total_bytes
        : 0U;

    FinalizeAliasGroups(plan);
    BuildAliasBarriers(plan);
    plan.timeline.alias_barrier_count = static_cast<std::uint32_t>(plan.alias_barriers.size());
    for (const auto& record_ : plan.records) {
        if (record_.aliased) {
            plan.timeline.aliased_resource_count += 1U;
        }
    }

    std::uint32_t max_pass_order = 0U;
    bool have_passes = false;
    for (const auto& record_ : plan.records) {
        if (!record_.eligible) {
            continue;
        }
        max_pass_order = (std::max)(max_pass_order, record_.last_pass_order);
        have_passes = true;
    }

    if (have_passes) {
        plan.timeline.samples.reserve(static_cast<std::size_t>(max_pass_order) + 1U);
        for (std::uint32_t pass_order = 0U; pass_order <= max_pass_order; ++pass_order) {
            const std::uint64_t logical_live_bytes = PassLiveBytes(plan, pass_order, false);
            const std::uint64_t physical_live_bytes = PassLiveBytes(plan, pass_order, true);
            const std::uint32_t live_page_count = PassLivePageCount(plan, pass_order);
            plan.timeline.samples.push_back(TransientMemoryTimelineSample{
                .pass_order = pass_order,
                .logical_live_bytes = logical_live_bytes,
                .physical_live_bytes = physical_live_bytes,
                .live_page_count = live_page_count,
            });
            plan.timeline.peak_logical_live_bytes = (std::max)(plan.timeline.peak_logical_live_bytes,
                                                               logical_live_bytes);
            plan.timeline.peak_live_bytes = (std::max)(plan.timeline.peak_live_bytes,
                                                       physical_live_bytes);
        }
    }

    return plan;
}

} // namespace vr::render_graph
