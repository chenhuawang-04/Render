module;
// Global module fragment
#include "vr/detail/vr_module_fwd.hpp"
#include "Center/Memory/Adaptor/VulkanBuddyAdaptor.hpp"
#include "Center/Memory/Provider/VulkanNativeBackend.hpp"
#include "Center/Memory/Vulkan/Types.hpp"
#include <cstdint>
#include <optional>

export module vr.resource;
import vr.types;
import vr.context;

export {
namespace vr::resource {

// Forward declaration
class GpuMemoryHost;

// --- gpu_memory_host.hpp ----------------------------------------------------

struct GpuMemoryHostCreateInfo {
    bool enable_dedicated_allocation = true;
    Center::Memory::Vulkan::AllocationPolicy allocation_policy =
        Center::Memory::Vulkan::AllocationPolicy::throughput_first();
};

class GpuMemoryHost final {
public:
    GpuMemoryHost() = default;
    ~GpuMemoryHost() = default;

    GpuMemoryHost(const GpuMemoryHost&) = delete;
    GpuMemoryHost& operator=(const GpuMemoryHost&) = delete;

    GpuMemoryHost(GpuMemoryHost&&) = delete;
    GpuMemoryHost& operator=(GpuMemoryHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const GpuMemoryHostCreateInfo& create_info_ = {});

    void Shutdown();

    [[nodiscard]] bool IsInitialized() const noexcept;

    [[nodiscard]] Center::Memory::Vulkan::Slice AllocateAndBindBuffer(
        VkBuffer buffer_,
        const VkMemoryRequirements& requirements_,
        VkMemoryPropertyFlags required_properties_,
        VkMemoryPropertyFlags preferred_properties_,
        bool persistent_map_,
        Center::Memory::Vulkan::LifetimeHint lifetime_hint_ = Center::Memory::Vulkan::LifetimeHint::long_lived,
        Center::Memory::Vulkan::HostAccess host_access_ = Center::Memory::Vulkan::HostAccess::none,
        bool dedicated_required_ = false,
        bool dedicated_preferred_ = false);

    [[nodiscard]] Center::Memory::Vulkan::Slice AllocateAndBindImage(
        VkImage image_,
        const VkMemoryRequirements& requirements_,
        VkImageTiling tiling_,
        VkMemoryPropertyFlags required_properties_,
        VkMemoryPropertyFlags preferred_properties_,
        bool persistent_map_,
        Center::Memory::Vulkan::LifetimeHint lifetime_hint_ = Center::Memory::Vulkan::LifetimeHint::long_lived,
        Center::Memory::Vulkan::HostAccess host_access_ = Center::Memory::Vulkan::HostAccess::none,
        bool dedicated_required_ = false,
        bool dedicated_preferred_ = true);

    void Deallocate(const Center::Memory::Vulkan::Slice& slice_) noexcept;

    [[nodiscard]] bool FlushSlice(const Center::Memory::Vulkan::Slice& slice_,
                                  VkDeviceSize offset_ = 0U,
                                  VkDeviceSize size_ = VK_WHOLE_SIZE) noexcept;

    [[nodiscard]] bool InvalidateSlice(const Center::Memory::Vulkan::Slice& slice_,
                                       VkDeviceSize offset_ = 0U,
                                       VkDeviceSize size_ = VK_WHOLE_SIZE) noexcept;

    void Trim() noexcept;

    [[nodiscard]] VkMemoryPropertyFlags QueryMemoryProperties(uint32_t memory_type_index_) const noexcept;

private:
    [[nodiscard]] static Center::Memory::Vulkan::AllocationKind ToAllocationKind(
        VkImageTiling tiling_) noexcept;

    [[nodiscard]] static std::size_t ToSizeTChecked(VkDeviceSize value_,
                                                    const char* stage_);

    [[nodiscard]] static bool NormalizeMappedRange(
        const Center::Memory::Vulkan::Slice& slice_,
        VkDeviceSize offset_,
        VkDeviceSize size_,
        Center::Memory::Vulkan::MappedRange& out_range_) noexcept;

private:
    using NativeProvider = Center::Memory::VulkanNativeProvider;
    using AllocationAdaptor = Center::Memory::VulkanBuddyAdaptor<NativeProvider, (64u * 1024u * 1024u), 256u>;

    std::optional<AllocationAdaptor> allocator{};
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkDeviceSize non_coherent_atom_size = 1U;
    bool initialized = false;
};

// --- buffer_host.hpp --------------------------------------------------------

struct BufferCreateInfo {
    VkDeviceSize size = 0U;
    VkBufferUsageFlags usage = 0U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferCreateFlags flags = 0U;
    VkSharingMode sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    McVector<uint32_t> queue_family_indices{};
    bool persistently_mapped = false;
};

struct BufferResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    Center::Memory::Vulkan::Slice allocation_slice{};
    GpuMemoryHost* memory_host = nullptr;
    VkDeviceSize size = 0U;
    VkBufferUsageFlags usage = 0U;
    VkMemoryPropertyFlags memory_properties = 0U;
    uint32_t memory_type_index = 0U;
    VkDeviceSize non_coherent_atom_size = 1U;
    void* mapped_ptr = nullptr;
};

class BufferHost final {
public:
    BufferHost() = delete;

    [[nodiscard]] static BufferResource CreateBuffer(VulkanContext& context_,
                                                     const BufferCreateInfo& create_info_,
                                                     GpuMemoryHost& gpu_memory_host_);

    static void DestroyBuffer(VulkanContext& context_,
                              BufferResource& resource_);

    [[nodiscard]] static void* Map(VulkanContext& context_,
                                   BufferResource& resource_,
                                   VkDeviceSize offset_ = 0U,
                                   VkDeviceSize size_ = VK_WHOLE_SIZE);

    static void Unmap(VulkanContext& context_,
                      BufferResource& resource_);

    static void Flush(VulkanContext& context_,
                      const BufferResource& resource_,
                      VkDeviceSize offset_ = 0U,
                      VkDeviceSize size_ = VK_WHOLE_SIZE);

    static void Invalidate(VulkanContext& context_,
                           const BufferResource& resource_,
                           VkDeviceSize offset_ = 0U,
                           VkDeviceSize size_ = VK_WHOLE_SIZE);

    [[nodiscard]] static bool IsHostVisible(const BufferResource& resource_) noexcept;
    [[nodiscard]] static bool IsHostCoherent(const BufferResource& resource_) noexcept;
};

// --- image_host.hpp ----------------------------------------------------------

struct ImageCreateInfo {
    VkImageCreateFlags flags = 0U;
    VkImageType image_type = VK_IMAGE_TYPE_2D;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{1U, 1U, 1U};
    uint32_t mip_levels = 1U;
    uint32_t array_layers = 1U;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageUsageFlags usage = 0U;
    VkSharingMode sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    McVector<uint32_t> queue_family_indices{};
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    bool create_default_view = false;
    VkImageViewCreateFlags default_view_flags = 0U;
    VkImageViewType default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    VkImageAspectFlags default_view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t default_base_mip_level = 0U;
    uint32_t default_level_count = VK_REMAINING_MIP_LEVELS;
    uint32_t default_base_array_layer = 0U;
    uint32_t default_layer_count = VK_REMAINING_ARRAY_LAYERS;
};

struct ImageResource {
    VkImage image = VK_NULL_HANDLE;
    Center::Memory::Vulkan::Slice allocation_slice{};
    GpuMemoryHost* memory_host = nullptr;
    VkImageView default_view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    uint32_t mip_levels = 1U;
    uint32_t array_layers = 1U;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = 0U;
    VkMemoryPropertyFlags memory_properties = 0U;
    uint32_t memory_type_index = 0U;
};

class ImageHost final {
public:
    ImageHost() = delete;

    [[nodiscard]] static ImageResource CreateImage(VulkanContext& context_,
                                                   const ImageCreateInfo& create_info_,
                                                   GpuMemoryHost& gpu_memory_host_);

    static void DestroyImage(VulkanContext& context_,
                             ImageResource& resource_);

    [[nodiscard]] static VkImageView CreateView(VulkanContext& context_,
                                                VkImage image_,
                                                const VkImageViewCreateInfo& create_info_);

    static void DestroyView(VulkanContext& context_,
                            VkImageView& view_);
};

// --- sampler_host.hpp --------------------------------------------------------

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
    McVector<SamplerEntry> entries{};
    McVector<HashLookupNode> lookup{};
    SamplerHostCreateInfo create_info_cache{};
    SamplerHostStats stats{};
    bool initialized = false;
};

} // namespace vr::resource
} // export
