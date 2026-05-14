#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/bindless_types.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>

namespace vr::render {

#ifndef VR_ENABLE_DESCRIPTOR_VALIDATION
#define VR_ENABLE_DESCRIPTOR_VALIDATION 0
#endif

template<typename T>
using DescriptorMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct DescriptorPoolSizeRatio {
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    float ratio = 1.0F;
};

struct DescriptorHostCreateInfo {
    uint32_t frames_in_flight = 2U;
    uint32_t max_sets_per_pool = 512U;
    VkDescriptorPoolCreateFlags pool_flags = 0U;
    DescriptorMcVector<DescriptorPoolSizeRatio> pool_size_ratios{};
    bool preallocate_first_pool_per_frame = true;
    uint32_t reserve_layout_count = 128U;
    bool enable_validation = true;
};

struct DescriptorSetLayoutDesc {
    DescriptorMcVector<VkDescriptorSetLayoutBinding> bindings{};
    DescriptorMcVector<VkDescriptorBindingFlags> binding_flags{};
    VkDescriptorSetLayoutCreateFlags flags = 0U;
};

struct DescriptorBufferWrite {
    uint32_t binding = 0U;
    uint32_t array_element = 0U;
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0U;
    VkDeviceSize range = VK_WHOLE_SIZE;
};

struct DescriptorImageWrite {
    uint32_t binding = 0U;
    uint32_t array_element = 0U;
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

struct DescriptorTexelBufferWrite {
    uint32_t binding = 0U;
    uint32_t array_element = 0U;
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    VkBufferView buffer_view = VK_NULL_HANDLE;
};

struct DescriptorSetLayoutId {
    uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

struct DescriptorValidationStats {
    std::uint64_t tracked_set_count = 0U;
    std::uint64_t check_count = 0U;
    std::uint64_t failure_count = 0U;
    std::uint64_t begin_frame_invalidation_count = 0U;
};

struct DescriptorHostStats {
    std::uint64_t transient_update_call_count = 0U;
    std::uint64_t transient_descriptor_write_count = 0U;
    std::uint64_t bindless_table_count = 0U;
    std::uint64_t bindless_pending_write_count = 0U;
    std::uint64_t bindless_descriptor_write_count = 0U;
    std::uint64_t bindless_live_slot_count = 0U;
    std::uint64_t bindless_slot_recycle_count = 0U;
};

class DescriptorHost final {
public:
    DescriptorHost() = default;
    ~DescriptorHost() = default;

    DescriptorHost(const DescriptorHost&) = delete;
    DescriptorHost& operator=(const DescriptorHost&) = delete;

    DescriptorHost(DescriptorHost&&) = delete;
    DescriptorHost& operator=(DescriptorHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const DescriptorHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_, uint32_t frame_index_);

    [[nodiscard]] DescriptorSetLayoutId RegisterLayout(VulkanContext& context_,
                                                       const DescriptorSetLayoutDesc& layout_desc_);

    [[nodiscard]] VkDescriptorSetLayout AcquireLayout(VulkanContext& context_,
                                                      const DescriptorSetLayoutDesc& layout_desc_);

    [[nodiscard]] VkDescriptorSetLayout GetLayout(DescriptorSetLayoutId layout_id_) const;

    [[nodiscard]] VkDescriptorSet AllocateSet(VulkanContext& context_,
                                              uint32_t frame_index_,
                                              VkDescriptorSetLayout layout_);

    [[nodiscard]] VkDescriptorSet AllocateSet(VulkanContext& context_,
                                              uint32_t frame_index_,
                                              DescriptorSetLayoutId layout_id_);

    void AllocateSets(VulkanContext& context_,
                      uint32_t frame_index_,
                      VkDescriptorSetLayout layout_,
                      uint32_t set_count_,
                      VkDescriptorSet* out_sets_);

    void AllocateSets(VulkanContext& context_,
                      uint32_t frame_index_,
                      DescriptorSetLayoutId layout_id_,
                      uint32_t set_count_,
                      VkDescriptorSet* out_sets_);

    void UpdateSet(VkDevice device_,
                   VkDescriptorSet set_,
                   const DescriptorMcVector<DescriptorBufferWrite>& buffer_writes_,
                   const DescriptorMcVector<DescriptorImageWrite>& image_writes_,
                   const DescriptorMcVector<DescriptorTexelBufferWrite>& texel_writes_ = {});

    void UpdateSet(VulkanContext& context_,
                   VkDescriptorSet set_,
                   const DescriptorMcVector<DescriptorBufferWrite>& buffer_writes_,
                   const DescriptorMcVector<DescriptorImageWrite>& image_writes_,
                   const DescriptorMcVector<DescriptorTexelBufferWrite>& texel_writes_ = {});

