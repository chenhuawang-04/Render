#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/render_target_desc.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>

namespace vr::render {

template<typename T>
using RenderTargetPoolMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct RenderTargetPoolCreateInfo final {
    std::uint32_t reserve_bucket_count = 32U;
    std::uint32_t reserve_live_target_count = 64U;
    std::uint32_t reserve_bucket_free_indices = 64U;
    std::uint32_t max_cached_targets_per_bucket = 8U;
    std::uint32_t max_idle_frames_before_collect = 32U;
};

struct AcquireTransientRenderTargetResult final {
    RenderTargetHandle handle{};
    VkExtent3D extent{};
    bool reused = false;
    bool created = false;
};

struct RenderTargetPoolStats final {
    std::uint32_t live_transient_target_count = 0U;
    std::uint32_t pending_reuse_target_count = 0U;
    std::uint32_t reusable_target_count = 0U;
    std::uint32_t bucket_count = 0U;
    std::uint32_t frame_revision = 0U;
    std::uint64_t acquire_count = 0U;
    std::uint64_t reuse_hit_count = 0U;
    std::uint64_t create_count = 0U;
    std::uint64_t destroy_count = 0U;
    std::uint64_t gc_destroy_count = 0U;
};

class RenderTargetPool final {
public:
    RenderTargetPool() = default;
    ~RenderTargetPool() = default;

    RenderTargetPool(const RenderTargetPool&) = delete;
    RenderTargetPool& operator=(const RenderTargetPool&) = delete;

    RenderTargetPool(RenderTargetPool&&) = delete;
    RenderTargetPool& operator=(RenderTargetPool&&) = delete;

    void Initialize(const RenderTargetPoolCreateInfo& create_info_ = {});
    void Shutdown() noexcept;
    void BeginFrame(std::uint32_t frame_index_,
                    std::uint64_t completed_submit_value_) noexcept;
    void EndFrame(std::uint32_t frame_index_,
                  std::uint64_t last_submitted_value_) noexcept;

    [[nodiscard]] AcquireTransientRenderTargetResult AcquireTransientTarget(
        VulkanContext& context_,
        RenderTargetHost& render_target_host_,
        const RenderTargetDesc& desc_,
        VkExtent2D reference_extent_ = {});

    void InvalidateAll(VulkanContext& context_,
                       RenderTargetHost& render_target_host_,
                       std::uint64_t last_submitted_value_,
                       std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const RenderTargetPoolStats& Stats() const noexcept;

private:
    struct TransientTargetKey final {
        RenderTargetDimension dimension = RenderTargetDimension::image_2d;
        VkExtent3D extent{};
        VkImageCreateFlags flags = 0U;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkImageUsageFlags usage = 0U;
        VkImageAspectFlags aspect = 0U;
        std::uint32_t mip_levels = 1U;
        std::uint32_t array_layers = 1U;
        RenderTargetColorEncoding color_encoding = RenderTargetColorEncoding::linear;
        RenderTargetMemoryPolicy memory_policy = RenderTargetMemoryPolicy::auto_select;
        bool allow_uav = false;
        bool allow_alias = false;
        bool allow_history = false;
    };

    struct BucketRecord final {
        TransientTargetKey key{};
        RenderTargetPoolMcVector<std::uint32_t> reusable_target_indices{};
    };

    struct BucketLookupNode final {
        std::uint64_t hash = 0U;
        std::uint32_t bucket_index = 0U;
    };

    struct TargetRecord final {
        RenderTargetHandle handle{};
        TransientTargetKey key{};
        std::uint32_t bucket_index = 0U;
        std::uint64_t reusable_after_submit_value = 0U;
        std::uint32_t last_released_frame_revision = 0U;
        bool active = false;
        bool in_use = false;
        bool in_reusable_bucket = false;
    };

    [[nodiscard]] static TransientTargetKey BuildKey(const RenderTargetDesc& desc_,
                                                     VkExtent2D reference_extent_);
    [[nodiscard]] static bool KeysEqual(const TransientTargetKey& lhs_,
                                        const TransientTargetKey& rhs_) noexcept;
    [[nodiscard]] static std::uint64_t HashKey(const TransientTargetKey& key_) noexcept;
    [[nodiscard]] std::size_t LowerBoundBucketLookupIndex(std::uint64_t hash_) const noexcept;
    [[nodiscard]] std::uint32_t FindOrCreateBucket(const TransientTargetKey& key_);
    [[nodiscard]] std::uint32_t AllocateTargetSlot();
    [[nodiscard]] std::uint32_t FindReusableTargetIndex(std::uint32_t bucket_index_) noexcept;
    void CollectGarbage(VulkanContext& context_,
                        RenderTargetHost& render_target_host_,
                        std::uint64_t completed_submit_value_);
    void PromoteCompletedTargetsToReusable(std::uint64_t completed_submit_value_) noexcept;
    void RefreshStats() noexcept;

private:
    RenderTargetPoolCreateInfo create_info_cache{};
    RenderTargetPoolMcVector<BucketRecord> buckets{};
    RenderTargetPoolMcVector<BucketLookupNode> bucket_lookup{};
    RenderTargetPoolMcVector<TargetRecord> targets{};
    RenderTargetPoolMcVector<std::uint32_t> free_target_indices{};
    std::uint32_t active_frame_index = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    RenderTargetPoolStats stats{};
    bool frame_open = false;
    bool initialized = false;
};

} // namespace vr::render

