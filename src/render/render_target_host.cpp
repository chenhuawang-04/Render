#include "vr/render/render_target_host.hpp"

#include "vr/render/descriptor_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::render {

namespace {

[[nodiscard]] bool IsDepthAspect(VkImageAspectFlags aspect_mask_) noexcept {
    return (aspect_mask_ & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0U;
}

[[nodiscard]] bool OwnedDescStructurallyEquals(const RenderTargetDesc& lhs_,
                                               const RenderTargetDesc& rhs_) noexcept {
    return lhs_.dimension == rhs_.dimension &&
           lhs_.lifetime == rhs_.lifetime &&
           lhs_.scale_mode == rhs_.scale_mode &&
           lhs_.width == rhs_.width &&
           lhs_.height == rhs_.height &&
           lhs_.depth == rhs_.depth &&
           lhs_.width_scale == rhs_.width_scale &&
           lhs_.height_scale == rhs_.height_scale &&
           lhs_.flags == rhs_.flags &&
           lhs_.format == rhs_.format &&
           lhs_.samples == rhs_.samples &&
           lhs_.usage == rhs_.usage &&
           lhs_.aspect == rhs_.aspect &&
           lhs_.mip_levels == rhs_.mip_levels &&
           lhs_.array_layers == rhs_.array_layers &&
           lhs_.color_encoding == rhs_.color_encoding &&
           lhs_.memory_policy == rhs_.memory_policy &&
           lhs_.allow_uav == rhs_.allow_uav &&
           lhs_.allow_alias == rhs_.allow_alias &&
           lhs_.allow_history == rhs_.allow_history;
}

} // namespace

void RenderTargetHost::Initialize(VulkanContext& context_,
                                  resource::GpuMemoryHost& gpu_memory_host_,
                                  const RenderTargetHostCreateInfo& create_info_) {
    if (initialized) {
        Shutdown(context_);
    }

    gpu_memory_host = &gpu_memory_host_;
    create_info_cache = create_info_;
    targets.clear();
    generations.clear();
    free_indices.clear();
    retired_targets.Clear();
    bindless_config = {};
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;

    if (create_info_cache.reserve_target_count > 0U) {
        targets.reserve(create_info_cache.reserve_target_count);
        generations.reserve(create_info_cache.reserve_target_count);
    }
    if (create_info_cache.reserve_free_index_count > 0U) {
        free_indices.reserve(create_info_cache.reserve_free_index_count);
    }
    if (create_info_cache.reserve_retired_target_count > 0U) {
        retired_targets.Reserve(create_info_cache.reserve_retired_target_count);
    }

    initialized = true;
}

void RenderTargetHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.Device());
    }

    for (auto& target : targets) {
        if (!target.active) {
            continue;
        }
        RetireRecord(target, 0U);
        ResetRecord(target);
    }
    DestroyRetiredTargets(context_);

    targets.clear();
    generations.clear();
    free_indices.clear();
    gpu_memory_host = nullptr;
    bindless_config = {};
    create_info_cache = {};
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = false;
}

void RenderTargetHost::BeginFrame(VulkanContext& context_,
                                  std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetHost::BeginFrame called before Initialize");
    }

    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredTargets(context_, completed_submit_value_);
    RefreshStats();
}

void RenderTargetHost::ConfigureBindless(
    const RenderTargetHostBindlessConfig& bindless_config_) {
    if (bindless_config.SameBinding(bindless_config_)) {
        return;
    }
    InvalidateBindlessRecords(bindless_config);
    bindless_config = bindless_config_;
}

RenderTargetHandle RenderTargetHost::CreatePersistentTarget(VulkanContext& context_,
                                                            const RenderTargetDesc& desc_,
                                                            VkExtent2D reference_extent_) {
    RenderTargetDesc normalized_desc = desc_;
    normalized_desc.lifetime = RenderTargetLifetime::persistent;
    return CreateOwnedTarget(context_, normalized_desc, reference_extent_);
}

RenderTargetHandle RenderTargetHost::CreateTransientTarget(VulkanContext& context_,
                                                           const RenderTargetDesc& desc_,
                                                           VkExtent2D reference_extent_) {
    RenderTargetDesc normalized_desc = desc_;
    normalized_desc.lifetime = RenderTargetLifetime::transient;
    return CreateOwnedTarget(context_, normalized_desc, reference_extent_);
}

RenderTargetHandle RenderTargetHost::CreateOwnedTarget(VulkanContext& context_,
                                                       const RenderTargetDesc& desc_,
                                                       VkExtent2D reference_extent_) {
    if (!initialized || gpu_memory_host == nullptr) {
        throw std::runtime_error("RenderTargetHost::CreateOwnedTarget called before Initialize");
    }

    ValidateOwnedDesc(desc_);

    const std::uint32_t slot_index = AllocateSlot();
    TargetRecord& record = targets[slot_index];
    ResetRecord(record);

    try {
        record.handle = RenderTargetHandle{
            .index = slot_index,
            .generation = generations[slot_index],
        };
        record.desc = desc_;
        record.ownership = RenderTargetOwnership::owned;
        record.owned_resource = resource::ImageHost::CreateImage(
            context_,
            BuildOwnedImageCreateInfo(context_, create_info_cache, desc_, reference_extent_),
            *gpu_memory_host);
        record.default_view_desc = RenderTargetViewDesc{
            .view_type = DefaultViewType(desc_.dimension, desc_.array_layers),
            .aspect = desc_.aspect,
            .base_mip_level = 0U,
            .level_count = desc_.mip_levels,
            .base_array_layer = 0U,
            .layer_count = desc_.array_layers,
        };
        record.format = desc_.format;
        record.extent = record.owned_resource.extent;
        record.samples = desc_.samples;
        record.usage = desc_.usage;
        record.aspect = desc_.aspect;
        record.state = RenderTargetStateKind::undefined;
        record.resource_revision = 1U;
        record.active = true;
    } catch (...) {
        ResetRecord(record);
        free_indices.push_back(slot_index);
        throw;
    }

    ++stats.resource_revision;
    RefreshStats();
    return record.handle;
}

