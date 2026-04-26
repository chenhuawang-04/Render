#include "vr/render/descriptor_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace vr::render {

namespace {

[[nodiscard]] const char* VkResultName(VkResult result_) noexcept {
    switch (result_) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
#ifdef VK_ERROR_OUT_OF_POOL_MEMORY
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
#endif
        default: return "VK_ERROR_UNKNOWN";
    }
}

template<typename HandleT>
[[nodiscard]] uint64_t HandleBits(HandleT handle_) noexcept {
    if constexpr (std::is_pointer_v<HandleT>) {
        return static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(handle_));
    } else {
        return static_cast<uint64_t>(handle_);
    }
}

template<typename LookupVectorT>
[[nodiscard]] uint32_t LowerBoundLookupByHash(const LookupVectorT& lookup_, uint64_t hash_) noexcept {
    uint32_t first = 0U;
    uint32_t count = static_cast<uint32_t>(lookup_.size());
    while (count > 0U) {
        const uint32_t step = count / 2U;
        const uint32_t it = first + step;
        if (lookup_[it].hash < hash_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

template<typename LookupVectorT>
void InsertLookupNode(LookupVectorT& lookup_, uint64_t hash_, uint32_t entry_index_) {
    typename LookupVectorT::value_type node{};
    node.hash = hash_;
    node.entry_index = entry_index_;

    const uint32_t insert_pos = LowerBoundLookupByHash(lookup_, hash_);
    lookup_.push_back(node);
    for (uint32_t i = static_cast<uint32_t>(lookup_.size() - 1U); i > insert_pos; --i) {
        lookup_[i] = lookup_[i - 1U];
    }
    lookup_[insert_pos] = node;
}

template<typename LookupVectorT, typename EntryVectorT, typename EntryT, typename EqualsFnT>
[[nodiscard]] uint32_t FindEntryIndex(const LookupVectorT& lookup_,
                                      const EntryVectorT& entries_,
                                      uint64_t hash_,
                                      const EntryT& candidate_,
                                      EqualsFnT equals_) noexcept {
    const uint32_t begin = LowerBoundLookupByHash(lookup_, hash_);
    for (uint32_t i = begin; i < lookup_.size(); ++i) {
        const auto& node = lookup_[i];
        if (node.hash != hash_) {
            break;
        }
        if (node.entry_index >= entries_.size()) {
            continue;
        }
        if (equals_(entries_[node.entry_index], candidate_)) {
            return node.entry_index;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

} // namespace

void DescriptorHost::Initialize(VulkanContext& context_,
                                const DescriptorHostCreateInfo& create_info_) {
    if (!context_.IsDeviceInitialized()) {
        throw std::runtime_error("DescriptorHost::Initialize requires initialized Vulkan device");
    }

    Shutdown(context_);

    create_info_cache = create_info_;
    if (create_info_cache.frames_in_flight == 0U) {
        create_info_cache.frames_in_flight = 1U;
    }
    if (create_info_cache.max_sets_per_pool == 0U) {
        create_info_cache.max_sets_per_pool = 1U;
    }
    if (create_info_cache.pool_size_ratios.empty()) {
        create_info_cache.pool_size_ratios = DefaultPoolRatios();
    }

    if (create_info_cache.reserve_layout_count > 0U) {
        layout_cache.reserve(create_info_cache.reserve_layout_count);
        layout_lookup.reserve(create_info_cache.reserve_layout_count);
    }

    pool_sizes_cache.clear();
    for (const auto& ratio : create_info_cache.pool_size_ratios) {
        const float clamped_ratio = std::max(0.0F, ratio.ratio);
        const float scaled = static_cast<float>(create_info_cache.max_sets_per_pool) * clamped_ratio;
        uint32_t descriptor_count = static_cast<uint32_t>(std::ceil(scaled));
        descriptor_count = std::max<uint32_t>(1U, descriptor_count);

        bool merged = false;
        for (auto& pool_size : pool_sizes_cache) {
            if (pool_size.type == ratio.descriptor_type) {
                if (pool_size.descriptorCount > std::numeric_limits<uint32_t>::max() - descriptor_count) {
                    throw std::runtime_error("DescriptorHost pool size overflow");
                }
                pool_size.descriptorCount += descriptor_count;
                merged = true;
                break;
            }
        }
        if (!merged) {
            VkDescriptorPoolSize pool_size{};
            pool_size.type = ratio.descriptor_type;
            pool_size.descriptorCount = descriptor_count;
            pool_sizes_cache.push_back(pool_size);
        }
    }

    if (pool_sizes_cache.empty()) {
        throw std::runtime_error("DescriptorHost::Initialize resolved empty descriptor pool sizes");
    }

    frame_arenas.resize(create_info_cache.frames_in_flight);
    if (create_info_cache.preallocate_first_pool_per_frame) {
        for (auto& arena : frame_arenas) {
            VkDescriptorPool pool = CreatePool(context_);
            arena.pools.push_back({.pool = pool, .allocated_sets = 0U});
            arena.active_pool_index = 0U;
        }
    }

    initialized = true;
}

void DescriptorHost::Shutdown(VulkanContext& context_) {
    if (!initialized &&
        frame_arenas.empty() &&
        layout_cache.empty() &&
        layout_lookup.empty()) {
        return;
    }

    const VkDevice device = context_.Device();
    if (device != VK_NULL_HANDLE) {
        for (auto& entry : layout_cache) {
            if (entry.layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, entry.layout, nullptr);
                entry.layout = VK_NULL_HANDLE;
            }
        }

        for (auto& arena : frame_arenas) {
            for (auto& page : arena.pools) {
                if (page.pool != VK_NULL_HANDLE) {
                    vkDestroyDescriptorPool(device, page.pool, nullptr);
                    page.pool = VK_NULL_HANDLE;
                }
                page.allocated_sets = 0U;
            }
            arena.pools.clear();
            arena.active_pool_index = 0U;
        }
    } else {
        for (auto& entry : layout_cache) {
            entry.layout = VK_NULL_HANDLE;
        }
        for (auto& arena : frame_arenas) {
            arena.pools.clear();
            arena.active_pool_index = 0U;
        }
    }

    layout_cache.clear();
    layout_lookup.clear();
    frame_arenas.clear();
    pool_sizes_cache.clear();
    update_scratch.writes.clear();
    update_scratch.buffer_infos.clear();
    update_scratch.image_infos.clear();
    update_scratch.texel_views.clear();
    create_info_cache = {};
    initialized = false;
}

void DescriptorHost::BeginFrame(VulkanContext& context_, uint32_t frame_index_) {
    if (!initialized) {
        throw std::runtime_error("DescriptorHost::BeginFrame called before Initialize");
    }

    FramePoolArena& arena = ArenaAt(frame_index_);
    for (auto& page : arena.pools) {
        CheckVk("vkResetDescriptorPool(frame)",
                vkResetDescriptorPool(context_.Device(), page.pool, 0U));
        page.allocated_sets = 0U;
    }
    arena.active_pool_index = 0U;
}

DescriptorSetLayoutId DescriptorHost::RegisterLayout(VulkanContext& context_,
                                                     const DescriptorSetLayoutDesc& layout_desc_) {
    if (!initialized) {
        throw std::runtime_error("DescriptorHost::RegisterLayout called before Initialize");
    }

    LayoutKey key = CanonicalizeLayoutDesc(layout_desc_);
    const uint64_t hash = HashLayoutKey(key);

    const uint32_t existing_index = FindEntryIndex(
        layout_lookup,
        layout_cache,
        hash,
        key,
        [](const LayoutCacheEntry& entry_, const LayoutKey& key_) noexcept {
            return LayoutKeyEquals(entry_.key, key_);
        });
    if (existing_index != std::numeric_limits<uint32_t>::max()) {
        return MakeLayoutId(existing_index);
    }

    DescriptorMcVector<VkDescriptorSetLayoutBinding> vk_bindings;
    vk_bindings.resize(key.bindings.size());
    for (uint32_t i = 0U; i < key.bindings.size(); ++i) {
        const LayoutBindingKey& binding_key = key.bindings[i];
        VkDescriptorSetLayoutBinding vk_binding{};
        vk_binding.binding = binding_key.binding;
        vk_binding.descriptorType = binding_key.descriptor_type;
        vk_binding.descriptorCount = binding_key.descriptor_count;
        vk_binding.stageFlags = binding_key.stage_flags;
        if (binding_key.immutable_sampler_offset != kInvalidSamplerOffset) {
            vk_binding.pImmutableSamplers =
                key.immutable_samplers.data() + binding_key.immutable_sampler_offset;
        } else {
            vk_binding.pImmutableSamplers = nullptr;
        }
        vk_bindings[i] = vk_binding;
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{};
    binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    binding_flags_info.bindingCount = static_cast<uint32_t>(key.binding_flags.size());
    binding_flags_info.pBindingFlags = key.binding_flags.empty()
        ? nullptr
        : key.binding_flags.data();

    VkDescriptorSetLayoutCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.pNext = key.binding_flags.empty() ? nullptr : &binding_flags_info;
    create_info.flags = key.flags;
    create_info.bindingCount = static_cast<uint32_t>(vk_bindings.size());
    create_info.pBindings = vk_bindings.empty() ? nullptr : vk_bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    CheckVk("vkCreateDescriptorSetLayout",
            vkCreateDescriptorSetLayout(context_.Device(), &create_info, nullptr, &layout));

    LayoutCacheEntry entry{};
    entry.hash = hash;
    entry.key = std::move(key);
    entry.layout = layout;
    if (layout_cache.size() >= std::numeric_limits<uint32_t>::max()) {
        vkDestroyDescriptorSetLayout(context_.Device(), layout, nullptr);
        throw std::runtime_error("DescriptorHost layout registry overflow");
    }
    layout_cache.push_back(std::move(entry));
    const uint32_t new_index = static_cast<uint32_t>(layout_cache.size() - 1U);
    InsertLookupNode(layout_lookup, hash, new_index);
    return MakeLayoutId(new_index);
}

VkDescriptorSetLayout DescriptorHost::AcquireLayout(VulkanContext& context_,
                                                    const DescriptorSetLayoutDesc& layout_desc_) {
    const DescriptorSetLayoutId layout_id = RegisterLayout(context_, layout_desc_);
    return GetLayout(layout_id);
}

VkDescriptorSetLayout DescriptorHost::GetLayout(DescriptorSetLayoutId layout_id_) const {
    if (!layout_id_.IsValid()) {
        throw std::runtime_error("DescriptorHost::GetLayout received invalid layout id");
    }
    const uint32_t index = IdToIndex(layout_id_.value);
    if (index >= layout_cache.size()) {
        throw std::out_of_range("DescriptorHost::GetLayout id out of range");
    }
    return layout_cache[index].layout;
}

VkDescriptorSet DescriptorHost::AllocateSet(VulkanContext& context_,
                                            uint32_t frame_index_,
                                            VkDescriptorSetLayout layout_) {
    if (!initialized) {
        throw std::runtime_error("DescriptorHost::AllocateSet called before Initialize");
    }
    if (layout_ == VK_NULL_HANDLE) {
        throw std::runtime_error("DescriptorHost::AllocateSet requires valid layout");
    }

    FramePoolArena& arena = ArenaAt(frame_index_);

    for (uint32_t attempts = 0U; attempts < 4096U; ++attempts) {
        VkDescriptorPool pool = AcquirePoolForFrame(context_, arena);

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = pool;
        alloc_info.descriptorSetCount = 1U;
        alloc_info.pSetLayouts = &layout_;

        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult result = vkAllocateDescriptorSets(context_.Device(), &alloc_info, &set);
        if (result == VK_SUCCESS) {
            arena.pools[arena.active_pool_index].allocated_sets += 1U;
            return set;
        }

#ifdef VK_ERROR_OUT_OF_POOL_MEMORY
        const bool pool_exhausted = result == VK_ERROR_OUT_OF_POOL_MEMORY ||
                                    result == VK_ERROR_FRAGMENTED_POOL;
#else
        const bool pool_exhausted = result == VK_ERROR_FRAGMENTED_POOL;
#endif
        if (pool_exhausted) {
            ++arena.active_pool_index;
            continue;
        }

        ThrowVk("vkAllocateDescriptorSets", result);
    }

    throw std::runtime_error(
        "DescriptorHost::AllocateSet exceeded retry budget; increase pool ratios or max_sets_per_pool");
}

VkDescriptorSet DescriptorHost::AllocateSet(VulkanContext& context_,
                                            uint32_t frame_index_,
                                            DescriptorSetLayoutId layout_id_) {
    return AllocateSet(context_, frame_index_, GetLayout(layout_id_));
}

void DescriptorHost::AllocateSets(VulkanContext& context_,
                                  uint32_t frame_index_,
                                  VkDescriptorSetLayout layout_,
                                  uint32_t set_count_,
                                  VkDescriptorSet* out_sets_) {
    if (!initialized) {
        throw std::runtime_error("DescriptorHost::AllocateSets called before Initialize");
    }
    if (layout_ == VK_NULL_HANDLE) {
        throw std::runtime_error("DescriptorHost::AllocateSets requires valid layout");
    }
    if (set_count_ == 0U) {
        return;
    }
    if (out_sets_ == nullptr && set_count_ > 0U) {
        throw std::runtime_error("DescriptorHost::AllocateSets requires valid out_sets");
    }

    FramePoolArena& arena = ArenaAt(frame_index_);
    constexpr uint32_t kLayoutBatchCapacity = 256U;
    std::array<VkDescriptorSetLayout, kLayoutBatchCapacity> layout_batch{};
    layout_batch.fill(layout_);

    uint32_t allocated = 0U;
    for (uint32_t attempts = 0U; attempts < 16384U && allocated < set_count_; ++attempts) {
        VkDescriptorPool pool = AcquirePoolForFrame(context_, arena);
        DescriptorPoolPage& page = arena.pools[arena.active_pool_index];

        uint32_t remaining_capacity = 0U;
        if (page.allocated_sets < create_info_cache.max_sets_per_pool) {
            remaining_capacity = create_info_cache.max_sets_per_pool - page.allocated_sets;
        }
        if (remaining_capacity == 0U) {
            ++arena.active_pool_index;
            continue;
        }

        const uint32_t remaining_sets = set_count_ - allocated;
        const uint32_t alloc_count = std::min(std::min(remaining_capacity, remaining_sets),
                                              kLayoutBatchCapacity);
        if (alloc_count == 0U) {
            break;
        }

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = pool;
        alloc_info.descriptorSetCount = alloc_count;
        alloc_info.pSetLayouts = layout_batch.data();

        const VkResult result = vkAllocateDescriptorSets(context_.Device(),
                                                         &alloc_info,
                                                         out_sets_ + allocated);
        if (result == VK_SUCCESS) {
            page.allocated_sets += alloc_count;
            allocated += alloc_count;
            continue;
        }

#ifdef VK_ERROR_OUT_OF_POOL_MEMORY
        const bool pool_exhausted = result == VK_ERROR_OUT_OF_POOL_MEMORY ||
                                    result == VK_ERROR_FRAGMENTED_POOL;
#else
        const bool pool_exhausted = result == VK_ERROR_FRAGMENTED_POOL;
#endif
        if (pool_exhausted) {
            ++arena.active_pool_index;
            continue;
        }

        ThrowVk("vkAllocateDescriptorSets", result);
    }

    if (allocated != set_count_) {
        throw std::runtime_error(
            "DescriptorHost::AllocateSets exceeded retry budget; increase pool ratios or max_sets_per_pool");
    }
}

void DescriptorHost::AllocateSets(VulkanContext& context_,
                                  uint32_t frame_index_,
                                  DescriptorSetLayoutId layout_id_,
                                  uint32_t set_count_,
                                  VkDescriptorSet* out_sets_) {
    AllocateSets(context_, frame_index_, GetLayout(layout_id_), set_count_, out_sets_);
}

void DescriptorHost::UpdateSet(VkDevice device_,
                               VkDescriptorSet set_,
                               const DescriptorMcVector<DescriptorBufferWrite>& buffer_writes_,
                               const DescriptorMcVector<DescriptorImageWrite>& image_writes_,
                               const DescriptorMcVector<DescriptorTexelBufferWrite>& texel_writes_) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("DescriptorHost::UpdateSet requires valid VkDevice");
    }
    if (set_ == VK_NULL_HANDLE) {
        throw std::runtime_error("DescriptorHost::UpdateSet requires valid VkDescriptorSet");
    }

    UpdateScratch& scratch = update_scratch;
    scratch.writes.clear();
    scratch.buffer_infos.clear();
    scratch.image_infos.clear();
    scratch.texel_views.clear();

    scratch.writes.reserve(buffer_writes_.size() + image_writes_.size() + texel_writes_.size());
    scratch.buffer_infos.reserve(buffer_writes_.size());
    scratch.image_infos.reserve(image_writes_.size());
    scratch.texel_views.reserve(texel_writes_.size());

    for (const auto& write : buffer_writes_) {
        if (write.buffer == VK_NULL_HANDLE) {
            throw std::runtime_error("DescriptorHost::UpdateSet buffer write has null buffer");
        }

        VkDescriptorBufferInfo info{};
        info.buffer = write.buffer;
        info.offset = write.offset;
        info.range = write.range;
        scratch.buffer_infos.push_back(info);

        VkWriteDescriptorSet vk_write{};
        vk_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vk_write.dstSet = set_;
        vk_write.dstBinding = write.binding;
        vk_write.dstArrayElement = write.array_element;
        vk_write.descriptorCount = 1U;
        vk_write.descriptorType = write.descriptor_type;
        vk_write.pBufferInfo = &scratch.buffer_infos.back();
        scratch.writes.push_back(vk_write);
    }

    for (const auto& write : image_writes_) {
        if (write.image_view == VK_NULL_HANDLE &&
            write.descriptor_type != VK_DESCRIPTOR_TYPE_SAMPLER) {
            throw std::runtime_error("DescriptorHost::UpdateSet image write has null image_view");
        }

        VkDescriptorImageInfo info{};
        info.sampler = write.sampler;
        info.imageView = write.image_view;
        info.imageLayout = write.image_layout;
        scratch.image_infos.push_back(info);

        VkWriteDescriptorSet vk_write{};
        vk_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vk_write.dstSet = set_;
        vk_write.dstBinding = write.binding;
        vk_write.dstArrayElement = write.array_element;
        vk_write.descriptorCount = 1U;
        vk_write.descriptorType = write.descriptor_type;
        vk_write.pImageInfo = &scratch.image_infos.back();
        scratch.writes.push_back(vk_write);
    }

    for (const auto& write : texel_writes_) {
        if (write.buffer_view == VK_NULL_HANDLE) {
            throw std::runtime_error("DescriptorHost::UpdateSet texel write has null buffer_view");
        }

        scratch.texel_views.push_back(write.buffer_view);

        VkWriteDescriptorSet vk_write{};
        vk_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vk_write.dstSet = set_;
        vk_write.dstBinding = write.binding;
        vk_write.dstArrayElement = write.array_element;
        vk_write.descriptorCount = 1U;
        vk_write.descriptorType = write.descriptor_type;
        vk_write.pTexelBufferView = &scratch.texel_views.back();
        scratch.writes.push_back(vk_write);
    }

    if (scratch.writes.empty()) {
        return;
    }

    vkUpdateDescriptorSets(device_,
                           static_cast<uint32_t>(scratch.writes.size()),
                           scratch.writes.data(),
                           0U,
                           nullptr);
}

void DescriptorHost::UpdateSet(VulkanContext& context_,
                               VkDescriptorSet set_,
                               const DescriptorMcVector<DescriptorBufferWrite>& buffer_writes_,
                               const DescriptorMcVector<DescriptorImageWrite>& image_writes_,
                               const DescriptorMcVector<DescriptorTexelBufferWrite>& texel_writes_) {
    UpdateSet(context_.Device(), set_, buffer_writes_, image_writes_, texel_writes_);
}

bool DescriptorHost::IsInitialized() const noexcept {
    return initialized;
}

uint32_t DescriptorHost::FramesInFlight() const noexcept {
    return static_cast<uint32_t>(frame_arenas.size());
}

uint32_t DescriptorHost::MaxSetsPerPool() const noexcept {
    return create_info_cache.max_sets_per_pool;
}

uint32_t DescriptorHost::CachedLayoutCount() const noexcept {
    return static_cast<uint32_t>(layout_cache.size());
}

uint32_t DescriptorHost::TotalPoolCount() const noexcept {
    uint32_t count = 0U;
    for (const auto& arena : frame_arenas) {
        count += static_cast<uint32_t>(arena.pools.size());
    }
    return count;
}

uint32_t DescriptorHost::FramePoolCount(uint32_t frame_index_) const {
    return static_cast<uint32_t>(ArenaAt(frame_index_).pools.size());
}

void DescriptorHost::ThrowVk(const char* stage_, VkResult result_) {
    std::ostringstream oss;
    oss << stage_ << " failed: " << VkResultName(result_) << " (" << static_cast<int>(result_) << ")";
    throw std::runtime_error(oss.str());
}

void DescriptorHost::CheckVk(const char* stage_, VkResult result_) {
    if (result_ != VK_SUCCESS) {
        ThrowVk(stage_, result_);
    }
}

void DescriptorHost::HashCombine(uint64_t& hash_, uint64_t value_) noexcept {
    hash_ ^= value_ + 0x9e3779b97f4a7c15ULL + (hash_ << 6U) + (hash_ >> 2U);
}

uint64_t DescriptorHost::HashLayoutKey(const LayoutKey& key_) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;
    HashCombine(hash, static_cast<uint64_t>(key_.flags));
    HashCombine(hash, static_cast<uint64_t>(key_.bindings.size()));
    HashCombine(hash, static_cast<uint64_t>(key_.binding_flags.size()));

    for (uint32_t i = 0U; i < key_.bindings.size(); ++i) {
        const LayoutBindingKey& binding = key_.bindings[i];
        HashCombine(hash, static_cast<uint64_t>(binding.binding));
        HashCombine(hash, static_cast<uint64_t>(binding.descriptor_type));
        HashCombine(hash, static_cast<uint64_t>(binding.descriptor_count));
        HashCombine(hash, static_cast<uint64_t>(binding.stage_flags));
        HashCombine(hash, static_cast<uint64_t>(binding.immutable_sampler_offset));

        if (binding.immutable_sampler_offset != kInvalidSamplerOffset) {
            const uint32_t count = binding.descriptor_count;
            const uint32_t offset = binding.immutable_sampler_offset;
            for (uint32_t j = 0U; j < count; ++j) {
                HashCombine(hash, HandleBits(key_.immutable_samplers[offset + j]));
            }
        }
    }

    for (VkDescriptorBindingFlags flags : key_.binding_flags) {
        HashCombine(hash, static_cast<uint64_t>(flags));
    }

    return hash;
}

bool DescriptorHost::LayoutKeyEquals(const LayoutKey& lhs_, const LayoutKey& rhs_) noexcept {
    if (lhs_.flags != rhs_.flags) {
        return false;
    }
    if (lhs_.bindings.size() != rhs_.bindings.size()) {
        return false;
    }
    if (lhs_.binding_flags.size() != rhs_.binding_flags.size()) {
        return false;
    }
    if (lhs_.immutable_samplers.size() != rhs_.immutable_samplers.size()) {
        return false;
    }

    for (uint32_t i = 0U; i < lhs_.bindings.size(); ++i) {
        const auto& l = lhs_.bindings[i];
        const auto& r = rhs_.bindings[i];
        if (l.binding != r.binding ||
            l.descriptor_type != r.descriptor_type ||
            l.descriptor_count != r.descriptor_count ||
            l.stage_flags != r.stage_flags ||
            l.immutable_sampler_offset != r.immutable_sampler_offset) {
            return false;
        }

        if (l.immutable_sampler_offset != kInvalidSamplerOffset) {
            for (uint32_t j = 0U; j < l.descriptor_count; ++j) {
                if (lhs_.immutable_samplers[l.immutable_sampler_offset + j] !=
                    rhs_.immutable_samplers[r.immutable_sampler_offset + j]) {
                    return false;
                }
            }
        }
    }

    for (uint32_t i = 0U; i < lhs_.binding_flags.size(); ++i) {
        if (lhs_.binding_flags[i] != rhs_.binding_flags[i]) {
            return false;
        }
    }
    return true;
}

DescriptorHost::LayoutKey DescriptorHost::CanonicalizeLayoutDesc(const DescriptorSetLayoutDesc& layout_desc_) {
    struct SortItem {
        VkDescriptorSetLayoutBinding binding{};
        VkDescriptorBindingFlags flags = 0U;
    };

    LayoutKey key{};
    key.flags = layout_desc_.flags;

    const bool has_binding_flags = !layout_desc_.binding_flags.empty();
    if (has_binding_flags && layout_desc_.binding_flags.size() != layout_desc_.bindings.size()) {
        throw std::runtime_error("DescriptorSetLayoutDesc.binding_flags size must match bindings size");
    }

    DescriptorMcVector<SortItem> items;
    items.resize(layout_desc_.bindings.size());
    for (uint32_t i = 0U; i < layout_desc_.bindings.size(); ++i) {
        items[i].binding = layout_desc_.bindings[i];
        items[i].flags = has_binding_flags ? layout_desc_.binding_flags[i] : 0U;
    }

    std::sort(items.begin(),
              items.end(),
              [](const SortItem& lhs_, const SortItem& rhs_) {
                  return lhs_.binding.binding < rhs_.binding.binding;
              });

    key.bindings.resize(items.size());
    if (has_binding_flags) {
        key.binding_flags.resize(items.size());
    }

    uint32_t immutable_sampler_total_count = 0U;
    for (const auto& item : items) {
        if (item.binding.pImmutableSamplers != nullptr) {
            if (immutable_sampler_total_count > std::numeric_limits<uint32_t>::max() - item.binding.descriptorCount) {
                throw std::runtime_error("DescriptorHost immutable sampler count overflow");
            }
            immutable_sampler_total_count += item.binding.descriptorCount;
        }
    }
    key.immutable_samplers.reserve(immutable_sampler_total_count);

    uint32_t previous_binding = std::numeric_limits<uint32_t>::max();
    for (uint32_t i = 0U; i < items.size(); ++i) {
        const auto& item = items[i];
        if (i > 0U && item.binding.binding == previous_binding) {
            throw std::runtime_error("DescriptorSetLayoutDesc has duplicate binding index");
        }
        previous_binding = item.binding.binding;

        LayoutBindingKey binding_key{};
        binding_key.binding = item.binding.binding;
        binding_key.descriptor_type = item.binding.descriptorType;
        binding_key.descriptor_count = item.binding.descriptorCount;
        binding_key.stage_flags = item.binding.stageFlags;
        binding_key.immutable_sampler_offset = kInvalidSamplerOffset;

        if (item.binding.pImmutableSamplers != nullptr && item.binding.descriptorCount > 0U) {
            binding_key.immutable_sampler_offset = static_cast<uint32_t>(key.immutable_samplers.size());
            for (uint32_t j = 0U; j < item.binding.descriptorCount; ++j) {
                key.immutable_samplers.push_back(item.binding.pImmutableSamplers[j]);
            }
        }

        key.bindings[i] = binding_key;
        if (has_binding_flags) {
            key.binding_flags[i] = item.flags;
        }
    }

    return key;
}

DescriptorMcVector<DescriptorPoolSizeRatio> DescriptorHost::DefaultPoolRatios() {
    DescriptorMcVector<DescriptorPoolSizeRatio> ratios;
    ratios.push_back({.descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLER, .ratio = 0.50F});
    ratios.push_back({.descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .ratio = 4.00F});
    ratios.push_back({.descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .ratio = 3.00F});
    ratios.push_back({.descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .ratio = 1.00F});
    ratios.push_back({.descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .ratio = 3.00F});
    ratios.push_back({.descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .ratio = 2.00F});
    ratios.push_back({.descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .ratio = 1.00F});
    ratios.push_back({.descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, .ratio = 1.00F});
    ratios.push_back({.descriptor_type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, .ratio = 0.50F});
    return ratios;
}

uint32_t DescriptorHost::IdToIndex(uint32_t id_value_) {
    if (id_value_ == 0U) {
        throw std::runtime_error("DescriptorHost layout id must be non-zero");
    }
    return id_value_ - 1U;
}

DescriptorSetLayoutId DescriptorHost::MakeLayoutId(uint32_t entry_index_) {
    DescriptorSetLayoutId id{};
    id.value = entry_index_ + 1U;
    return id;
}

DescriptorHost::FramePoolArena& DescriptorHost::ArenaAt(uint32_t frame_index_) {
    if (frame_index_ >= frame_arenas.size()) {
        throw std::out_of_range("DescriptorHost frame_index out of range");
    }
    return frame_arenas[frame_index_];
}

const DescriptorHost::FramePoolArena& DescriptorHost::ArenaAt(uint32_t frame_index_) const {
    if (frame_index_ >= frame_arenas.size()) {
        throw std::out_of_range("DescriptorHost frame_index out of range");
    }
    return frame_arenas[frame_index_];
}

VkDescriptorPool DescriptorHost::CreatePool(VulkanContext& context_) const {
    VkDescriptorPoolCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    create_info.flags = create_info_cache.pool_flags;
    create_info.maxSets = create_info_cache.max_sets_per_pool;
    create_info.poolSizeCount = static_cast<uint32_t>(pool_sizes_cache.size());
    create_info.pPoolSizes = pool_sizes_cache.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;
    CheckVk("vkCreateDescriptorPool",
            vkCreateDescriptorPool(context_.Device(), &create_info, nullptr, &pool));
    return pool;
}

VkDescriptorPool DescriptorHost::AcquirePoolForFrame(VulkanContext& context_,
                                                     FramePoolArena& arena_) {
    if (arena_.active_pool_index < arena_.pools.size()) {
        return arena_.pools[arena_.active_pool_index].pool;
    }

    VkDescriptorPool new_pool = CreatePool(context_);
    arena_.pools.push_back({.pool = new_pool, .allocated_sets = 0U});
    arena_.active_pool_index = static_cast<uint32_t>(arena_.pools.size() - 1U);
    return new_pool;
}

} // namespace vr::render
