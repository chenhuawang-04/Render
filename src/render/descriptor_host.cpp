#include "vr/render/descriptor_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
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

    bindless_tables.clear();
    bindless_update_scratch.writes.clear();
    bindless_update_scratch.image_infos.clear();
    stats = {};

#if VR_ENABLE_DESCRIPTOR_VALIDATION
    ValidationOnInitialize();
#endif
    initialized = true;
}

void DescriptorHost::Shutdown(VulkanContext& context_) {
    if (!initialized &&
        frame_arenas.empty() &&
        bindless_tables.empty() &&
        layout_cache.empty() &&
        layout_lookup.empty()) {
        return;
    }

    const VkDevice device = context_.Device();
    if (device != VK_NULL_HANDLE) {
        for (auto& table : bindless_tables) {
            if (table.pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, table.pool, nullptr);
                table.pool = VK_NULL_HANDLE;
            }
            table.set = VK_NULL_HANDLE;
        }
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
        for (auto& table : bindless_tables) {
            table.pool = VK_NULL_HANDLE;
            table.set = VK_NULL_HANDLE;
        }
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
    bindless_tables.clear();
    frame_arenas.clear();
    pool_sizes_cache.clear();
    update_scratch.writes.clear();
    update_scratch.buffer_infos.clear();
    update_scratch.image_infos.clear();
    update_scratch.texel_views.clear();
    bindless_update_scratch.writes.clear();
    bindless_update_scratch.image_infos.clear();
    stats = {};
#if VR_ENABLE_DESCRIPTOR_VALIDATION
    ValidationOnShutdown();
#endif
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
#if VR_ENABLE_DESCRIPTOR_VALIDATION
    ValidationOnBeginFrame(frame_index_);
#endif
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
#if VR_ENABLE_DESCRIPTOR_VALIDATION
            ValidationTrackSet(frame_index_, set);
#endif
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
#if VR_ENABLE_DESCRIPTOR_VALIDATION
            ValidationTrackSetArray(frame_index_, out_sets_ + allocated, alloc_count);
#endif
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
#if VR_ENABLE_DESCRIPTOR_VALIDATION
    if (create_info_cache.enable_validation) {
        ValidationRequireSetAlive(set_);
    }
#endif

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

    ++stats.transient_update_call_count;
    stats.transient_descriptor_write_count +=
        static_cast<std::uint64_t>(scratch.writes.size());
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

BindlessTableId DescriptorHost::CreateBindlessTable(VulkanContext& context_,
                                                    const BindlessTableDesc& table_desc_) {
    if (!initialized) {
        throw std::runtime_error("DescriptorHost::CreateBindlessTable called before Initialize");
    }
    if (table_desc_.requested_capacity == 0U) {
        throw std::runtime_error("DescriptorHost::CreateBindlessTable requires non-zero requested_capacity");
    }

    RequireBindlessCapsForTable(context_, table_desc_);

    DescriptorSetLayoutDesc layout_desc{};
    layout_desc.flags = table_desc_.enable_update_after_bind
        ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
        : 0U;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0U;
    binding.descriptorType = table_desc_.descriptor_type;
    binding.descriptorCount = table_desc_.requested_capacity;
    binding.stageFlags = table_desc_.stage_flags;
    layout_desc.bindings.push_back(binding);

    VkDescriptorBindingFlags binding_flags = 0U;
    if (table_desc_.enable_partially_bound) {
        binding_flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    }
    if (table_desc_.enable_variable_descriptor_count) {
        binding_flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    }
    if (table_desc_.enable_update_after_bind) {
        binding_flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    }
    layout_desc.binding_flags.push_back(binding_flags);

    LayoutKey layout_key = CanonicalizeLayoutDesc(layout_desc);
    DescriptorMcVector<VkDescriptorSetLayoutBinding> vk_bindings{};
    vk_bindings.resize(layout_key.bindings.size());
    for (uint32_t i = 0U; i < layout_key.bindings.size(); ++i) {
        const LayoutBindingKey& binding_key = layout_key.bindings[i];
        VkDescriptorSetLayoutBinding vk_binding{};
        vk_binding.binding = binding_key.binding;
        vk_binding.descriptorType = binding_key.descriptor_type;
        vk_binding.descriptorCount = binding_key.descriptor_count;
        vk_binding.stageFlags = binding_key.stage_flags;
        vk_bindings[i] = vk_binding;
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{};
    binding_flags_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    binding_flags_info.bindingCount = static_cast<uint32_t>(layout_key.binding_flags.size());
    binding_flags_info.pBindingFlags = layout_key.binding_flags.empty()
        ? nullptr
        : layout_key.binding_flags.data();

    VkDescriptorSetLayoutCreateInfo layout_create_info{};
    layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_create_info.pNext = layout_key.binding_flags.empty() ? nullptr : &binding_flags_info;
    layout_create_info.flags = layout_key.flags;
    layout_create_info.bindingCount = static_cast<uint32_t>(vk_bindings.size());
    layout_create_info.pBindings = vk_bindings.data();

    const DescriptorSetLayoutSupportInfo layout_support =
        context_.QueryDescriptorSetLayoutSupport(layout_create_info);
    uint32_t actual_capacity = table_desc_.requested_capacity;
    if (table_desc_.enable_variable_descriptor_count) {
        if (layout_support.max_variable_descriptor_count == 0U) {
            throw std::runtime_error(
                "DescriptorHost::CreateBindlessTable layout support reports zero max_variable_descriptor_count");
        }
        actual_capacity = std::min(actual_capacity, layout_support.max_variable_descriptor_count);
    } else if (!layout_support.supported) {
        throw std::runtime_error(
            "DescriptorHost::CreateBindlessTable layout is not supported on the current device");
    }
    actual_capacity = std::max(1U, actual_capacity);
    if (actual_capacity != table_desc_.requested_capacity || !layout_support.supported) {
        vk_bindings[0].descriptorCount = actual_capacity;
        layout_create_info.pBindings = vk_bindings.data();
        const DescriptorSetLayoutSupportInfo clamped_support =
            context_.QueryDescriptorSetLayoutSupport(layout_create_info);
        if (!clamped_support.supported) {
            throw std::runtime_error(
                "DescriptorHost::CreateBindlessTable clamped layout is still unsupported on the current device");
        }
    }

    layout_desc.bindings[0].descriptorCount = actual_capacity;
    const DescriptorSetLayoutId layout_id = RegisterLayout(context_, layout_desc);
    const VkDescriptorSetLayout layout = GetLayout(layout_id);

    VkDescriptorPoolSize pool_size{};
    pool_size.type = table_desc_.descriptor_type;
    pool_size.descriptorCount = actual_capacity;

    VkDescriptorPoolCreateInfo pool_create_info{};
    pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create_info.flags = table_desc_.enable_update_after_bind
        ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
        : 0U;
    pool_create_info.maxSets = 1U;
    pool_create_info.poolSizeCount = 1U;
    pool_create_info.pPoolSizes = &pool_size;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    CheckVk("vkCreateDescriptorPool(bindless)",
            vkCreateDescriptorPool(context_.Device(), &pool_create_info, nullptr, &pool));

    const uint32_t variable_count = actual_capacity;
    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info{};
    variable_count_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variable_count_info.descriptorSetCount = 1U;
    variable_count_info.pDescriptorCounts = &variable_count;

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.pNext = table_desc_.enable_variable_descriptor_count
        ? &variable_count_info
        : nullptr;
    alloc_info.descriptorPool = pool;
    alloc_info.descriptorSetCount = 1U;
    alloc_info.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult alloc_result = vkAllocateDescriptorSets(context_.Device(), &alloc_info, &set);
    if (alloc_result != VK_SUCCESS) {
        vkDestroyDescriptorPool(context_.Device(), pool, nullptr);
        ThrowVk("vkAllocateDescriptorSets(bindless)", alloc_result);
    }

    if (bindless_tables.size() >= std::numeric_limits<uint32_t>::max()) {
        vkDestroyDescriptorPool(context_.Device(), pool, nullptr);
        throw std::runtime_error("DescriptorHost bindless table registry overflow");
    }

    BindlessTable table{};
    table.desc = table_desc_;
    table.desc.requested_capacity = actual_capacity;
    table.layout_id = layout_id;
    table.pool = pool;
    table.set = set;
    table.capacity = actual_capacity;
    table.live_count = actual_capacity > 0U ? 1U : 0U;
    table.generations.resize(actual_capacity, 1U);
    table.initialized.resize(actual_capacity, 0U);
    table.pending_free.resize(actual_capacity, 0U);
    table.queued_write_revisions.resize(actual_capacity, 0U);
    if (actual_capacity > 1U) {
        table.free_list.reserve(actual_capacity - 1U);
        for (uint32_t index = actual_capacity; index > 1U; --index) {
            table.free_list.push_back(index - 1U);
        }
    }

    if (actual_capacity > 0U && table.desc.HasPlaceholder()) {
        QueueBindlessImageInfoWrite(table, BindlessSlot{0U, table.generations[0U]},
                                    table.desc.placeholder_image_info);
    }

    bindless_tables.push_back(std::move(table));
    RefreshBindlessStats();
    return MakeTableId(static_cast<uint32_t>(bindless_tables.size() - 1U));
}

VkDescriptorSet DescriptorHost::GetBindlessDescriptorSet(BindlessTableId table_id_) const {
    return TableAt(table_id_).set;
}

VkDescriptorSetLayout DescriptorHost::GetBindlessLayout(BindlessTableId table_id_) const {
    return GetLayout(TableAt(table_id_).layout_id);
}

uint32_t DescriptorHost::GetBindlessCapacity(BindlessTableId table_id_) const {
    return TableAt(table_id_).capacity;
}

BindlessTableStats DescriptorHost::GetBindlessTableStats(BindlessTableId table_id_) const {
    const BindlessTable& table = TableAt(table_id_);
    return BindlessTableStats{
        .capacity = table.capacity,
        .live_count = table.live_count,
        .pending_write_count = static_cast<uint32_t>(table.pending_writes.size()),
        .deferred_free_count = static_cast<uint32_t>(table.deferred_frees.size()),
        .has_placeholder = table.desc.HasPlaceholder(),
    };
}

BindlessSlot DescriptorHost::AllocateBindlessSlot(BindlessTableId table_id_) {
    BindlessTable& table = TableAt(table_id_);
    if (table.free_list.empty()) {
        throw std::runtime_error("DescriptorHost::AllocateBindlessSlot exhausted bindless table capacity");
    }

    const uint32_t slot_index = table.free_list.back();
    table.free_list.pop_back();
    if (table.generations[slot_index] == 0U) {
        table.generations[slot_index] = 1U;
    }
    ++table.live_count;

    BindlessSlot slot{};
    slot.index = slot_index;
    slot.generation = table.generations[slot_index];

    if (table.desc.HasPlaceholder()) {
        QueueBindlessImageInfoWrite(table, slot, table.desc.placeholder_image_info);
    }

    RefreshBindlessStats();
    return slot;
}

void DescriptorHost::FreeBindlessSlotDeferred(BindlessTableId table_id_,
                                              BindlessSlot slot_,
                                              std::uint64_t retire_value_) {
    BindlessTable& table = TableAt(table_id_);
    RequireAliveBindlessSlot(table, slot_, "DescriptorHost::FreeBindlessSlotDeferred");
    if (slot_.index == 0U) {
        throw std::runtime_error("DescriptorHost::FreeBindlessSlotDeferred cannot free placeholder slot 0");
    }
    if (table.pending_free[slot_.index] != 0U) {
        throw std::runtime_error("DescriptorHost::FreeBindlessSlotDeferred slot is already pending recycle");
    }

    table.pending_free[slot_.index] = 1U;
    table.deferred_frees.push_back({
        .slot = slot_,
        .retire_value = retire_value_,
    });
    RefreshBindlessStats();
}

void DescriptorHost::QueueBindlessImageWrite(BindlessTableId table_id_,
                                             BindlessSlot slot_,
                                             VkImageView image_view_,
                                             VkImageLayout image_layout_) {
    BindlessTable& table = TableAt(table_id_);
    if (!DescriptorTypeSupportsImageInfo(table.desc.descriptor_type) ||
        table.desc.descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLER) {
        throw std::runtime_error(
            "DescriptorHost::QueueBindlessImageWrite requires sampled-image or combined-image-sampler table");
    }
    if (image_view_ == VK_NULL_HANDLE) {
        throw std::runtime_error("DescriptorHost::QueueBindlessImageWrite requires valid image_view");
    }

    VkDescriptorImageInfo image_info{};
    image_info.imageView = image_view_;
    image_info.imageLayout = image_layout_;
    if (table.desc.descriptor_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        image_info.sampler = table.desc.placeholder_image_info.sampler;
    }
    QueueBindlessImageInfoWrite(table, slot_, image_info);
    RefreshBindlessStats();
}

void DescriptorHost::QueueBindlessSamplerWrite(BindlessTableId table_id_,
                                               BindlessSlot slot_,
                                               VkSampler sampler_) {
    BindlessTable& table = TableAt(table_id_);
    if (table.desc.descriptor_type != VK_DESCRIPTOR_TYPE_SAMPLER) {
        throw std::runtime_error(
            "DescriptorHost::QueueBindlessSamplerWrite requires sampler bindless table");
    }
    if (sampler_ == VK_NULL_HANDLE) {
        throw std::runtime_error("DescriptorHost::QueueBindlessSamplerWrite requires valid sampler");
    }

    VkDescriptorImageInfo image_info{};
    image_info.sampler = sampler_;
    QueueBindlessImageInfoWrite(table, slot_, image_info);
    RefreshBindlessStats();
}

void DescriptorHost::QueueBindlessCombinedImageSamplerWrite(BindlessTableId table_id_,
                                                            BindlessSlot slot_,
                                                            VkSampler sampler_,
                                                            VkImageView image_view_,
                                                            VkImageLayout image_layout_) {
    BindlessTable& table = TableAt(table_id_);
    if (table.desc.descriptor_type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        throw std::runtime_error(
            "DescriptorHost::QueueBindlessCombinedImageSamplerWrite requires combined-image-sampler table");
    }
    if (sampler_ == VK_NULL_HANDLE || image_view_ == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "DescriptorHost::QueueBindlessCombinedImageSamplerWrite requires valid sampler and image_view");
    }

    VkDescriptorImageInfo image_info{};
    image_info.sampler = sampler_;
    image_info.imageView = image_view_;
    image_info.imageLayout = image_layout_;
    QueueBindlessImageInfoWrite(table, slot_, image_info);
    RefreshBindlessStats();
}

void DescriptorHost::QueueBindlessPlaceholderWrite(BindlessTableId table_id_,
                                                   BindlessSlot slot_) {
    BindlessTable& table = TableAt(table_id_);
    if (!table.desc.HasPlaceholder()) {
        throw std::runtime_error(
            "DescriptorHost::QueueBindlessPlaceholderWrite requires table placeholder configuration");
    }
    QueueBindlessImageInfoWrite(table, slot_, table.desc.placeholder_image_info);
    RefreshBindlessStats();
}

void DescriptorHost::FlushBindlessWrites(VulkanContext& context_,
                                         std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("DescriptorHost::FlushBindlessWrites called before Initialize");
    }
    if (context_.Device() == VK_NULL_HANDLE) {
        throw std::runtime_error("DescriptorHost::FlushBindlessWrites requires valid Vulkan device");
    }

    for (auto& table : bindless_tables) {
        if (!table.pending_writes.empty()) {
            BindlessUpdateScratch& scratch = bindless_update_scratch;
            scratch.writes.clear();
            scratch.image_infos.clear();
            scratch.writes.reserve(table.pending_writes.size());
            scratch.image_infos.reserve(table.pending_writes.size());

            for (const PendingBindlessWrite& pending_write : table.pending_writes) {
                if (pending_write.slot_index >= table.capacity) {
                    continue;
                }
                if (table.queued_write_revisions[pending_write.slot_index] != pending_write.revision) {
                    continue;
                }

                scratch.image_infos.push_back(pending_write.image_info);

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = table.set;
                write.dstBinding = 0U;
                write.dstArrayElement = pending_write.slot_index;
                write.descriptorCount = 1U;
                write.descriptorType = table.desc.descriptor_type;
                write.pImageInfo = &scratch.image_infos.back();
                scratch.writes.push_back(write);
                table.initialized[pending_write.slot_index] = 1U;
            }

            if (!scratch.writes.empty()) {
                vkUpdateDescriptorSets(context_.Device(),
                                       static_cast<uint32_t>(scratch.writes.size()),
                                       scratch.writes.data(),
                                       0U,
                                       nullptr);
                stats.bindless_descriptor_write_count +=
                    static_cast<std::uint64_t>(scratch.writes.size());
            }

            table.pending_writes.clear();
        }

        uint32_t write_index = 0U;
        for (uint32_t read_index = 0U; read_index < table.deferred_frees.size(); ++read_index) {
            const DeferredBindlessFree deferred = table.deferred_frees[read_index];
            if (deferred.slot.index >= table.capacity) {
                continue;
            }
            if (deferred.retire_value <= completed_submit_value_) {
                table.pending_free[deferred.slot.index] = 0U;
                IncrementGeneration(table.generations[deferred.slot.index]);
                table.free_list.push_back(deferred.slot.index);
                if (table.live_count > 0U) {
                    --table.live_count;
                }
                table.initialized[deferred.slot.index] = table.desc.HasPlaceholder() ? 1U : 0U;
                ++stats.bindless_slot_recycle_count;
                continue;
            }

            if (write_index != read_index) {
                table.deferred_frees[write_index] = deferred;
            }
            ++write_index;
        }
        table.deferred_frees.resize(write_index);
    }

    RefreshBindlessStats();
}