EnsureRenderTargetResult RenderTargetHost::EnsurePersistentTarget(
    VulkanContext& context_,
    RenderTargetHandle current_handle_,
    const RenderTargetDesc& desc_,
    VkExtent2D reference_extent_,
    std::uint64_t last_submitted_value_,
    std::uint64_t completed_submit_value_) {
    if (!initialized || gpu_memory_host == nullptr) {
        throw std::runtime_error("RenderTargetHost::EnsurePersistentTarget called before Initialize");
    }

    RenderTargetDesc normalized_desc = desc_;
    if (normalized_desc.lifetime == RenderTargetLifetime::transient ||
        normalized_desc.lifetime == RenderTargetLifetime::imported) {
        throw std::invalid_argument(
            "RenderTargetHost::EnsurePersistentTarget requires persistent/history desc");
    }
    ValidateOwnedDesc(normalized_desc);
    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredTargets(context_, completed_submit_value_);

    EnsureRenderTargetResult result{};
    TargetRecord* record = Resolve(current_handle_);
    if (record == nullptr) {
        result.handle = (normalized_desc.lifetime == RenderTargetLifetime::history)
            ? CreateHistoryTarget(context_, normalized_desc, reference_extent_)
            : CreatePersistentTarget(context_, normalized_desc, reference_extent_);
        result.created = true;
        result.revision_changed = true;
        return result;
    }

    if (record->ownership != RenderTargetOwnership::owned) {
        throw std::invalid_argument(
            "RenderTargetHost::EnsurePersistentTarget requires an owned target handle");
    }

    const VkExtent3D desired_extent = ResolveExtent(normalized_desc, reference_extent_);
    const bool requires_recreate =
        !OwnedDescStructurallyEquals(record->desc, normalized_desc) ||
        record->extent.width != desired_extent.width ||
        record->extent.height != desired_extent.height ||
        record->extent.depth != desired_extent.depth;

    record->desc.debug_name = normalized_desc.debug_name;
    result.handle = record->handle;

    if (!requires_recreate) {
        return result;
    }

    resource::ImageResource replacement_resource = resource::ImageHost::CreateImage(
        context_,
        BuildOwnedImageCreateInfo(context_, create_info_cache, normalized_desc, reference_extent_),
        *gpu_memory_host);

    RetiredTargetPayload payload{};
    payload.owned_resource = std::move(record->owned_resource);
    if (payload.owned_resource.image != VK_NULL_HANDLE) {
        retired_targets.Retire(std::move(payload), last_submitted_value_);
    }

    record->desc = normalized_desc;
    record->owned_resource = std::move(replacement_resource);
    record->default_view_desc = RenderTargetViewDesc{
        .view_type = DefaultViewType(normalized_desc.dimension, normalized_desc.array_layers),
        .aspect = normalized_desc.aspect,
        .base_mip_level = 0U,
        .level_count = normalized_desc.mip_levels,
        .base_array_layer = 0U,
        .layer_count = normalized_desc.array_layers,
    };
    record->format = normalized_desc.format;
    record->extent = record->owned_resource.extent;
    record->samples = normalized_desc.samples;
    record->usage = normalized_desc.usage;
    record->aspect = normalized_desc.aspect;
    record->state = RenderTargetStateKind::undefined;
    if (record->resource_revision == (std::numeric_limits<std::uint32_t>::max)()) {
        record->resource_revision = 1U;
    } else {
        ++record->resource_revision;
    }
    ++stats.resource_revision;
    RefreshStats();

    result.recreated = true;
    result.revision_changed = true;
    return result;
}

RenderTargetHandle RenderTargetHost::CreateHistoryTarget(VulkanContext& context_,
                                                         const RenderTargetDesc& desc_,
                                                         VkExtent2D reference_extent_) {
    RenderTargetDesc normalized_desc = desc_;
    normalized_desc.lifetime = RenderTargetLifetime::history;
    return CreateOwnedTarget(context_, normalized_desc, reference_extent_);
}

