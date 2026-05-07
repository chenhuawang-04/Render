#include "vr/resource/sampler_host.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vr::resource {

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
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        default: return "VK_ERROR_UNKNOWN";
    }
}

void ThrowVk(const char* stage_, VkResult result_) {
    std::ostringstream oss;
    oss << stage_ << " failed: " << VkResultName(result_) << " (" << static_cast<int>(result_) << ")";
    throw std::runtime_error(oss.str());
}

void CheckVk(const char* stage_, VkResult result_) {
    if (result_ != VK_SUCCESS) {
        ThrowVk(stage_, result_);
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

void SamplerHost::Initialize(VulkanContext& context_,
                             const SamplerHostCreateInfo& create_info_) {
    if (!context_.IsDeviceInitialized()) {
        throw std::runtime_error("SamplerHost::Initialize requires initialized Vulkan device");
    }
    Shutdown(context_);
    create_info_cache = create_info_;
    if (create_info_cache.reserve_sampler_count > 0U) {
        entries.reserve(create_info_cache.reserve_sampler_count);
        lookup.reserve(create_info_cache.reserve_sampler_count);
    }
    initialized = true;
}

void SamplerHost::Shutdown(VulkanContext& context_) {
    const VkDevice device = context_.Device();
    if (device != VK_NULL_HANDLE) {
        for (auto& entry : entries) {
            if (entry.sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, entry.sampler, nullptr);
                entry.sampler = VK_NULL_HANDLE;
            }
        }
    }
    entries.clear();
    lookup.clear();
    create_info_cache = {};
    stats = {};
    initialized = false;
}

SamplerId SamplerHost::RegisterSampler(VulkanContext& context_,
                                       const SamplerDesc& desc_) {
    if (!initialized) {
        throw std::runtime_error("SamplerHost::RegisterSampler called before Initialize");
    }

    SamplerDesc normalized = desc_;
    normalized.max_anisotropy = std::max(1.0F, normalized.max_anisotropy);
    if (normalized.unnormalized_coordinates) {
        normalized.min_lod = 0.0F;
        normalized.max_lod = 0.0F;
        normalized.mip_lod_bias = 0.0F;
        normalized.anisotropy_enable = false;
    }

    if (normalized.anisotropy_enable) {
        if (context_.EnabledFeatures().samplerAnisotropy != VK_TRUE) {
            throw std::runtime_error(
                "SamplerHost::RegisterSampler anisotropy enabled but device feature samplerAnisotropy was not enabled");
        }

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(context_.PhysicalDevice(), &props);
        normalized.max_anisotropy = std::min(normalized.max_anisotropy, props.limits.maxSamplerAnisotropy);
    }

    const uint64_t hash = HashDesc(normalized);
    const uint32_t existing_index = FindEntryIndex(
        lookup,
        entries,
        hash,
        normalized,
        [](const SamplerEntry& entry_, const SamplerDesc& desc_) noexcept {
            return EqualDesc(entry_.desc, desc_);
        });
    if (existing_index != std::numeric_limits<uint32_t>::max()) {
        ++stats.cache_hits;
        return MakeSamplerId(existing_index);
    }

    VkSamplerCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    create_info.flags = normalized.flags;
    create_info.magFilter = normalized.mag_filter;
    create_info.minFilter = normalized.min_filter;
    create_info.mipmapMode = normalized.mipmap_mode;
    create_info.addressModeU = normalized.address_mode_u;
    create_info.addressModeV = normalized.address_mode_v;
    create_info.addressModeW = normalized.address_mode_w;
    create_info.mipLodBias = normalized.mip_lod_bias;
    create_info.anisotropyEnable = normalized.anisotropy_enable ? VK_TRUE : VK_FALSE;
    create_info.maxAnisotropy = normalized.max_anisotropy;
    create_info.compareEnable = normalized.compare_enable ? VK_TRUE : VK_FALSE;
    create_info.compareOp = normalized.compare_op;
    create_info.minLod = normalized.min_lod;
    create_info.maxLod = normalized.max_lod;
    create_info.borderColor = normalized.border_color;
    create_info.unnormalizedCoordinates = normalized.unnormalized_coordinates ? VK_TRUE : VK_FALSE;

    VkSampler sampler = VK_NULL_HANDLE;
    CheckVk("vkCreateSampler",
            vkCreateSampler(context_.Device(), &create_info, nullptr, &sampler));

    if (entries.size() >= std::numeric_limits<uint32_t>::max()) {
        vkDestroySampler(context_.Device(), sampler, nullptr);
        throw std::runtime_error("SamplerHost sampler registry overflow");
    }

    entries.push_back({
        .hash = hash,
        .desc = normalized,
        .sampler = sampler
    });
    const uint32_t new_index = static_cast<uint32_t>(entries.size() - 1U);
    InsertLookupNode(lookup, hash, new_index);
    ++stats.cache_misses;
    stats.sampler_count = static_cast<uint32_t>(entries.size());
    return MakeSamplerId(new_index);
}

VkSampler SamplerHost::AcquireSampler(VulkanContext& context_,
                                      const SamplerDesc& desc_) {
    const SamplerId sampler_id = RegisterSampler(context_, desc_);
    return GetSampler(sampler_id);
}

VkSampler SamplerHost::GetSampler(SamplerId sampler_id_) const {
    if (!sampler_id_.IsValid()) {
        throw std::runtime_error("SamplerHost::GetSampler received invalid sampler id");
    }
    const uint32_t index = IdToIndex(sampler_id_.value);
    if (index >= entries.size()) {
        throw std::out_of_range("SamplerHost::GetSampler id out of range");
    }
    return entries[index].sampler;
}

bool SamplerHost::IsInitialized() const noexcept {
    return initialized;
}

const SamplerHostStats& SamplerHost::Stats() const noexcept {
    return stats;
}

uint32_t SamplerHost::IdToIndex(uint32_t id_value_) {
    if (id_value_ == 0U) {
        throw std::runtime_error("SamplerHost sampler id must be non-zero");
    }
    return id_value_ - 1U;
}

SamplerId SamplerHost::MakeSamplerId(uint32_t entry_index_) {
    SamplerId id{};
    id.value = entry_index_ + 1U;
    return id;
}

uint64_t SamplerHost::HashDesc(const SamplerDesc& desc_) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;
    HashCombine(hash, static_cast<uint64_t>(desc_.flags));
    HashCombine(hash, static_cast<uint64_t>(desc_.mag_filter));
    HashCombine(hash, static_cast<uint64_t>(desc_.min_filter));
    HashCombine(hash, static_cast<uint64_t>(desc_.mipmap_mode));
    HashCombine(hash, static_cast<uint64_t>(desc_.address_mode_u));
    HashCombine(hash, static_cast<uint64_t>(desc_.address_mode_v));
    HashCombine(hash, static_cast<uint64_t>(desc_.address_mode_w));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(desc_.mip_lod_bias)));
    HashCombine(hash, static_cast<uint64_t>(desc_.anisotropy_enable));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(desc_.max_anisotropy)));
    HashCombine(hash, static_cast<uint64_t>(desc_.compare_enable));
    HashCombine(hash, static_cast<uint64_t>(desc_.compare_op));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(desc_.min_lod)));
    HashCombine(hash, static_cast<uint64_t>(std::bit_cast<uint32_t>(desc_.max_lod)));
    HashCombine(hash, static_cast<uint64_t>(desc_.border_color));
    HashCombine(hash, static_cast<uint64_t>(desc_.unnormalized_coordinates));
    return hash;
}

