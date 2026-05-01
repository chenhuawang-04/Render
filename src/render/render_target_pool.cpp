#include "vr/render/render_target_pool.hpp"

#include <stdexcept>

namespace vr::render {

namespace {

[[nodiscard]] VkExtent3D ResolveExtentForPool(const RenderTargetDesc& desc_,
                                              VkExtent2D reference_extent_) {
    VkExtent3D extent{desc_.width, desc_.height, desc_.depth};
    if (desc_.scale_mode == RenderTargetScaleMode::swapchain_relative) {
        if (reference_extent_.width == 0U || reference_extent_.height == 0U) {
            throw std::invalid_argument(
                "RenderTargetPool transient relative extent requires non-zero reference extent");
        }
        const float scaled_width = static_cast<float>(reference_extent_.width) * desc_.width_scale;
        const float scaled_height = static_cast<float>(reference_extent_.height) * desc_.height_scale;
        extent.width = static_cast<std::uint32_t>(scaled_width < 1.0F ? 1.0F : scaled_width);
        extent.height = static_cast<std::uint32_t>(scaled_height < 1.0F ? 1.0F : scaled_height);
    }
    return extent;
}

} // namespace

void RenderTargetPool::Initialize(const RenderTargetPoolCreateInfo& create_info_) {
    create_info_cache = create_info_;
    buckets.clear();
    targets.clear();
    if (create_info_cache.reserve_bucket_count > 0U) {
        buckets.reserve(create_info_cache.reserve_bucket_count);
    }
    if (create_info_cache.reserve_live_target_count > 0U) {
        targets.reserve(create_info_cache.reserve_live_target_count);
    }
    active_frame_index = 0U;
    completed_submit_value_seen = 0U;
    stats = {};
    frame_open = false;
    initialized = true;
}

void RenderTargetPool::Shutdown() noexcept {
    for (auto& bucket : buckets) {
        bucket.reusable_target_indices.clear();
    }
    buckets.clear();
    targets.clear();
    create_info_cache = {};
    active_frame_index = 0U;
    completed_submit_value_seen = 0U;
    stats = {};
    frame_open = false;
    initialized = false;
}

void RenderTargetPool::BeginFrame(std::uint32_t frame_index_,
                                  std::uint64_t completed_submit_value_) noexcept {
    if (!initialized) {
        return;
    }

    active_frame_index = frame_index_;
    completed_submit_value_seen = completed_submit_value_;
    frame_open = true;
    ++stats.frame_revision;
    PromoteCompletedTargetsToReusable(completed_submit_value_seen);
    RefreshStats();
}

void RenderTargetPool::EndFrame(std::uint32_t frame_index_,
                                std::uint64_t last_submitted_value_) noexcept {
    if (!initialized || !frame_open || frame_index_ != active_frame_index) {
        return;
    }

    for (auto& target : targets) {
        if (!target.active || !target.in_use) {
            continue;
        }
        target.in_use = false;
        target.in_reusable_bucket = false;
        target.reusable_after_submit_value = last_submitted_value_;
    }

    frame_open = false;
    RefreshStats();
}

AcquireTransientRenderTargetResult RenderTargetPool::AcquireTransientTarget(
    VulkanContext& context_,
    RenderTargetHost& render_target_host_,
    const RenderTargetDesc& desc_,
    VkExtent2D reference_extent_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetPool::AcquireTransientTarget called before Initialize");
    }
    if (!frame_open) {
        throw std::runtime_error("RenderTargetPool::AcquireTransientTarget requires active frame scope");
    }
    if (desc_.lifetime == RenderTargetLifetime::imported) {
        throw std::invalid_argument("RenderTargetPool transient target desc cannot use imported lifetime");
    }

    RenderTargetDesc transient_desc = desc_;
    transient_desc.lifetime = RenderTargetLifetime::transient;

    const TransientTargetKey key = BuildKey(transient_desc, reference_extent_);
    const std::uint32_t bucket_index = FindOrCreateBucket(key);
    const std::uint32_t target_index = FindReusableTargetIndex(bucket_index);

    AcquireTransientRenderTargetResult result{};
    result.extent = key.extent;
    ++stats.acquire_count;

    if (target_index != invalid_render_target_index) {
        TargetRecord& target = targets[target_index];
        target.in_use = true;
        target.in_reusable_bucket = false;
        target.reusable_after_submit_value = 0U;
        result.handle = target.handle;
        result.reused = true;
        ++stats.reuse_hit_count;
        RefreshStats();
        return result;
    }

    const RenderTargetHandle handle =
        render_target_host_.CreateTransientTarget(context_, transient_desc, reference_extent_);
    TargetRecord record{};
    record.handle = handle;
    record.key = key;
    record.bucket_index = bucket_index;
    record.reusable_after_submit_value = 0U;
    record.active = true;
    record.in_use = true;
    record.in_reusable_bucket = false;
    targets.push_back(record);

    result.handle = handle;
    result.created = true;
    ++stats.create_count;
    RefreshStats();
    return result;
}