RenderTargetHandle RenderTargetHost::ImportTarget(VulkanContext& context_,
                                                  const ImportedRenderTargetDesc& desc_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetHost::ImportTarget called before Initialize");
    }

    ValidateImportedDesc(desc_);

    const std::uint32_t slot_index = AllocateSlot();
    TargetRecord& record = targets[slot_index];
    ResetRecord(record);

    try {
        record.handle = RenderTargetHandle{
            .index = slot_index,
            .generation = generations[slot_index],
        };
        record.desc.debug_name = desc_.debug_name;
        record.desc.dimension = desc_.dimension;
        record.desc.lifetime = RenderTargetLifetime::imported;
        record.desc.scale_mode = RenderTargetScaleMode::absolute;
        record.desc.width = desc_.extent.width;
        record.desc.height = desc_.extent.height;
        record.desc.depth = desc_.extent.depth;
        record.desc.format = desc_.format;
        record.desc.samples = desc_.samples;
        record.desc.usage = desc_.usage;
        record.desc.aspect = desc_.aspect;
        record.desc.mip_levels = desc_.mip_levels;
        record.desc.array_layers = desc_.array_layers;
        record.desc.color_encoding = desc_.color_encoding;
        record.desc.memory_policy = RenderTargetMemoryPolicy::auto_select;
        record.imported_image = desc_.image;
        record.imported_view = desc_.image_view;
        record.ownership = desc_.ownership;
        record.default_view_desc = RenderTargetViewDesc{
            .view_type = DefaultViewType(desc_.dimension, desc_.array_layers),
            .aspect = desc_.aspect,
            .base_mip_level = 0U,
            .level_count = desc_.mip_levels,
            .base_array_layer = 0U,
            .layer_count = desc_.array_layers,
        };

        if (record.imported_view == VK_NULL_HANDLE &&
            record.ownership == RenderTargetOwnership::imported_image_owned_view) {
            VkImageViewCreateInfo view_create_info{};
            view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_create_info.image = desc_.image;
            view_create_info.viewType = record.default_view_desc.view_type;
            view_create_info.format = desc_.format;
            view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_create_info.subresourceRange.aspectMask = desc_.aspect;
            view_create_info.subresourceRange.baseMipLevel = 0U;
            view_create_info.subresourceRange.levelCount = desc_.mip_levels;
            view_create_info.subresourceRange.baseArrayLayer = 0U;
            view_create_info.subresourceRange.layerCount = desc_.array_layers;
            record.imported_view = resource::ImageHost::CreateView(context_, desc_.image, view_create_info);
        }

        record.format = desc_.format;
        record.extent = desc_.extent;
        record.samples = desc_.samples;
        record.usage = desc_.usage;
        record.aspect = desc_.aspect;
        record.state = desc_.initial_state;
        record.resource_revision = 1U;
        record.active = true;
    } catch (...) {
        if (record.imported_view != VK_NULL_HANDLE &&
            desc_.image_view == VK_NULL_HANDLE &&
            desc_.ownership == RenderTargetOwnership::imported_image_owned_view) {
            resource::ImageHost::DestroyView(context_, record.imported_view);
        }
        ResetRecord(record);
        free_indices.push_back(slot_index);
        throw;
    }

    ++stats.resource_revision;
    RefreshStats();
    return record.handle;
}

bool RenderTargetHost::DestroyTarget(VulkanContext& context_,
                                     RenderTargetHandle handle_,
                                     std::uint64_t last_submitted_value_,
                                     std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetHost::DestroyTarget called before Initialize");
    }

    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredTargets(context_, completed_submit_value_);
    TargetRecord* record = Resolve(handle_);
    if (record == nullptr) {
        return false;
    }

    RetireRecord(*record, last_submitted_value_);
    ResetRecord(*record);
    record->active = false;

    if (handle_.index < generations.size()) {
        if (generations[handle_.index] == (std::numeric_limits<std::uint32_t>::max)()) {
            generations[handle_.index] = 1U;
        } else {
            ++generations[handle_.index];
        }
    }
    free_indices.push_back(handle_.index);

    ++stats.resource_revision;
    RefreshStats();
    return true;
}

const RenderTargetHost::TargetRecord* RenderTargetHost::Resolve(RenderTargetHandle handle_) const noexcept {
    if (!IsValidRenderTargetHandle(handle_) ||
        handle_.index >= targets.size() ||
        handle_.index >= generations.size()) {
        return nullptr;
    }

    const TargetRecord& record = targets[handle_.index];
    if (!record.active || generations[handle_.index] != handle_.generation) {
        return nullptr;
    }
    return &record;
}

RenderTargetHost::TargetRecord* RenderTargetHost::Resolve(RenderTargetHandle handle_) noexcept {
    return const_cast<TargetRecord*>(std::as_const(*this).Resolve(handle_));
}

BindlessSlot RenderTargetHost::ResolveBindlessImageSlot(RenderTargetHandle handle_) const noexcept {
    const TargetRecord* record = Resolve(handle_);
    if (record == nullptr) {
        return {};
    }
    return record->bindless_image_slot;
}