bool SamplerHost::EqualDesc(const SamplerDesc& lhs_, const SamplerDesc& rhs_) noexcept {
    return lhs_.flags == rhs_.flags &&
           lhs_.mag_filter == rhs_.mag_filter &&
           lhs_.min_filter == rhs_.min_filter &&
           lhs_.mipmap_mode == rhs_.mipmap_mode &&
           lhs_.address_mode_u == rhs_.address_mode_u &&
           lhs_.address_mode_v == rhs_.address_mode_v &&
           lhs_.address_mode_w == rhs_.address_mode_w &&
           lhs_.mip_lod_bias == rhs_.mip_lod_bias &&
           lhs_.anisotropy_enable == rhs_.anisotropy_enable &&
           lhs_.max_anisotropy == rhs_.max_anisotropy &&
           lhs_.compare_enable == rhs_.compare_enable &&
           lhs_.compare_op == rhs_.compare_op &&
           lhs_.min_lod == rhs_.min_lod &&
           lhs_.max_lod == rhs_.max_lod &&
           lhs_.border_color == rhs_.border_color &&
           lhs_.unnormalized_coordinates == rhs_.unnormalized_coordinates;
}

void SamplerHost::HashCombine(uint64_t& hash_, uint64_t value_) noexcept {
    hash_ ^= value_ + 0x9e3779b97f4a7c15ULL + (hash_ << 6U) + (hash_ >> 2U);
}

} // namespace vr::resource
