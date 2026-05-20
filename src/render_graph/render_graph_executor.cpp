#include "vr/render_graph/render_graph_executor.hpp"

#ifdef FindResource
#undef FindResource
#endif

#include "vr/render_graph/compiled_render_graph.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace vr::render_graph {

namespace {

template<typename HandleT>
[[nodiscard]] HandleT RehydrateHandle(const std::uintptr_t bits_) noexcept {
    if constexpr (std::is_pointer_v<HandleT>) {
        return reinterpret_cast<HandleT>(bits_);
    } else {
        return static_cast<HandleT>(bits_);
    }
}

[[nodiscard]] VkShaderStageFlags ToVkShaderStageFlags(const std::uint32_t stage_flags_) {
    VkShaderStageFlags vk_flags = 0U;
    if (HasShaderStageFlag(stage_flags_, shader_stage_vertex_flag)) {
        vk_flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (HasShaderStageFlag(stage_flags_, shader_stage_fragment_flag)) {
        vk_flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (HasShaderStageFlag(stage_flags_, shader_stage_compute_flag)) {
        vk_flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    if (vk_flags == 0U) {
        throw std::runtime_error(
            "RenderGraphExecutor::Record encountered descriptor binding with empty stage flags");
    }
    return vk_flags;
}

[[nodiscard]] VkDescriptorType ToVkDescriptorType(const DescriptorBindingKind kind_) {
    switch (kind_) {
    case DescriptorBindingKind::storage_buffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case DescriptorBindingKind::uniform_buffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case DescriptorBindingKind::sampled_image_table:
    case DescriptorBindingKind::sampler_table:
    default:
        break;
    }
    throw std::runtime_error(
        "RenderGraphExecutor::Record encountered unsupported transient descriptor binding kind");
}

[[nodiscard]] VkDescriptorSet ResolveDirectBindlessDescriptorSet(
    GraphCommandContext& context_,
    const DescriptorWriteDesc& write_) {
    if (write_.binding != 0U) {
        throw std::runtime_error(
            "RenderGraphExecutor::Record requires direct bindless descriptor sets to use binding 0");
    }
    return context_.Descriptors().GetBindlessDescriptorSet(
        render::BindlessTableId{.value = write_.source_id});
}

[[nodiscard]] VkDescriptorSet StageTransientDescriptorSetForWrites(
    GraphCommandContext& context_,
    const PassDescriptorBindingDesc* const layout_begin_,
    const PassDescriptorBindingDesc* const layout_end_,
    const DescriptorWriteDesc* const writes_begin_,
    const DescriptorWriteDesc* const writes_end_) {
    if (layout_begin_ == layout_end_) {
        throw std::runtime_error(
            "RenderGraphExecutor::Record cannot stage an empty descriptor set layout");
    }
    if (writes_begin_ == writes_end_) {
        throw std::runtime_error(
            "RenderGraphExecutor::Record cannot stage a descriptor set without writes");
    }

    render::DescriptorSetLayoutDesc layout_desc{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> buffer_writes{};
    render::DescriptorMcVector<render::DescriptorImageWrite> image_writes{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> texel_writes{};

    for (auto layout_it = layout_begin_; layout_it != layout_end_; ++layout_it) {
        VkDescriptorSetLayoutBinding layout_binding{};
        layout_binding.binding = layout_it->binding;
        layout_binding.descriptorType = ToVkDescriptorType(layout_it->kind);
        layout_binding.descriptorCount = 1U;
        layout_binding.stageFlags = ToVkShaderStageFlags(layout_it->stage_flags);
        layout_binding.pImmutableSamplers = nullptr;
        layout_desc.bindings.push_back(layout_binding);
    }

    for (auto write_it = writes_begin_; write_it != writes_end_; ++write_it) {
        const DescriptorWriteDesc& write = *write_it;
        const auto layout_it = std::find_if(
            layout_begin_,
            layout_end_,
            [&](const PassDescriptorBindingDesc& layout_binding_) {
                return layout_binding_.binding == write.binding;
            });
        if (layout_it == layout_end_) {
            throw std::runtime_error(
                "RenderGraphExecutor::Record encountered a descriptor write without matching pass layout binding");
        }
        if (write.source == DescriptorBindingSource::bindless_table) {
            throw std::runtime_error(
                "RenderGraphExecutor::Record does not support lowering mixed bindless/transient descriptor sets");
        }
        if (write.source != DescriptorBindingSource::external_buffer) {
            throw std::runtime_error(
                "RenderGraphExecutor::Record encountered unsupported transient descriptor binding source");
        }

        const auto* resolver = context_.Graph().FindExternalBufferBindingResolver(write.source_id);
        if (resolver == nullptr || resolver->resolve_fn == nullptr) {
            throw std::runtime_error(
                "RenderGraphExecutor::Record could not resolve external buffer binding resolver");
        }

        const ExternalBufferBindingPayload payload =
            resolver->resolve_fn(resolver->user_data);
        if (payload.native_buffer == 0U || payload.size_bytes == 0U) {
            throw std::runtime_error(
                "RenderGraphExecutor::Record resolved invalid external buffer descriptor payload");
        }

        buffer_writes.push_back({
            .binding = write.binding,
            .array_element = 0U,
            .descriptor_type = ToVkDescriptorType(layout_it->kind),
            .buffer = RehydrateHandle<VkBuffer>(payload.native_buffer),
            .offset = payload.offset_bytes,
            .range = payload.size_bytes,
        });
    }

    const render::DescriptorSetLayoutId layout_id =
        context_.Descriptors().RegisterLayout(context_.Device(), layout_desc);
    const VkDescriptorSet descriptor_set =
        context_.Descriptors().AllocateSet(context_.Device(),
                                           context_.FrameIndex(),
                                           layout_id);
    if (descriptor_set == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "RenderGraphExecutor::Record failed to allocate transient descriptor set");
    }

    context_.Descriptors().UpdateSet(context_.Device(),
                                     descriptor_set,
                                     buffer_writes,
                                     image_writes,
                                     texel_writes);
    return descriptor_set;
}

void PrepareDescriptorSetsForPass(GraphCommandContext& context_,
                                  const CompiledPass& pass_) {
    const auto& descriptor_plan = context_.Graph().DescriptorPlan();
    const auto pass_layout_it = std::find_if(
        descriptor_plan.pass_layouts.begin(),
        descriptor_plan.pass_layouts.end(),
        [&](const PassDescriptorLayout& layout_) {
            return layout_.pass.index == pass_.handle.index;
        });
    const auto write_batch_it = std::find_if(
        descriptor_plan.writes.begin(),
        descriptor_plan.writes.end(),
        [&](const DescriptorWriteBatch& batch_) {
            return batch_.pass.index == pass_.handle.index;
        });
    if (pass_layout_it == descriptor_plan.pass_layouts.end()) {
        if (write_batch_it != descriptor_plan.writes.end()) {
            throw std::runtime_error(
                "RenderGraphExecutor::Record encountered descriptor writes without a pass layout");
        }
        context_.SetCurrentPassDescriptorSets({});
        return;
    }
    if (write_batch_it == descriptor_plan.writes.end()) {
        throw std::runtime_error(
            "RenderGraphExecutor::Record encountered a descriptor layout without writes");
    }
    if (!context_.HasDescriptorHost()) {
        throw std::runtime_error(
            "RenderGraphExecutor::Record requires DescriptorHost for passes with descriptor bindings");
    }

    std::uint32_t max_set_index = 0U;
    for (const auto& layout_binding : pass_layout_it->bindings) {
        max_set_index = std::max(max_set_index, layout_binding.set);
    }

    std::vector<VkDescriptorSet> descriptor_sets(max_set_index + 1U, VK_NULL_HANDLE);
    const PassDescriptorBindingDesc* layout_begin = pass_layout_it->bindings.data();
    const PassDescriptorBindingDesc* const layout_end =
        layout_begin + pass_layout_it->bindings.size();
    const DescriptorWriteDesc* const writes_data = write_batch_it->writes.data();
    while (layout_begin != layout_end) {
        const std::uint32_t set_index = layout_begin->set;
        const PassDescriptorBindingDesc* layout_end_for_set = layout_begin + 1;
        while (layout_end_for_set != layout_end &&
               layout_end_for_set->set == set_index) {
            ++layout_end_for_set;
        }

        const auto writes_begin = std::find_if(
            write_batch_it->writes.begin(),
            write_batch_it->writes.end(),
            [&](const DescriptorWriteDesc& write_) {
                return write_.set == set_index;
            });
        if (writes_begin == write_batch_it->writes.end()) {
            throw std::runtime_error(
                "RenderGraphExecutor::Record encountered a descriptor set layout without any writes for that set");
        }
        auto writes_end_for_set = writes_begin;
        while (writes_end_for_set != write_batch_it->writes.end() &&
               writes_end_for_set->set == set_index) {
            ++writes_end_for_set;
        }
        const std::size_t writes_begin_index =
            static_cast<std::size_t>(std::distance(write_batch_it->writes.begin(), writes_begin));
        const std::size_t writes_end_index =
            static_cast<std::size_t>(std::distance(write_batch_it->writes.begin(), writes_end_for_set));
        const DescriptorWriteDesc* const writes_begin_ptr = writes_data + writes_begin_index;
        const DescriptorWriteDesc* const writes_end_ptr = writes_data + writes_end_index;

        if ((layout_end_for_set - layout_begin) == 1 &&
            std::distance(writes_begin, writes_end_for_set) == 1 &&
            writes_begin->source == DescriptorBindingSource::bindless_table) {
            descriptor_sets[set_index] =
                ResolveDirectBindlessDescriptorSet(context_, *writes_begin);
        } else {
            descriptor_sets[set_index] =
                StageTransientDescriptorSetForWrites(context_,
                                                     layout_begin,
                                                     layout_end_for_set,
                                                     writes_begin_ptr,
                                                     writes_end_ptr);
        }
        if (descriptor_sets[set_index] == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "RenderGraphExecutor::Record resolved null descriptor set from descriptor binding plan");
        }
        layout_begin = layout_end_for_set;
    }

    context_.SetCurrentPassDescriptorSets(std::move(descriptor_sets));
}

void EmitDependencyInfo(const VkCommandBuffer command_buffer_,
                        const VulkanDependencyInfoData* const dependency_,
                        RenderGraphRecordStats& stats_) {
    if (command_buffer_ == VK_NULL_HANDLE || dependency_ == nullptr) {
        return;
    }

    const VkDependencyInfo dependency_info = dependency_->BuildVkDependencyInfo();
    if (dependency_info.memoryBarrierCount == 0U &&
        dependency_info.bufferMemoryBarrierCount == 0U &&
        dependency_info.imageMemoryBarrierCount == 0U) {
        return;
    }

    vkCmdPipelineBarrier2(command_buffer_, &dependency_info);
    stats_.command_batch_count += 1U;
    stats_.memory_barrier_count +=
        static_cast<std::uint32_t>(dependency_->memory_barriers.size());
    stats_.buffer_barrier_count +=
        static_cast<std::uint32_t>(dependency_->buffer_barriers.size());
    stats_.image_barrier_count +=
        static_cast<std::uint32_t>(dependency_->image_barriers.size());
}

[[nodiscard]] std::vector<const VulkanCommandBarrierBatch*> BuildCommandBatchesByPass(
    const GraphCommandContext& context_) {
    std::vector<const VulkanCommandBarrierBatch*> command_batches_by_pass(
        context_.Graph().Passes().size(),
        nullptr);
    for (const auto& batch_ : context_.CommandReadyBarriers().command_batches) {
        if (batch_.pass.index < command_batches_by_pass.size()) {
            command_batches_by_pass[batch_.pass.index] = &batch_;
        }
    }
    return command_batches_by_pass;
}

void RecordPass(GraphCommandContext& context_,
                const CompiledPass& pass_,
                const std::vector<const VulkanCommandBarrierBatch*>& command_batches_by_pass_,
                RenderGraphRecordStats& stats_) {
    const auto* command_batch = (pass_.handle.index < command_batches_by_pass_.size())
        ? command_batches_by_pass_[pass_.handle.index]
        : nullptr;

    EmitDependencyInfo(context_.CommandBuffer(),
                       command_batch != nullptr ? &command_batch->dependency : nullptr,
                       stats_);

    const bool has_raster_pass = pass_.raster_pass.has_value();
    if (!has_raster_pass && !pass_.execute) {
        return;
    }

    context_.SetCurrentPass(pass_.handle);
    PrepareDescriptorSetsForPass(context_, pass_);

    if (has_raster_pass) {
        const auto rendering_info = context_.BuildRenderingInfo(*pass_.raster_pass);
        context_.BeginRendering(rendering_info);
    }

    if (pass_.execute) {
        pass_.execute(context_);
    }

    if (has_raster_pass) {
        context_.EndRendering();
    }
    context_.ClearCurrentPass();
    stats_.pass_count += 1U;
}

} // namespace

RenderGraphRecordStats RenderGraphExecutor::Record(GraphCommandContext& context_) {
    RenderGraphRecordStats stats{};

    const VkCommandBuffer command_buffer = context_.CommandBuffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return stats;
    }

    const auto& command_ready = context_.CommandReadyBarriers();
    if (!command_ready.queue_transfer_batches.empty()) {
        throw std::runtime_error(
            "RenderGraphExecutor::Record currently supports only single-queue command batches");
    }

    const auto command_batches_by_pass = BuildCommandBatchesByPass(context_);

    for (const auto pass_handle_ : context_.Graph().ExecutionOrder()) {
        const auto* pass_ = context_.Graph().FindPass(pass_handle_);
        if (pass_ == nullptr) {
            continue;
        }
        RecordPass(context_, *pass_, command_batches_by_pass, stats);
    }

    stats.queue_transfer_batch_count = static_cast<std::uint32_t>(command_ready.queue_transfer_batches.size());
    return stats;
}

RenderGraphRecordStats RenderGraphExecutor::RecordQueueBatch(
    GraphCommandContext& context_,
    const QueueSubmitBatch& queue_batch_,
    const VulkanDependencyInfoData* begin_dependency_,
    const VulkanDependencyInfoData* end_dependency_) {
    RenderGraphRecordStats stats{};
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        return stats;
    }

    const auto command_batches_by_pass = BuildCommandBatchesByPass(context_);
    EmitDependencyInfo(context_.CommandBuffer(), begin_dependency_, stats);

    for (const auto pass_handle_ : queue_batch_.passes) {
        const auto* pass_ = context_.Graph().FindPass(pass_handle_);
        if (pass_ == nullptr) {
            continue;
        }
        RecordPass(context_, *pass_, command_batches_by_pass, stats);
    }

    EmitDependencyInfo(context_.CommandBuffer(), end_dependency_, stats);
    return stats;
}

} // namespace vr::render_graph