void RenderTargetPool::InvalidateAll(VulkanContext& context_,
                                     RenderTargetHost& render_target_host_,
                                     std::uint64_t last_submitted_value_,
                                     std::uint64_t completed_submit_value_) {
    if (!initialized) {
        return;
    }

    const bool was_frame_open = frame_open;

    for (auto& target : targets) {
        if (!target.active || !IsValidRenderTargetHandle(target.handle)) {
            continue;
        }
        if (render_target_host_.DestroyTarget(context_,
                                              target.handle,
                                              last_submitted_value_,
                                              completed_submit_value_)) {
            ++stats.destroy_count;
        }
        target = {};
    }

    for (auto& bucket : buckets) {
        bucket.reusable_target_indices.clear();
    }
    buckets.clear();
    targets.clear();
    frame_open = was_frame_open;
    completed_submit_value_seen = completed_submit_value_;
    RefreshStats();
}

bool RenderTargetPool::IsInitialized() const noexcept {
    return initialized;
}

const RenderTargetPoolStats& RenderTargetPool::Stats() const noexcept {
    return stats;
}

RenderTargetPool::TransientTargetKey RenderTargetPool::BuildKey(const RenderTargetDesc& desc_,
                                                                VkExtent2D reference_extent_) {
    TransientTargetKey key{};
    key.dimension = desc_.dimension;
    key.extent = ResolveExtentForPool(desc_, reference_extent_);
    key.flags = desc_.flags;
    key.format = desc_.format;
    key.samples = desc_.samples;
    key.usage = desc_.usage;
    key.aspect = desc_.aspect;
    key.mip_levels = desc_.mip_levels;
    key.array_layers = desc_.array_layers;
    key.color_encoding = desc_.color_encoding;
    key.memory_policy = desc_.memory_policy;
    key.allow_uav = desc_.allow_uav;
    key.allow_alias = desc_.allow_alias;
    key.allow_history = desc_.allow_history;
    return key;
}

bool RenderTargetPool::KeysEqual(const TransientTargetKey& lhs_,
                                 const TransientTargetKey& rhs_) noexcept {
    return lhs_.dimension == rhs_.dimension &&
           lhs_.extent.width == rhs_.extent.width &&
           lhs_.extent.height == rhs_.extent.height &&
           lhs_.extent.depth == rhs_.extent.depth &&
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

std::uint32_t RenderTargetPool::FindOrCreateBucket(const TransientTargetKey& key_) {
    for (std::uint32_t index = 0U; index < buckets.size(); ++index) {
        if (KeysEqual(buckets[index].key, key_)) {
            return index;
        }
    }

    BucketRecord bucket{};
    bucket.key = key_;
    if (create_info_cache.reserve_bucket_free_indices > 0U) {
        bucket.reusable_target_indices.reserve(create_info_cache.reserve_bucket_free_indices);
    }
    buckets.push_back(bucket);
    return static_cast<std::uint32_t>(buckets.size() - 1U);
}

std::uint32_t RenderTargetPool::FindReusableTargetIndex(std::uint32_t bucket_index_) noexcept {
    if (bucket_index_ >= buckets.size()) {
        return invalid_render_target_index;
    }

    BucketRecord& bucket = buckets[bucket_index_];
    while (!bucket.reusable_target_indices.empty()) {
        const std::uint32_t target_index = bucket.reusable_target_indices.back();
        bucket.reusable_target_indices.pop_back();
        if (target_index >= targets.size()) {
            continue;
        }

        TargetRecord& target = targets[target_index];
        if (!target.active || target.in_use || target.bucket_index != bucket_index_) {
            continue;
        }

        target.in_reusable_bucket = false;
        return target_index;
    }

    return invalid_render_target_index;
}

void RenderTargetPool::PromoteCompletedTargetsToReusable(std::uint64_t completed_submit_value_) noexcept {
    for (std::uint32_t index = 0U; index < targets.size(); ++index) {
        TargetRecord& target = targets[index];
        if (!target.active || target.in_use || target.in_reusable_bucket) {
            continue;
        }
        if (target.reusable_after_submit_value == 0U ||
            completed_submit_value_ < target.reusable_after_submit_value) {
            continue;
        }
        if (target.bucket_index >= buckets.size()) {
            continue;
        }

        buckets[target.bucket_index].reusable_target_indices.push_back(index);
        target.in_reusable_bucket = true;
    }
}

void RenderTargetPool::RefreshStats() noexcept {
    std::uint32_t live_count = 0U;
    std::uint32_t pending_count = 0U;
    std::uint32_t reusable_count = 0U;

    for (const auto& target : targets) {
        if (!target.active) {
            continue;
        }
        ++live_count;
        if (target.in_reusable_bucket) {
            ++reusable_count;
        } else if (!target.in_use) {
            ++pending_count;
        }
    }

    stats.live_transient_target_count = live_count;
    stats.pending_reuse_target_count = pending_count;
    stats.reusable_target_count = reusable_count;
    stats.bucket_count = static_cast<std::uint32_t>(buckets.size());
}

} // namespace vr::render