BindlessSlot RenderTargetHost::EnsureBindlessImageSlot(RenderTargetHandle handle_) {
    TargetRecord* record = Resolve(handle_);
    if (record == nullptr) {
        throw std::out_of_range("RenderTargetHost::EnsureBindlessImageSlot invalid render target handle");
    }
    if (!bindless_config.Enabled()) {
        return {};
    }
    if (!SupportsBindlessSampling(*record)) {
        return {};
    }

    const VkImageLayout sampled_layout = DescribeState(RenderTargetStateKind::shader_read, record->aspect).layout;
    const VkImageView image_view = ResolveDefaultImageView(*record);
    if (image_view == VK_NULL_HANDLE) {
        return {};
    }

    if (!record->bindless_image_slot.IsValid()) {
        record->bindless_image_slot =
            bindless_config.descriptor_host->AllocateBindlessSlot(bindless_config.image_table);
        record->bindless_resource_revision_written = 0U;
    }

    if (record->bindless_resource_revision_written != record->resource_revision) {
        bindless_config.descriptor_host->QueueBindlessImageWrite(bindless_config.image_table,
                                                                 record->bindless_image_slot,
                                                                 image_view,
                                                                 sampled_layout);
        record->bindless_resource_revision_written = record->resource_revision;
    }

    return record->bindless_image_slot;
}

RenderTargetResolvedView RenderTargetHost::ResolveView(RenderTargetHandle handle_) const {
    const TargetRecord* record = Resolve(handle_);
    if (record == nullptr) {
        throw std::out_of_range("RenderTargetHost::ResolveView invalid render target handle");
    }

    RenderTargetResolvedView resolved{};
    resolved.handle = record->handle;
    resolved.image = (record->ownership == RenderTargetOwnership::owned)
        ? record->owned_resource.image
        : record->imported_image;
    resolved.image_view = GetDefaultView(handle_);
    resolved.format = record->format;
    resolved.extent = record->extent;
    resolved.samples = record->samples;
    resolved.usage = record->usage;
    resolved.aspect = record->aspect;
    resolved.color_encoding = record->desc.color_encoding;
    resolved.lifetime = record->desc.lifetime;
    resolved.ownership = record->ownership;
    resolved.state = record->state;
    resolved.resource_revision = record->resource_revision;
    return resolved;
}

VkImageView RenderTargetHost::GetDefaultView(RenderTargetHandle handle_) const {
    const TargetRecord* record = Resolve(handle_);
    if (record == nullptr) {
        throw std::out_of_range("RenderTargetHost::GetDefaultView invalid render target handle");
    }

    return (record->ownership == RenderTargetOwnership::owned)
        ? record->owned_resource.default_view
        : record->imported_view;
}

VkImageSubresourceRange RenderTargetHost::DefaultSubresourceRange(RenderTargetHandle handle_) const {
    const TargetRecord* record = Resolve(handle_);
    if (record == nullptr) {
        throw std::out_of_range("RenderTargetHost::DefaultSubresourceRange invalid render target handle");
    }

    VkImageSubresourceRange range{};
    range.aspectMask = record->aspect;
    range.baseMipLevel = 0U;
    range.levelCount = record->desc.mip_levels;
    range.baseArrayLayer = 0U;
    range.layerCount = record->desc.array_layers;
    return range;
}