    [[nodiscard]] BindlessTableId CreateBindlessTable(VulkanContext& context_,
                                                      const BindlessTableDesc& table_desc_);
    [[nodiscard]] VkDescriptorSet GetBindlessDescriptorSet(BindlessTableId table_id_) const;
    [[nodiscard]] VkDescriptorSetLayout GetBindlessLayout(BindlessTableId table_id_) const;
    [[nodiscard]] uint32_t GetBindlessCapacity(BindlessTableId table_id_) const;
    [[nodiscard]] BindlessTableStats GetBindlessTableStats(BindlessTableId table_id_) const;

    [[nodiscard]] BindlessSlot AllocateBindlessSlot(BindlessTableId table_id_);
    void FreeBindlessSlotDeferred(BindlessTableId table_id_,
                                  BindlessSlot slot_,
                                  std::uint64_t retire_value_);

    void QueueBindlessImageWrite(BindlessTableId table_id_,
                                 BindlessSlot slot_,
                                 VkImageView image_view_,
                                 VkImageLayout image_layout_);
    void QueueBindlessSamplerWrite(BindlessTableId table_id_,
                                   BindlessSlot slot_,
                                   VkSampler sampler_);
    void QueueBindlessCombinedImageSamplerWrite(BindlessTableId table_id_,
                                                BindlessSlot slot_,
                                                VkSampler sampler_,
                                                VkImageView image_view_,
                                                VkImageLayout image_layout_);
    void QueueBindlessPlaceholderWrite(BindlessTableId table_id_,
                                       BindlessSlot slot_);
    void FlushBindlessWrites(VulkanContext& context_,
                             std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsBindlessSlotAlive(BindlessTableId table_id_,
                                           BindlessSlot slot_) const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] uint32_t MaxSetsPerPool() const noexcept;
    [[nodiscard]] uint32_t CachedLayoutCount() const noexcept;
    [[nodiscard]] uint32_t TotalPoolCount() const noexcept;
    [[nodiscard]] uint32_t FramePoolCount(uint32_t frame_index_) const;
    [[nodiscard]] uint32_t TotalAllocatedSetCount() const noexcept;
    [[nodiscard]] uint32_t FrameAllocatedSetCount(uint32_t frame_index_) const;
    [[nodiscard]] bool ValidationEnabled() const noexcept;
    [[nodiscard]] DescriptorValidationStats ValidationStats() const noexcept;
    [[nodiscard]] const DescriptorHostStats& Stats() const noexcept;
#if VR_ENABLE_DESCRIPTOR_VALIDATION
    [[nodiscard]] bool IsDescriptorSetAlive(VkDescriptorSet set_) const noexcept;
#endif

private:
    struct LayoutBindingKey {
        uint32_t binding = 0U;
        VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        uint32_t descriptor_count = 0U;
        VkShaderStageFlags stage_flags = 0U;
        uint32_t immutable_sampler_offset = 0U;
    };

    struct LayoutKey {
        VkDescriptorSetLayoutCreateFlags flags = 0U;
        DescriptorMcVector<LayoutBindingKey> bindings{};
        DescriptorMcVector<VkDescriptorBindingFlags> binding_flags{};
        DescriptorMcVector<VkSampler> immutable_samplers{};
    };

    struct LayoutCacheEntry {
        uint64_t hash = 0U;
        LayoutKey key{};
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    };

    struct HashLookupNode {
        uint64_t hash = 0U;
        uint32_t entry_index = 0U;
    };

    struct DescriptorPoolPage {
        VkDescriptorPool pool = VK_NULL_HANDLE;
        uint32_t allocated_sets = 0U;
    };

    struct FramePoolArena {
        DescriptorMcVector<DescriptorPoolPage> pools{};
        uint32_t active_pool_index = 0U;
    };

    struct UpdateScratch {
        DescriptorMcVector<VkWriteDescriptorSet> writes{};
        DescriptorMcVector<VkDescriptorBufferInfo> buffer_infos{};
        DescriptorMcVector<VkDescriptorImageInfo> image_infos{};
        DescriptorMcVector<VkBufferView> texel_views{};
    };

    struct PendingBindlessWrite {
        uint32_t slot_index = 0U;
        std::uint64_t revision = 0U;
        VkDescriptorImageInfo image_info{};
    };

    struct DeferredBindlessFree {
        BindlessSlot slot{};
        std::uint64_t retire_value = 0U;
    };

    struct BindlessTable {
        BindlessTableDesc desc{};
        DescriptorSetLayoutId layout_id{};
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        uint32_t capacity = 0U;
        uint32_t live_count = 0U;
        std::uint64_t next_write_revision = 0U;

