#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>

namespace vr::resource {

template<typename T>
using SamplerMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SamplerDesc {
    VkSamplerCreateFlags flags = 0U;
    VkFilter mag_filter = VK_FILTER_LINEAR;
    VkFilter min_filter = VK_FILTER_LINEAR;
    VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float mip_lod_bias = 0.0F;
    bool anisotropy_enable = false;
    float max_anisotropy = 1.0F;
    bool compare_enable = false;
    VkCompareOp compare_op = VK_COMPARE_OP_ALWAYS;
    float min_lod = 0.0F;
    float max_lod = VK_LOD_CLAMP_NONE;
    VkBorderColor border_color = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    bool unnormalized_coordinates = false;
};

struct SamplerHostStats {
    uint32_t sampler_count = 0U;
    uint32_t cache_hits = 0U;
    uint32_t cache_misses = 0U;
};

struct SamplerHostCreateInfo {
    uint32_t reserve_sampler_count = 128U;
};

struct SamplerId {
    uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

class SamplerHost final {
public:
    SamplerHost() = default;
    ~SamplerHost() = default;

    SamplerHost(const SamplerHost&) = delete;
    SamplerHost& operator=(const SamplerHost&) = delete;

    SamplerHost(SamplerHost&&) = delete;
    SamplerHost& operator=(SamplerHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const SamplerHostCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    [[nodiscard]] SamplerId RegisterSampler(VulkanContext& context_,
                                            const SamplerDesc& desc_);

    [[nodiscard]] VkSampler AcquireSampler(VulkanContext& context_,
                                           const SamplerDesc& desc_);

    [[nodiscard]] VkSampler GetSampler(SamplerId sampler_id_) const;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const SamplerHostStats& Stats() const noexcept;

private:
    struct SamplerEntry {
        uint64_t hash = 0U;
        SamplerDesc desc{};
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct HashLookupNode {
        uint64_t hash = 0U;
        uint32_t entry_index = 0U;
    };

    static uint64_t HashDesc(const SamplerDesc& desc_) noexcept;
    static bool EqualDesc(const SamplerDesc& lhs_, const SamplerDesc& rhs_) noexcept;
    static void HashCombine(uint64_t& hash_, uint64_t value_) noexcept;
    [[nodiscard]] static uint32_t IdToIndex(uint32_t id_value_);
    [[nodiscard]] static SamplerId MakeSamplerId(uint32_t entry_index_);

private:
    SamplerMcVector<SamplerEntry> entries{};
    SamplerMcVector<HashLookupNode> lookup{};
    SamplerHostCreateInfo create_info_cache{};
    SamplerHostStats stats{};
    bool initialized = false;
};

} // namespace vr::resource