RenderTargetPipelineSignature RenderTargetHost::BuildPipelineSignature(
    const AttachmentRef* color_attachments_,
    std::uint32_t color_attachment_count_,
    const AttachmentRef* depth_attachment_,
    const AttachmentRef* stencil_attachment_) const {
    if (color_attachment_count_ > k_max_render_target_color_attachments) {
        throw std::out_of_range("RenderTargetHost::BuildPipelineSignature color attachment count exceeds limit");
    }

    RenderTargetPipelineSignature signature{};
    signature.color_attachment_count = color_attachment_count_;
    for (std::uint32_t index = 0U; index < color_attachment_count_; ++index) {
        const TargetRecord* record = Resolve(color_attachments_[index].target);
        if (record == nullptr) {
            throw std::out_of_range("RenderTargetHost::BuildPipelineSignature invalid color attachment handle");
        }
        signature.color_formats[index] = record->format;
        signature.samples = record->samples;
    }

    if (depth_attachment_ != nullptr) {
        const TargetRecord* record = Resolve(depth_attachment_->target);
        if (record == nullptr) {
            throw std::out_of_range("RenderTargetHost::BuildPipelineSignature invalid depth attachment handle");
        }
        if ((record->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0U) {
            signature.depth_format = record->format;
        }
        if ((record->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0U) {
            signature.stencil_format = record->format;
        }
        if (signature.samples == VK_SAMPLE_COUNT_1_BIT) {
            signature.samples = record->samples;
        }
    }

    if (stencil_attachment_ != nullptr) {
        const TargetRecord* record = Resolve(stencil_attachment_->target);
        if (record == nullptr) {
            throw std::out_of_range("RenderTargetHost::BuildPipelineSignature invalid stencil attachment handle");
        }
        if ((record->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0U) {
            signature.stencil_format = record->format;
        }
        if (signature.samples == VK_SAMPLE_COUNT_1_BIT) {
            signature.samples = record->samples;
        }
    }

    return signature;
}

RenderTargetRenderingInfo RenderTargetHost::BuildRenderingInfo(
    VkRect2D render_area_,
    std::uint32_t layer_count_,
    const AttachmentRef* color_attachments_,
    std::uint32_t color_attachment_count_,
    const AttachmentRef* depth_attachment_,
    const AttachmentRef* stencil_attachment_) const {
    if (color_attachment_count_ > k_max_render_target_color_attachments) {
        throw std::out_of_range("RenderTargetHost::BuildRenderingInfo color attachment count exceeds limit");
    }
    if (layer_count_ == 0U) {
        throw std::invalid_argument("RenderTargetHost::BuildRenderingInfo layer_count must be > 0");
    }

    RenderTargetRenderingInfo info{};
    info.color_attachment_count = color_attachment_count_;
    for (std::uint32_t index = 0U; index < color_attachment_count_; ++index) {
        info.color_attachments[index] = BuildOneRenderingAttachment(
            color_attachments_[index],
            RenderTargetStateKind::color_attachment);
    }

    if (depth_attachment_ != nullptr) {
        info.depth_attachment = BuildOneRenderingAttachment(
            *depth_attachment_,
            RenderTargetStateKind::depth_attachment);
        info.has_depth_attachment = true;
    }

    if (stencil_attachment_ != nullptr) {
        info.stencil_attachment = BuildOneRenderingAttachment(
            *stencil_attachment_,
            RenderTargetStateKind::depth_attachment);
        info.has_stencil_attachment = true;
    }

    info.vk.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.vk.renderArea = render_area_;
    info.vk.layerCount = layer_count_;
    info.vk.colorAttachmentCount = color_attachment_count_;
    info.vk.pColorAttachments = (color_attachment_count_ > 0U) ? info.color_attachments.data() : nullptr;
    info.vk.pDepthAttachment = info.has_depth_attachment ? &info.depth_attachment : nullptr;
    info.vk.pStencilAttachment = info.has_stencil_attachment ? &info.stencil_attachment : nullptr;
    return info;
}

void RenderTargetHost::RecordTransition(VkCommandBuffer command_buffer_,
                                        RenderTargetHandle handle_,
                                        RenderTargetStateKind new_state_) {
    if (command_buffer_ == VK_NULL_HANDLE) {
        throw std::invalid_argument("RenderTargetHost::RecordTransition requires valid command buffer");
    }

    TargetRecord* record = Resolve(handle_);
    if (record == nullptr) {
        throw std::out_of_range("RenderTargetHost::RecordTransition invalid render target handle");
    }
    if (record->state == new_state_) {
        return;
    }

    const RenderTargetStateInfo src_state = DescribeState(record->state, record->aspect);
    const RenderTargetStateInfo dst_state = DescribeState(new_state_, record->aspect);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_state.stage_mask;
    barrier.srcAccessMask = src_state.access_mask;
    barrier.dstStageMask = dst_state.stage_mask;
    barrier.dstAccessMask = dst_state.access_mask;
    barrier.oldLayout = src_state.layout;
    barrier.newLayout = dst_state.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = (record->ownership == RenderTargetOwnership::owned)
        ? record->owned_resource.image
        : record->imported_image;
    barrier.subresourceRange = DefaultSubresourceRange(handle_);

    VkDependencyInfo dependency_info{};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.imageMemoryBarrierCount = 1U;
    dependency_info.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command_buffer_, &dependency_info);

    record->state = new_state_;
}

void RenderTargetHost::RecordTransitionsForRendering(VkCommandBuffer command_buffer_,
                                                     const AttachmentRef* color_attachments_,
                                                     std::uint32_t color_attachment_count_,
                                                     const AttachmentRef* depth_attachment_,
                                                     const AttachmentRef* stencil_attachment_) {
    if (command_buffer_ == VK_NULL_HANDLE) {
        throw std::invalid_argument(
            "RenderTargetHost::RecordTransitionsForRendering requires valid command buffer");
    }
    if (color_attachment_count_ > k_max_render_target_color_attachments) {
        throw std::out_of_range(
            "RenderTargetHost::RecordTransitionsForRendering color attachment count exceeds limit");
    }

    struct PendingTransition final {
        RenderTargetHandle handle{};
        RenderTargetStateKind state = RenderTargetStateKind::undefined;
    };

    std::array<PendingTransition, k_max_render_target_color_attachments + 4U> pending{};
    std::uint32_t pending_count = 0U;

    auto append_transition = [&](RenderTargetHandle handle_,
                                 RenderTargetStateKind state_) {
        if (!IsValidRenderTargetHandle(handle_)) {
            return;
        }
        for (std::uint32_t index = 0U; index < pending_count; ++index) {
            if (pending[index].handle.index == handle_.index &&
                pending[index].handle.generation == handle_.generation) {
                if (pending[index].state != state_) {
                    throw std::invalid_argument(
                        "RenderTargetHost::RecordTransitionsForRendering conflicting target states in one batch");
                }
                return;
            }
        }
        pending[pending_count++] = PendingTransition{
            .handle = handle_,
            .state = state_,
        };
    };

    for (std::uint32_t index = 0U; index < color_attachment_count_; ++index) {
        append_transition(color_attachments_[index].target,
                          color_attachments_[index].expected_state);
        if (color_attachments_[index].use_resolve) {
            append_transition(color_attachments_[index].resolve_target,
                              RenderTargetStateKind::color_attachment);
        }
    }
    if (depth_attachment_ != nullptr) {
        append_transition(depth_attachment_->target,
                          depth_attachment_->expected_state);
    }
    if (stencil_attachment_ != nullptr) {
        append_transition(stencil_attachment_->target,
                          stencil_attachment_->expected_state);
    }

    std::array<VkImageMemoryBarrier2, k_max_render_target_color_attachments + 4U> barriers{};
    std::uint32_t barrier_count = 0U;
    for (std::uint32_t index = 0U; index < pending_count; ++index) {
        TargetRecord* record = Resolve(pending[index].handle);
        if (record == nullptr || record->state == pending[index].state) {
            continue;
        }

        const RenderTargetStateInfo src_state = DescribeState(record->state, record->aspect);
        const RenderTargetStateInfo dst_state = DescribeState(pending[index].state, record->aspect);
        VkImageMemoryBarrier2& barrier = barriers[barrier_count++];
        barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = src_state.stage_mask;
        barrier.srcAccessMask = src_state.access_mask;
        barrier.dstStageMask = dst_state.stage_mask;
        barrier.dstAccessMask = dst_state.access_mask;
        barrier.oldLayout = src_state.layout;
        barrier.newLayout = dst_state.layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = (record->ownership == RenderTargetOwnership::owned)
            ? record->owned_resource.image
            : record->imported_image;
        barrier.subresourceRange = DefaultSubresourceRange(pending[index].handle);
        record->state = pending[index].state;
    }

    if (barrier_count == 0U) {
        return;
    }

    VkDependencyInfo dependency_info{};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.imageMemoryBarrierCount = barrier_count;
    dependency_info.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(command_buffer_, &dependency_info);
}

bool RenderTargetHost::IsValid(RenderTargetHandle handle_) const noexcept {
    return Resolve(handle_) != nullptr;
}

bool RenderTargetHost::IsInitialized() const noexcept {
    return initialized;
}

const RenderTargetHostStats& RenderTargetHost::Stats() const noexcept {
    return stats;
}

RenderTargetStateInfo RenderTargetHost::DescribeState(RenderTargetStateKind state_kind_,
                                                      VkImageAspectFlags aspect_mask_) noexcept {
    const bool is_depth = IsDepthAspect(aspect_mask_);
    switch (state_kind_) {
        case RenderTargetStateKind::undefined:
            return RenderTargetStateInfo{
                .layout = VK_IMAGE_LAYOUT_UNDEFINED,
                .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                .access_mask = VK_ACCESS_2_NONE,
            };
        case RenderTargetStateKind::color_attachment:
            return RenderTargetStateInfo{
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            };
        case RenderTargetStateKind::depth_attachment:
            return RenderTargetStateInfo{
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            };
        case RenderTargetStateKind::depth_read_only:
            return RenderTargetStateInfo{
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            };
        case RenderTargetStateKind::shader_read:
            return RenderTargetStateInfo{
                .layout = is_depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .stage_mask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            };
        case RenderTargetStateKind::shader_write:
            return RenderTargetStateInfo{
                .layout = VK_IMAGE_LAYOUT_GENERAL,
                .stage_mask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                               VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            };
        case RenderTargetStateKind::transfer_src:
            return RenderTargetStateInfo{
                .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
            };
        case RenderTargetStateKind::transfer_dst:
            return RenderTargetStateInfo{
                .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            };
        case RenderTargetStateKind::present_src:
            return RenderTargetStateInfo{
                .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .stage_mask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                .access_mask = VK_ACCESS_2_NONE,
            };
        default:
            return RenderTargetStateInfo{};
    }
}

std::uint32_t RenderTargetHost::AllocateSlot() noexcept {
    if (!free_indices.empty()) {
        const std::uint32_t index = free_indices.back();
        free_indices.pop_back();
        return index;
    }

    const std::uint32_t new_index = static_cast<std::uint32_t>(targets.size());
    targets.push_back({});
    generations.push_back(1U);
    return new_index;
}

void RenderTargetHost::RetireRecord(TargetRecord& record_,
                                    std::uint64_t retire_value_) {
    if (bindless_config.Enabled() && record_.bindless_image_slot.IsValid()) {
        bindless_config.descriptor_host->QueueBindlessPlaceholderWrite(bindless_config.image_table,
                                                                       record_.bindless_image_slot);
        bindless_config.descriptor_host->FreeBindlessSlotDeferred(bindless_config.image_table,
                                                                  record_.bindless_image_slot,
                                                                  retire_value_);
        record_.bindless_image_slot = {};
        record_.bindless_resource_revision_written = 0U;
    }

    RetiredTargetPayload payload{};
    if (record_.ownership == RenderTargetOwnership::owned &&
        record_.owned_resource.image != VK_NULL_HANDLE) {
        payload.owned_resource = std::move(record_.owned_resource);
    } else if (record_.ownership == RenderTargetOwnership::imported_image_owned_view &&
               record_.imported_view != VK_NULL_HANDLE) {
        payload.owned_view = record_.imported_view;
    }

    if (payload.owned_resource.image != VK_NULL_HANDLE ||
        payload.owned_view != VK_NULL_HANDLE) {
        retired_targets.Retire(std::move(payload), retire_value_);
    }
}

void RenderTargetHost::InvalidateBindlessRecords(const RenderTargetHostBindlessConfig& bindless_config_) {
    const std::uint64_t retire_value = ComputeBindlessRetireValue();
    for (TargetRecord& record : targets) {
        if (!record.active) {
            continue;
        }
        if (bindless_config_.Enabled() && record.bindless_image_slot.IsValid()) {
            bindless_config_.descriptor_host->QueueBindlessPlaceholderWrite(bindless_config_.image_table,
                                                                            record.bindless_image_slot);
            bindless_config_.descriptor_host->FreeBindlessSlotDeferred(bindless_config_.image_table,
                                                                       record.bindless_image_slot,
                                                                       retire_value);
        }
        record.bindless_image_slot = {};
        record.bindless_resource_revision_written = 0U;
    }
}

void RenderTargetHost::CollectRetiredTargets(VulkanContext& context_,
                                             std::uint64_t completed_submit_value_) {
    if (retired_targets.Empty()) {
        return;
    }
    (void)retired_targets.Collect(completed_submit_value_,
                                  [&](RetiredTargetPayload& payload_) {
                                      DestroyRetiredPayload(context_, payload_);
                                  });
}

void RenderTargetHost::DestroyRetiredTargets(VulkanContext& context_) noexcept {
    (void)retired_targets.Flush([&](RetiredTargetPayload& payload_) {
        DestroyRetiredPayload(context_, payload_);
    });
}

void RenderTargetHost::DestroyRetiredPayload(VulkanContext& context_,
                                             RetiredTargetPayload& payload_) noexcept {
    if (payload_.owned_view != VK_NULL_HANDLE) {
        resource::ImageHost::DestroyView(context_, payload_.owned_view);
    }
    if (payload_.owned_resource.image != VK_NULL_HANDLE) {
        resource::ImageHost::DestroyImage(context_, payload_.owned_resource);
    }
}

void RenderTargetHost::ResetRecord(TargetRecord& record_) noexcept {
    record_ = {};
}

std::uint64_t RenderTargetHost::ComputeBindlessRetireValue() const noexcept {
    return std::max(last_submitted_value_seen, completed_submit_value_seen);
}

void RenderTargetHost::RefreshStats() noexcept {
    stats.target_count = 0U;
    stats.owned_target_count = 0U;
    stats.imported_target_count = 0U;
    for (const auto& target : targets) {
        if (!target.active) {
            continue;
        }
        ++stats.target_count;
        if (target.ownership == RenderTargetOwnership::owned) {
            ++stats.owned_target_count;
        } else {
            ++stats.imported_target_count;
        }
    }
    stats.retired_target_count = retired_targets.PendingCount();
}

bool RenderTargetHost::SupportsBindlessSampling(const TargetRecord& record_) const noexcept {
    return (record_.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0U &&
           (record_.aspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0U;
}

VkImageView RenderTargetHost::ResolveDefaultImageView(const TargetRecord& record_) const noexcept {
    return (record_.ownership == RenderTargetOwnership::owned)
        ? record_.owned_resource.default_view
        : record_.imported_view;
}

VkRenderingAttachmentInfo RenderTargetHost::BuildOneRenderingAttachment(
    const AttachmentRef& attachment_,
    RenderTargetStateKind fallback_state_) const {
    const TargetRecord* record = Resolve(attachment_.target);
    if (record == nullptr) {
        throw std::out_of_range("RenderTargetHost::BuildOneRenderingAttachment invalid target handle");
    }

    const RenderTargetStateKind expected_state =
        (attachment_.expected_state == RenderTargetStateKind::undefined)
            ? fallback_state_
            : attachment_.expected_state;
    const RenderTargetStateInfo state_info = DescribeState(expected_state, record->aspect);

    VkRenderingAttachmentInfo attachment_info{};
    attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment_info.imageView = GetDefaultView(attachment_.target);
    attachment_info.imageLayout = state_info.layout;
    attachment_info.loadOp = attachment_.load_op;
    attachment_info.storeOp = attachment_.store_op;
    attachment_info.clearValue = attachment_.clear_value;

    if (attachment_.use_resolve) {
        const TargetRecord* resolve_record = Resolve(attachment_.resolve_target);
        if (resolve_record == nullptr) {
            throw std::out_of_range(
                "RenderTargetHost::BuildOneRenderingAttachment invalid resolve target handle");
        }
        const RenderTargetStateInfo resolve_state =
            DescribeState(RenderTargetStateKind::color_attachment, resolve_record->aspect);
        attachment_info.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        attachment_info.resolveImageView = GetDefaultView(attachment_.resolve_target);
        attachment_info.resolveImageLayout = resolve_state.layout;
    }

    return attachment_info;
}

VkExtent3D RenderTargetHost::ResolveExtent(const RenderTargetDesc& desc_,
                                           VkExtent2D reference_extent_) {
    VkExtent3D extent{desc_.width, desc_.height, desc_.depth};
    if (desc_.scale_mode == RenderTargetScaleMode::swapchain_relative) {
        if (reference_extent_.width == 0U || reference_extent_.height == 0U) {
            throw std::invalid_argument(
                "RenderTargetHost relative extent requires non-zero reference swapchain extent");
        }
        const float scaled_width = static_cast<float>(reference_extent_.width) * desc_.width_scale;
        const float scaled_height = static_cast<float>(reference_extent_.height) * desc_.height_scale;
        extent.width = static_cast<std::uint32_t>(scaled_width < 1.0F ? 1.0F : scaled_width);
        extent.height = static_cast<std::uint32_t>(scaled_height < 1.0F ? 1.0F : scaled_height);
        extent.depth = desc_.depth;
    }
    return extent;
}

VkImageType RenderTargetHost::ToVkImageType(RenderTargetDimension dimension_) noexcept {
    return dimension_ == RenderTargetDimension::image_3d
        ? VK_IMAGE_TYPE_3D
        : VK_IMAGE_TYPE_2D;
}

VkImageViewType RenderTargetHost::DefaultViewType(RenderTargetDimension dimension_,
                                                  std::uint32_t array_layers_) noexcept {
    switch (dimension_) {
        case RenderTargetDimension::image_2d:
            return array_layers_ > 1U ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        case RenderTargetDimension::image_2d_array:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case RenderTargetDimension::image_3d:
            return VK_IMAGE_VIEW_TYPE_3D;
        case RenderTargetDimension::cube:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case RenderTargetDimension::cube_array:
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        default:
            return VK_IMAGE_VIEW_TYPE_2D;
    }
}

VkImageCreateFlags RenderTargetHost::ResolveImageCreateFlags(const RenderTargetDesc& desc_) noexcept {
    VkImageCreateFlags flags = desc_.flags;
    if (desc_.dimension == RenderTargetDimension::cube ||
        desc_.dimension == RenderTargetDimension::cube_array) {
        flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    return flags;
}

resource::ImageCreateInfo RenderTargetHost::BuildOwnedImageCreateInfo(
    VulkanContext& context_,
    const RenderTargetHostCreateInfo& host_create_info_,
    const RenderTargetDesc& desc_,
    VkExtent2D reference_extent_) {
    resource::ImageCreateInfo create_info{};
    create_info.flags = ResolveImageCreateFlags(desc_);
    create_info.image_type = ToVkImageType(desc_.dimension);
    create_info.format = desc_.format;
    create_info.extent = ResolveExtent(desc_, reference_extent_);
    create_info.mip_levels = desc_.mip_levels;
    create_info.array_layers = desc_.array_layers;
    create_info.samples = desc_.samples;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = desc_.usage;
    create_info.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;

    const auto& families = context_.QueueFamilies();
    if (families.graphics.has_value() &&
        families.transfer.has_value() &&
        families.graphics.value() != families.transfer.value()) {
        create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
        create_info.queue_family_indices.push_back(families.graphics.value());
        create_info.queue_family_indices.push_back(families.transfer.value());
    }

    create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    create_info.memory_properties = host_create_info_.memory_properties;
    create_info.create_default_view = true;
    create_info.default_view_type = DefaultViewType(desc_.dimension, desc_.array_layers);
    create_info.default_view_aspect = desc_.aspect;
    create_info.default_base_mip_level = 0U;
    create_info.default_level_count = desc_.mip_levels;
    create_info.default_base_array_layer = 0U;
    create_info.default_layer_count = desc_.array_layers;
    return create_info;
}

void RenderTargetHost::ValidateOwnedDesc(const RenderTargetDesc& desc_) {
    if (desc_.lifetime == RenderTargetLifetime::imported) {
        throw std::invalid_argument(
            "RenderTargetHost owned desc cannot use imported lifetime");
    }
    if (desc_.format == VK_FORMAT_UNDEFINED) {
        throw std::invalid_argument("RenderTargetHost owned desc requires valid format");
    }
    if (desc_.usage == 0U) {
        throw std::invalid_argument("RenderTargetHost owned desc requires non-zero usage");
    }
    if (desc_.aspect == 0U) {
        throw std::invalid_argument("RenderTargetHost owned desc requires non-zero aspect mask");
    }
    if (desc_.width == 0U || desc_.height == 0U || desc_.depth == 0U) {
        throw std::invalid_argument("RenderTargetHost owned desc extent dimensions must be > 0");
    }
    if (desc_.mip_levels == 0U || desc_.array_layers == 0U) {
        throw std::invalid_argument("RenderTargetHost owned desc mip_levels/array_layers must be > 0");
    }
}

void RenderTargetHost::ValidateImportedDesc(const ImportedRenderTargetDesc& desc_) {
    if (desc_.image == VK_NULL_HANDLE) {
        throw std::invalid_argument("RenderTargetHost imported desc requires valid image");
    }
    if (desc_.format == VK_FORMAT_UNDEFINED) {
        throw std::invalid_argument("RenderTargetHost imported desc requires valid format");
    }
    if (desc_.usage == 0U) {
        throw std::invalid_argument("RenderTargetHost imported desc requires non-zero usage");
    }
    if (desc_.aspect == 0U) {
        throw std::invalid_argument("RenderTargetHost imported desc requires non-zero aspect mask");
    }
    if (desc_.extent.width == 0U || desc_.extent.height == 0U || desc_.extent.depth == 0U) {
        throw std::invalid_argument("RenderTargetHost imported desc extent dimensions must be > 0");
    }
    if (desc_.ownership == RenderTargetOwnership::imported_image_imported_view &&
        desc_.image_view == VK_NULL_HANDLE) {
        throw std::invalid_argument(
            "RenderTargetHost imported external-view desc requires non-null image_view");
    }
}

} // namespace vr::render