        DescriptorMcVector<uint32_t> generations{};
        DescriptorMcVector<uint8_t> initialized{};
        DescriptorMcVector<uint8_t> pending_free{};
        DescriptorMcVector<std::uint64_t> queued_write_revisions{};
        DescriptorMcVector<uint32_t> free_list{};
        DescriptorMcVector<PendingBindlessWrite> pending_writes{};
        DescriptorMcVector<DeferredBindlessFree> deferred_frees{};
    };

    struct BindlessUpdateScratch {
        DescriptorMcVector<VkWriteDescriptorSet> writes{};
        DescriptorMcVector<VkDescriptorImageInfo> image_infos{};
    };
#if VR_ENABLE_DESCRIPTOR_VALIDATION
    struct DescriptorSetValidationNode {
        uint64_t descriptor_set_bits = 0U;
        uint32_t frame_index = 0U;
        uint64_t frame_generation = 0U;
    };
#endif

    static constexpr uint32_t kInvalidSamplerOffset = 0xFFFFFFFFU;

    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);
    static void HashCombine(uint64_t& hash_, uint64_t value_) noexcept;
    static uint64_t HashLayoutKey(const LayoutKey& key_) noexcept;
    static bool LayoutKeyEquals(const LayoutKey& lhs_, const LayoutKey& rhs_) noexcept;
    static LayoutKey CanonicalizeLayoutDesc(const DescriptorSetLayoutDesc& layout_desc_);
    static DescriptorMcVector<DescriptorPoolSizeRatio> DefaultPoolRatios();
    [[nodiscard]] static uint32_t IdToIndex(uint32_t id_value_);
    [[nodiscard]] static DescriptorSetLayoutId MakeLayoutId(uint32_t entry_index_);
    [[nodiscard]] static uint32_t TableIdToIndex(uint32_t table_id_value_);
    [[nodiscard]] static BindlessTableId MakeTableId(uint32_t entry_index_);

    FramePoolArena& ArenaAt(uint32_t frame_index_);
    const FramePoolArena& ArenaAt(uint32_t frame_index_) const;
    BindlessTable& TableAt(BindlessTableId table_id_);
    const BindlessTable& TableAt(BindlessTableId table_id_) const;

    VkDescriptorPool CreatePool(VulkanContext& context_) const;
    VkDescriptorPool AcquirePoolForFrame(VulkanContext& context_,
                                         FramePoolArena& arena_);
    [[nodiscard]] static bool DescriptorTypeSupportsImageInfo(VkDescriptorType descriptor_type_) noexcept;
    [[nodiscard]] static bool IsPlaceholderImageInfoValid(VkDescriptorType descriptor_type_,
                                                          const VkDescriptorImageInfo& image_info_) noexcept;
    static void IncrementGeneration(uint32_t& generation_) noexcept;
    static void RequireBindlessCapsForTable(const VulkanContext& context_,
                                            const BindlessTableDesc& table_desc_);
    static void RequireAliveBindlessSlot(const BindlessTable& table_,
                                         BindlessSlot slot_,
                                         const char* action_);
    void QueueBindlessImageInfoWrite(BindlessTable& table_,
                                     BindlessSlot slot_,
                                     const VkDescriptorImageInfo& image_info_);
    void RefreshBindlessStats() noexcept;
#if VR_ENABLE_DESCRIPTOR_VALIDATION
    static uint64_t DescriptorSetHandleBits(VkDescriptorSet set_) noexcept;
    static uint32_t LowerBoundDescriptorSetBits(const DescriptorMcVector<DescriptorSetValidationNode>& nodes_,
                                                uint64_t descriptor_set_bits_) noexcept;
    void ValidationOnInitialize();
    void ValidationOnShutdown();
    void ValidationOnBeginFrame(uint32_t frame_index_);
    void ValidationTrackSet(uint32_t frame_index_,
                            VkDescriptorSet set_);
    void ValidationTrackSetArray(uint32_t frame_index_,
                                 const VkDescriptorSet* sets_,
                                 uint32_t set_count_);
    void ValidationRequireSetAlive(VkDescriptorSet set_);
#endif

private:
    DescriptorHostCreateInfo create_info_cache{};
    DescriptorMcVector<VkDescriptorPoolSize> pool_sizes_cache{};
    DescriptorMcVector<FramePoolArena> frame_arenas{};
    DescriptorMcVector<LayoutCacheEntry> layout_cache{};
    DescriptorMcVector<HashLookupNode> layout_lookup{};
    UpdateScratch update_scratch{};
    DescriptorMcVector<BindlessTable> bindless_tables{};
    BindlessUpdateScratch bindless_update_scratch{};
    DescriptorHostStats stats{};
#if VR_ENABLE_DESCRIPTOR_VALIDATION
    DescriptorMcVector<DescriptorSetValidationNode> descriptor_set_validation_nodes{};
    DescriptorMcVector<uint64_t> frame_validation_generations{};
    DescriptorValidationStats validation_stats{};
#endif
    bool initialized = false;
};

} // namespace vr::render

