#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/shadow/shadow_atlas_host.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace vr::render {

template<typename T>
using ShadowAtlasBindingCoordinatorMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ShadowAtlasBindingResolveInput final {
    const shadow::ShadowAtlasHost* atlas_host = nullptr;
    std::uint32_t namespace_id = 0U;
    std::uint32_t fallback_namespace_id = 1U;
    std::uint8_t allow_namespace_fallback = 1U;
    VkSampler primary_sampler = VK_NULL_HANDLE;
    VkImageView fallback_view = VK_NULL_HANDLE;
    VkSampler fallback_sampler = VK_NULL_HANDLE;
    VkImageLayout fallback_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

struct ShadowAtlasBindingResolveResult final {
    VkImageView image_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::uint32_t atlas_host_revision = 0U;
    std::uint32_t atlas_namespace_id = 0U;
    std::uint32_t atlas_record_revision = 0U;
    std::uint64_t binding_signature = 0U;
    bool valid = false;
    bool cache_reused = false;
};

struct ShadowAtlasBindingCoordinatorStats final {
    std::uint64_t resolve_call_count = 0U;
    std::uint64_t cache_reuse_hit_count = 0U;
    std::uint64_t cache_build_count = 0U;
    std::uint64_t cache_evict_count = 0U;
};

class ShadowAtlasBindingCoordinator final {
public:
    static constexpr std::uint32_t default_max_cache_entries = 64U;

    ShadowAtlasBindingCoordinator() = default;
    ~ShadowAtlasBindingCoordinator() = default;

    ShadowAtlasBindingCoordinator(const ShadowAtlasBindingCoordinator&) = delete;
    ShadowAtlasBindingCoordinator& operator=(const ShadowAtlasBindingCoordinator&) = delete;
    ShadowAtlasBindingCoordinator(ShadowAtlasBindingCoordinator&&) = delete;
    ShadowAtlasBindingCoordinator& operator=(ShadowAtlasBindingCoordinator&&) = delete;

    void Reset() noexcept {
        entries.clear();
        access_stamp = 1U;
        stats = {};
        last_cache_hit_index = invalid_cache_index;
    }

    void Reserve(std::uint32_t cache_entry_count_) {
        const std::size_t reserve_count = static_cast<std::size_t>(cache_entry_count_);
        if (entries.capacity() < reserve_count) {
            entries.reserve(reserve_count);
        }
    }

    void SetMaxCacheEntries(std::uint32_t max_cache_entries_) noexcept {
        max_cache_entries = std::max<std::uint32_t>(1U, max_cache_entries_);
        TrimToMaxCacheEntries();
    }

    [[nodiscard]] std::uint32_t MaxCacheEntries() const noexcept {
        return max_cache_entries;
    }

    [[nodiscard]] ShadowAtlasBindingResolveResult Resolve(
        const ShadowAtlasBindingResolveInput& resolve_input_) {
        ++stats.resolve_call_count;

        const CacheKey cache_key = BuildKey(resolve_input_);
        CacheEntry* cache_entry = FindEntry(cache_key);
        if (cache_entry != nullptr) {
            cache_entry->last_access_stamp = NextAccessStamp();
            ShadowAtlasBindingResolveResult cached_result = cache_entry->result;
            cached_result.cache_reused = true;
            ++stats.cache_reuse_hit_count;
            return cached_result;
        }

        ShadowAtlasBindingResolveResult built_result = BuildResult(resolve_input_, cache_key);
        built_result.cache_reused = false;
        StoreEntry(cache_key, built_result);
        ++stats.cache_build_count;
        return built_result;
    }

    [[nodiscard]] const ShadowAtlasBindingCoordinatorStats& Stats() const noexcept {
        return stats;
    }

private:
    struct CacheKey final {
        const shadow::ShadowAtlasHost* atlas_host = nullptr;
        std::uint32_t atlas_host_revision = 0U;
        std::uint32_t namespace_id = 0U;
        std::uint32_t fallback_namespace_id = 0U;
        std::uint8_t allow_namespace_fallback = 0U;
        VkSampler primary_sampler = VK_NULL_HANDLE;
        VkImageView fallback_view = VK_NULL_HANDLE;
        VkSampler fallback_sampler = VK_NULL_HANDLE;
        VkImageLayout fallback_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct CacheEntry final {
        CacheKey key{};
        ShadowAtlasBindingResolveResult result{};
        std::uint32_t last_access_stamp = 0U;
    };

    static constexpr std::size_t invalid_cache_index = (std::numeric_limits<std::size_t>::max)();
    static constexpr std::uint64_t k_hash_offset_basis = 14695981039346656037ULL;
    static constexpr std::uint64_t k_hash_prime = 1099511628211ULL;

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= k_hash_prime;
    }

    static bool IsKeyEqual(const CacheKey& lhs_, const CacheKey& rhs_) noexcept {
        return lhs_.atlas_host == rhs_.atlas_host &&
               lhs_.atlas_host_revision == rhs_.atlas_host_revision &&
               lhs_.namespace_id == rhs_.namespace_id &&
               lhs_.fallback_namespace_id == rhs_.fallback_namespace_id &&
               lhs_.allow_namespace_fallback == rhs_.allow_namespace_fallback &&
               lhs_.primary_sampler == rhs_.primary_sampler &&
               lhs_.fallback_view == rhs_.fallback_view &&
               lhs_.fallback_sampler == rhs_.fallback_sampler &&
               lhs_.fallback_layout == rhs_.fallback_layout;
    }

    static CacheKey BuildKey(const ShadowAtlasBindingResolveInput& resolve_input_) noexcept {
        CacheKey key{};
        key.atlas_host = resolve_input_.atlas_host;
        key.atlas_host_revision = resolve_input_.atlas_host != nullptr
            ? resolve_input_.atlas_host->Stats().revision
            : 0U;
        key.namespace_id = resolve_input_.namespace_id;
        key.fallback_namespace_id = resolve_input_.fallback_namespace_id;
        key.allow_namespace_fallback = resolve_input_.allow_namespace_fallback != 0U ? 1U : 0U;
        key.primary_sampler = resolve_input_.primary_sampler;
        key.fallback_view = resolve_input_.fallback_view;
        key.fallback_sampler = resolve_input_.fallback_sampler;
        key.fallback_layout = resolve_input_.fallback_layout;
        return key;
    }

    ShadowAtlasBindingResolveResult BuildResult(const ShadowAtlasBindingResolveInput& resolve_input_,
                                                const CacheKey& cache_key_) const noexcept {
        ShadowAtlasBindingResolveResult result{};
        result.atlas_host_revision = cache_key_.atlas_host_revision;

        const shadow::ShadowAtlasHost* atlas_host = resolve_input_.atlas_host;
        if (atlas_host != nullptr && resolve_input_.primary_sampler != VK_NULL_HANDLE) {
            const shadow::ShadowAtlasHost::AtlasRecord* atlas_record = atlas_host->FindAtlas(resolve_input_.namespace_id);
            if (atlas_record == nullptr &&
                resolve_input_.allow_namespace_fallback != 0U &&
                resolve_input_.fallback_namespace_id != 0U &&
                resolve_input_.fallback_namespace_id != resolve_input_.namespace_id) {
                atlas_record = atlas_host->FindAtlas(resolve_input_.fallback_namespace_id);
            }
            if (atlas_record != nullptr && atlas_record->array_view != VK_NULL_HANDLE) {
                result.image_view = atlas_record->array_view;
                result.sampler = resolve_input_.primary_sampler;
                result.image_layout = atlas_record->current_layout == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
                    : atlas_record->current_layout;
                result.atlas_namespace_id = atlas_record->namespace_id;
                result.atlas_record_revision = atlas_record->revision;
                result.valid = true;
            }
        }

        if (!result.valid &&
            resolve_input_.fallback_view != VK_NULL_HANDLE &&
            resolve_input_.fallback_sampler != VK_NULL_HANDLE) {
            result.image_view = resolve_input_.fallback_view;
            result.sampler = resolve_input_.fallback_sampler;
            result.image_layout = resolve_input_.fallback_layout == VK_IMAGE_LAYOUT_UNDEFINED
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : resolve_input_.fallback_layout;
            result.atlas_namespace_id = 0U;
            result.atlas_record_revision = 0U;
            result.valid = true;
        }

        std::uint64_t binding_signature = k_hash_offset_basis;
        HashCombine(binding_signature, static_cast<std::uint64_t>(cache_key_.atlas_host_revision));
        HashCombine(binding_signature, static_cast<std::uint64_t>(result.atlas_namespace_id));
        HashCombine(binding_signature, static_cast<std::uint64_t>(result.atlas_record_revision));
        HashCombine(binding_signature, static_cast<std::uint64_t>(result.image_layout));
        HashCombine(binding_signature, static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(result.image_view)));
        HashCombine(binding_signature, static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(result.sampler)));
        HashCombine(binding_signature, static_cast<std::uint64_t>(result.valid ? 1U : 0U));
        result.binding_signature = binding_signature;
        return result;
    }

    CacheEntry* FindEntry(const CacheKey& cache_key_) noexcept {
        if (last_cache_hit_index < entries.size() &&
            IsKeyEqual(entries[last_cache_hit_index].key, cache_key_)) {
            return &entries[last_cache_hit_index];
        }

        for (std::size_t i = 0U; i < entries.size(); ++i) {
            if (!IsKeyEqual(entries[i].key, cache_key_)) {
                continue;
            }
            last_cache_hit_index = i;
            return &entries[i];
        }
        return nullptr;
    }

    void StoreEntry(const CacheKey& cache_key_,
                    const ShadowAtlasBindingResolveResult& result_) {
        if (entries.size() >= max_cache_entries) {
            EvictOneOldestEntry();
        }

        CacheEntry entry{};
        entry.key = cache_key_;
        entry.result = result_;
        entry.last_access_stamp = NextAccessStamp();
        entries.push_back(entry);
        last_cache_hit_index = entries.empty() ? invalid_cache_index : entries.size() - 1U;
    }

    void EvictOneOldestEntry() noexcept {
        if (entries.empty()) {
            return;
        }

        std::size_t oldest_index = 0U;
        std::uint32_t oldest_stamp = entries[0U].last_access_stamp;
        for (std::size_t i = 1U; i < entries.size(); ++i) {
            if (entries[i].last_access_stamp >= oldest_stamp) {
                continue;
            }
            oldest_stamp = entries[i].last_access_stamp;
            oldest_index = i;
        }

        if (oldest_index != entries.size() - 1U) {
            entries[oldest_index] = entries.back();
            if (last_cache_hit_index == entries.size() - 1U) {
                last_cache_hit_index = oldest_index;
            }
        }
        entries.pop_back();
        ++stats.cache_evict_count;
        if (entries.empty()) {
            last_cache_hit_index = invalid_cache_index;
        } else if (last_cache_hit_index >= entries.size()) {
            last_cache_hit_index = entries.size() - 1U;
        }
    }

    void TrimToMaxCacheEntries() noexcept {
        while (entries.size() > max_cache_entries) {
            EvictOneOldestEntry();
        }
    }

    std::uint32_t NextAccessStamp() noexcept {
        if (access_stamp == (std::numeric_limits<std::uint32_t>::max)()) {
            access_stamp = 1U;
            for (std::size_t i = 0U; i < entries.size(); ++i) {
                entries[i].last_access_stamp = static_cast<std::uint32_t>(i + 1U);
            }
            if (!entries.empty()) {
                access_stamp = static_cast<std::uint32_t>(entries.size() + 1U);
            }
            return access_stamp;
        }
        return access_stamp++;
    }

private:
    ShadowAtlasBindingCoordinatorMcVector<CacheEntry> entries{};
    std::size_t last_cache_hit_index = invalid_cache_index;
    std::uint32_t access_stamp = 1U;
    std::uint32_t max_cache_entries = default_max_cache_entries;
    ShadowAtlasBindingCoordinatorStats stats{};
};

} // namespace vr::render