bool DescriptorHost::IsBindlessSlotAlive(BindlessTableId table_id_,
                                         BindlessSlot slot_) const noexcept {
    if (!table_id_.IsValid() || !slot_.IsValid()) {
        return false;
    }
    const uint32_t table_index = table_id_.value - 1U;
    if (table_index >= bindless_tables.size()) {
        return false;
    }
    const BindlessTable& table = bindless_tables[table_index];
    if (slot_.index >= table.capacity) {
        return false;
    }
    if (table.pending_free[slot_.index] != 0U) {
        return false;
    }
    return table.generations[slot_.index] == slot_.generation;
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

uint32_t DescriptorHost::TotalAllocatedSetCount() const noexcept {
    uint32_t count = 0U;
    for (const auto& arena : frame_arenas) {
        for (const auto& pool_page : arena.pools) {
            count += pool_page.allocated_sets;
        }
    }
    return count;
}

uint32_t DescriptorHost::FrameAllocatedSetCount(uint32_t frame_index_) const {
    const FramePoolArena& arena = ArenaAt(frame_index_);
    uint32_t count = 0U;
    for (const auto& pool_page : arena.pools) {
        count += pool_page.allocated_sets;
    }
    return count;
}

bool DescriptorHost::ValidationEnabled() const noexcept {
#if VR_ENABLE_DESCRIPTOR_VALIDATION
    return create_info_cache.enable_validation;
#else
    return false;
#endif
}

DescriptorValidationStats DescriptorHost::ValidationStats() const noexcept {
#if VR_ENABLE_DESCRIPTOR_VALIDATION
    DescriptorValidationStats stats = validation_stats;
    stats.tracked_set_count = static_cast<std::uint64_t>(descriptor_set_validation_nodes.size());
    return stats;
#else
    return {};
#endif
}

const DescriptorHostStats& DescriptorHost::Stats() const noexcept {
    return stats;
}

#if VR_ENABLE_DESCRIPTOR_VALIDATION
bool DescriptorHost::IsDescriptorSetAlive(VkDescriptorSet set_) const noexcept {
    if (!create_info_cache.enable_validation || set_ == VK_NULL_HANDLE) {
        return false;
    }
    const uint64_t descriptor_set_bits = DescriptorSetHandleBits(set_);
    const uint32_t lower_bound = LowerBoundDescriptorSetBits(descriptor_set_validation_nodes,
                                                              descriptor_set_bits);
    if (lower_bound >= descriptor_set_validation_nodes.size()) {
        return false;
    }
    return descriptor_set_validation_nodes[lower_bound].descriptor_set_bits == descriptor_set_bits;
}
#endif

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

uint32_t DescriptorHost::TableIdToIndex(uint32_t table_id_value_) {
    if (table_id_value_ == 0U) {
        throw std::runtime_error("DescriptorHost bindless table id must be non-zero");
    }
    return table_id_value_ - 1U;
}

BindlessTableId DescriptorHost::MakeTableId(uint32_t entry_index_) {
    BindlessTableId id{};
    id.value = entry_index_ + 1U;
    return id;
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

DescriptorHost::BindlessTable& DescriptorHost::TableAt(BindlessTableId table_id_) {
    if (!table_id_.IsValid()) {
        throw std::runtime_error("DescriptorHost bindless table id must be valid");
    }
    const uint32_t index = TableIdToIndex(table_id_.value);
    if (index >= bindless_tables.size()) {
        throw std::out_of_range("DescriptorHost bindless table id out of range");
    }
    return bindless_tables[index];
}

const DescriptorHost::BindlessTable& DescriptorHost::TableAt(BindlessTableId table_id_) const {
    if (!table_id_.IsValid()) {
        throw std::runtime_error("DescriptorHost bindless table id must be valid");
    }
    const uint32_t index = TableIdToIndex(table_id_.value);
    if (index >= bindless_tables.size()) {
        throw std::out_of_range("DescriptorHost bindless table id out of range");
    }
    return bindless_tables[index];
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

bool DescriptorHost::DescriptorTypeSupportsImageInfo(VkDescriptorType descriptor_type_) noexcept {
    switch (descriptor_type_) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return true;
    default:
        break;
    }
    return false;
}

bool DescriptorHost::IsPlaceholderImageInfoValid(
    VkDescriptorType descriptor_type_,
    const VkDescriptorImageInfo& image_info_) noexcept {
    switch (descriptor_type_) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        return image_info_.sampler != VK_NULL_HANDLE;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return image_info_.imageView != VK_NULL_HANDLE;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return image_info_.sampler != VK_NULL_HANDLE &&
               image_info_.imageView != VK_NULL_HANDLE;
    default:
        break;
    }
    return false;
}

void DescriptorHost::IncrementGeneration(uint32_t& generation_) noexcept {
    if (generation_ == std::numeric_limits<uint32_t>::max()) {
        generation_ = 1U;
    } else {
        ++generation_;
        if (generation_ == 0U) {
            generation_ = 1U;
        }
    }
}

void DescriptorHost::RequireBindlessCapsForTable(const VulkanContext& context_,
                                                 const BindlessTableDesc& table_desc_) {
    const auto& caps = context_.DescriptorIndexingCapsInfo();
    if (!caps.supported || !caps.enabled) {
        throw std::runtime_error(
            "DescriptorHost::CreateBindlessTable requires enabled descriptor indexing bindless feature set");
    }

    if (table_desc_.enable_update_after_bind) {
        const bool supports_update_after_bind =
            table_desc_.descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLER
                ? caps.sampler_update_after_bind
                : (table_desc_.descriptor_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                       ? (caps.sampled_image_update_after_bind &&
                          caps.sampler_update_after_bind)
                       : caps.sampled_image_update_after_bind);
        if (!supports_update_after_bind) {
            throw std::runtime_error(
                "DescriptorHost::CreateBindlessTable requested update-after-bind but the enabled device features do not support it for this descriptor type");
        }
    }

    switch (table_desc_.descriptor_type) {
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        if (!caps.sampled_image_array_non_uniform_indexing) {
            throw std::runtime_error(
                "DescriptorHost::CreateBindlessTable requires sampled-image non-uniform indexing support");
        }
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        if (!caps.sampler_array_non_uniform_indexing) {
            throw std::runtime_error(
                "DescriptorHost::CreateBindlessTable requires sampler-array non-uniform indexing support");
        }
        break;
    default:
        throw std::runtime_error(
            "DescriptorHost::CreateBindlessTable currently supports sampled-image, sampler, and combined-image-sampler descriptors only");
    }
}

void DescriptorHost::RequireAliveBindlessSlot(const BindlessTable& table_,
                                              BindlessSlot slot_,
                                              const char* action_) {
    if (!slot_.IsValid()) {
        throw std::runtime_error(std::string(action_) + " received invalid bindless slot");
    }
    if (slot_.index >= table_.capacity) {
        throw std::out_of_range(std::string(action_) + " bindless slot index out of range");
    }
    if (table_.pending_free[slot_.index] != 0U) {
        throw std::runtime_error(std::string(action_) + " bindless slot is pending recycle");
    }
    if (table_.generations[slot_.index] != slot_.generation) {
        throw std::runtime_error(std::string(action_) + " bindless slot generation mismatch");
    }
}

void DescriptorHost::QueueBindlessImageInfoWrite(BindlessTable& table_,
                                                 BindlessSlot slot_,
                                                 const VkDescriptorImageInfo& image_info_) {
    RequireAliveBindlessSlot(table_, slot_, "DescriptorHost::QueueBindlessImageInfoWrite");
    if (!DescriptorTypeSupportsImageInfo(table_.desc.descriptor_type)) {
        throw std::runtime_error(
            "DescriptorHost::QueueBindlessImageInfoWrite received unsupported bindless descriptor type");
    }
    if (!IsPlaceholderImageInfoValid(table_.desc.descriptor_type, image_info_)) {
        throw std::runtime_error(
            "DescriptorHost::QueueBindlessImageInfoWrite received invalid image/sampler payload for descriptor type");
    }

    ++table_.next_write_revision;
    table_.queued_write_revisions[slot_.index] = table_.next_write_revision;
    table_.pending_writes.push_back({
        .slot_index = slot_.index,
        .revision = table_.next_write_revision,
        .image_info = image_info_,
    });
}

void DescriptorHost::RefreshBindlessStats() noexcept {
    stats.bindless_table_count = static_cast<std::uint64_t>(bindless_tables.size());
    stats.bindless_pending_write_count = 0U;
    stats.bindless_live_slot_count = 0U;
    for (const auto& table : bindless_tables) {
        stats.bindless_pending_write_count +=
            static_cast<std::uint64_t>(table.pending_writes.size());
        stats.bindless_live_slot_count += static_cast<std::uint64_t>(table.live_count);
    }
}

#if VR_ENABLE_DESCRIPTOR_VALIDATION
uint64_t DescriptorHost::DescriptorSetHandleBits(VkDescriptorSet set_) noexcept {
    return HandleBits(set_);
}

uint32_t DescriptorHost::LowerBoundDescriptorSetBits(
    const DescriptorMcVector<DescriptorSetValidationNode>& nodes_,
    uint64_t descriptor_set_bits_) noexcept {
    uint32_t first = 0U;
    uint32_t count = static_cast<uint32_t>(nodes_.size());
    while (count > 0U) {
        const uint32_t step = count / 2U;
        const uint32_t it = first + step;
        if (nodes_[it].descriptor_set_bits < descriptor_set_bits_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

void DescriptorHost::ValidationOnInitialize() {
    descriptor_set_validation_nodes.clear();
    frame_validation_generations.clear();
    frame_validation_generations.resize(create_info_cache.frames_in_flight);
    for (auto& generation : frame_validation_generations) {
        generation = 1U;
    }
    validation_stats = {};
}

void DescriptorHost::ValidationOnShutdown() {
    descriptor_set_validation_nodes.clear();
    frame_validation_generations.clear();
    validation_stats = {};
}

void DescriptorHost::ValidationOnBeginFrame(uint32_t frame_index_) {
    if (!create_info_cache.enable_validation) {
        return;
    }
    if (frame_index_ >= frame_validation_generations.size()) {
        throw std::out_of_range("DescriptorHost::ValidationOnBeginFrame frame_index out of range");
    }

    uint64_t& generation = frame_validation_generations[frame_index_];
    if (generation == std::numeric_limits<uint64_t>::max()) {
        generation = 1U;
    } else {
        ++generation;
    }

    std::uint64_t invalidated_count = 0U;
    uint32_t write_index = 0U;
    for (uint32_t read_index = 0U; read_index < descriptor_set_validation_nodes.size(); ++read_index) {
        const DescriptorSetValidationNode node = descriptor_set_validation_nodes[read_index];
        if (node.frame_index == frame_index_) {
            ++invalidated_count;
            continue;
        }
        if (write_index != read_index) {
            descriptor_set_validation_nodes[write_index] = node;
        }
        ++write_index;
    }
    descriptor_set_validation_nodes.resize(write_index);
    validation_stats.begin_frame_invalidation_count += invalidated_count;
    validation_stats.tracked_set_count = static_cast<std::uint64_t>(descriptor_set_validation_nodes.size());
}

void DescriptorHost::ValidationTrackSet(uint32_t frame_index_,
                                        VkDescriptorSet set_) {
    if (!create_info_cache.enable_validation || set_ == VK_NULL_HANDLE) {
        return;
    }
    if (frame_index_ >= frame_validation_generations.size()) {
        throw std::out_of_range("DescriptorHost::ValidationTrackSet frame_index out of range");
    }

    const uint64_t descriptor_set_bits = DescriptorSetHandleBits(set_);
    const uint32_t insert_index = LowerBoundDescriptorSetBits(descriptor_set_validation_nodes,
                                                               descriptor_set_bits);

    if (insert_index < descriptor_set_validation_nodes.size() &&
        descriptor_set_validation_nodes[insert_index].descriptor_set_bits == descriptor_set_bits) {
        auto& node = descriptor_set_validation_nodes[insert_index];
        node.frame_index = frame_index_;
        node.frame_generation = frame_validation_generations[frame_index_];
        return;
    }

    DescriptorSetValidationNode node{};
    node.descriptor_set_bits = descriptor_set_bits;
    node.frame_index = frame_index_;
    node.frame_generation = frame_validation_generations[frame_index_];

    descriptor_set_validation_nodes.push_back(node);
    for (uint32_t move_index = static_cast<uint32_t>(descriptor_set_validation_nodes.size() - 1U);
         move_index > insert_index;
         --move_index) {
        descriptor_set_validation_nodes[move_index] = descriptor_set_validation_nodes[move_index - 1U];
    }
    descriptor_set_validation_nodes[insert_index] = node;
    validation_stats.tracked_set_count = static_cast<std::uint64_t>(descriptor_set_validation_nodes.size());
}

void DescriptorHost::ValidationTrackSetArray(uint32_t frame_index_,
                                             const VkDescriptorSet* sets_,
                                             uint32_t set_count_) {
    if (!create_info_cache.enable_validation || sets_ == nullptr || set_count_ == 0U) {
        return;
    }
    for (uint32_t i = 0U; i < set_count_; ++i) {
        ValidationTrackSet(frame_index_, sets_[i]);
    }
}

void DescriptorHost::ValidationRequireSetAlive(VkDescriptorSet set_) {
    ++validation_stats.check_count;
    if (set_ == VK_NULL_HANDLE) {
        ++validation_stats.failure_count;
        throw std::runtime_error("DescriptorHost::ValidationRequireSetAlive received null descriptor set");
    }
    const uint64_t descriptor_set_bits = DescriptorSetHandleBits(set_);
    const uint32_t lower_bound = LowerBoundDescriptorSetBits(descriptor_set_validation_nodes,
                                                              descriptor_set_bits);
    if (lower_bound >= descriptor_set_validation_nodes.size() ||
        descriptor_set_validation_nodes[lower_bound].descriptor_set_bits != descriptor_set_bits) {
        ++validation_stats.failure_count;
        std::ostringstream oss;
        oss << "DescriptorHost validation: descriptor set is stale or unknown (0x"
            << std::hex << descriptor_set_bits << std::dec << "). "
            << "Likely caused by per-frame descriptor pool reset and stale cache reuse.";
        throw std::runtime_error(oss.str());
    }
}
#endif

} // namespace vr::render
