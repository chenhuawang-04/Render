#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/render_target_desc.hpp"
#include "vr/render/render_pass_preset.hpp"
#include "vr/render/retire_bus.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/vulkan_context.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::render {

template<typename T>
using RenderTargetMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct RenderTargetHostCreateInfo final {
    std::uint32_t reserve_target_count = 64U;
    std::uint32_t reserve_free_index_count = 64U;
    std::uint32_t reserve_retired_target_count = 64U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
};

struct RenderTargetResolvedView final {
    RenderTargetHandle handle{};
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = 0U;
    VkImageAspectFlags aspect = 0U;
    RenderTargetColorEncoding color_encoding = RenderTargetColorEncoding::linear;
    RenderTargetLifetime lifetime = RenderTargetLifetime::persistent;
    RenderTargetOwnership ownership = RenderTargetOwnership::owned;
    RenderTargetStateKind state = RenderTargetStateKind::undefined;
    std::uint32_t resource_revision = 0U;
};

struct EnsureRenderTargetResult final {
    RenderTargetHandle handle{};
    bool created = false;
    bool recreated = false;
    bool revision_changed = false;
};

struct RenderTargetHostStats final {
    std::uint32_t target_count = 0U;
    std::uint32_t owned_target_count = 0U;
    std::uint32_t imported_target_count = 0U;
    std::uint32_t retired_target_count = 0U;
    std::uint32_t resource_revision = 0U;
};

struct RenderTargetStateInfo final {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
};

struct RenderTargetRenderingInfo final {
    std::array<VkRenderingAttachmentInfo, k_max_render_target_color_attachments> color_attachments{};
    VkRenderingAttachmentInfo depth_attachment{};
    VkRenderingAttachmentInfo stencil_attachment{};
    mutable VkRenderingInfo vk{};
    std::uint32_t color_attachment_count = 0U;
    bool has_depth_attachment = false;
    bool has_stencil_attachment = false;

    RenderTargetRenderingInfo() = default;

    RenderTargetRenderingInfo(const RenderTargetRenderingInfo& other_) noexcept
        : color_attachments(other_.color_attachments),
          depth_attachment(other_.depth_attachment),
          stencil_attachment(other_.stencil_attachment),
          vk(other_.vk),
          color_attachment_count(other_.color_attachment_count),
          has_depth_attachment(other_.has_depth_attachment),
          has_stencil_attachment(other_.has_stencil_attachment) {
        RefreshPointers();
    }

    RenderTargetRenderingInfo(RenderTargetRenderingInfo&& other_) noexcept
        : color_attachments(std::move(other_.color_attachments)),
          depth_attachment(other_.depth_attachment),
          stencil_attachment(other_.stencil_attachment),
          vk(other_.vk),
          color_attachment_count(other_.color_attachment_count),
          has_depth_attachment(other_.has_depth_attachment),
          has_stencil_attachment(other_.has_stencil_attachment) {
        RefreshPointers();
    }

    RenderTargetRenderingInfo& operator=(const RenderTargetRenderingInfo& other_) noexcept {
        if (this == &other_) {
            return *this;
        }
        color_attachments = other_.color_attachments;
        depth_attachment = other_.depth_attachment;
        stencil_attachment = other_.stencil_attachment;
        vk = other_.vk;
        color_attachment_count = other_.color_attachment_count;
        has_depth_attachment = other_.has_depth_attachment;
        has_stencil_attachment = other_.has_stencil_attachment;
        RefreshPointers();
        return *this;
    }

    RenderTargetRenderingInfo& operator=(RenderTargetRenderingInfo&& other_) noexcept {
        if (this == &other_) {
            return *this;
        }
        color_attachments = std::move(other_.color_attachments);
        depth_attachment = other_.depth_attachment;
        stencil_attachment = other_.stencil_attachment;
        vk = other_.vk;
        color_attachment_count = other_.color_attachment_count;
        has_depth_attachment = other_.has_depth_attachment;
        has_stencil_attachment = other_.has_stencil_attachment;
        RefreshPointers();
        return *this;
    }

    void RefreshPointers() const noexcept {
        vk.pColorAttachments = (color_attachment_count > 0U) ? color_attachments.data() : nullptr;
        vk.pDepthAttachment = has_depth_attachment ? &depth_attachment : nullptr;
        vk.pStencilAttachment = has_stencil_attachment ? &stencil_attachment : nullptr;
    }

    [[nodiscard]] const VkRenderingInfo* VkInfoPtr() const noexcept {
        RefreshPointers();
        return &vk;
    }
};

class RenderTargetHost final {
public:
    struct TargetRecord final {
        RenderTargetHandle handle{};
        RenderTargetDesc desc{};
        RenderTargetViewDesc default_view_desc{};
        RenderTargetOwnership ownership = RenderTargetOwnership::owned;
        VkImage imported_image = VK_NULL_HANDLE;
        VkImageView imported_view = VK_NULL_HANDLE;
        resource::ImageResource owned_resource{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent3D extent{};
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkImageUsageFlags usage = 0U;
        VkImageAspectFlags aspect = 0U;
        RenderTargetStateKind state = RenderTargetStateKind::undefined;
        std::uint32_t resource_revision = 0U;
        bool active = false;
    };

    RenderTargetHost() = default;
    ~RenderTargetHost() = default;

    RenderTargetHost(const RenderTargetHost&) = delete;
    RenderTargetHost& operator=(const RenderTargetHost&) = delete;

    RenderTargetHost(RenderTargetHost&&) = delete;
    RenderTargetHost& operator=(RenderTargetHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const RenderTargetHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_,
                    std::uint64_t completed_submit_value_);

    [[nodiscard]] RenderTargetHandle CreatePersistentTarget(VulkanContext& context_,
                                                            const RenderTargetDesc& desc_,
                                                            VkExtent2D reference_extent_ = {});
    [[nodiscard]] RenderTargetHandle CreateTransientTarget(VulkanContext& context_,
                                                           const RenderTargetDesc& desc_,
                                                           VkExtent2D reference_extent_ = {});
    [[nodiscard]] EnsureRenderTargetResult EnsurePersistentTarget(
        VulkanContext& context_,
        RenderTargetHandle current_handle_,
        const RenderTargetDesc& desc_,
        VkExtent2D reference_extent_,
        std::uint64_t last_submitted_value_,
        std::uint64_t completed_submit_value_);

    [[nodiscard]] RenderTargetHandle CreateHistoryTarget(VulkanContext& context_,
                                                         const RenderTargetDesc& desc_,
                                                         VkExtent2D reference_extent_ = {});

    [[nodiscard]] RenderTargetHandle ImportTarget(VulkanContext& context_,
                                                  const ImportedRenderTargetDesc& desc_);

    [[nodiscard]] bool DestroyTarget(VulkanContext& context_,
                                     RenderTargetHandle handle_,
                                     std::uint64_t last_submitted_value_,
                                     std::uint64_t completed_submit_value_);

    [[nodiscard]] const TargetRecord* Resolve(RenderTargetHandle handle_) const noexcept;
    [[nodiscard]] TargetRecord* Resolve(RenderTargetHandle handle_) noexcept;
    [[nodiscard]] RenderTargetResolvedView ResolveView(RenderTargetHandle handle_) const;
    [[nodiscard]] VkImageView GetDefaultView(RenderTargetHandle handle_) const;
    [[nodiscard]] VkImageSubresourceRange DefaultSubresourceRange(RenderTargetHandle handle_) const;
    [[nodiscard]] RenderTargetPipelineSignature BuildPipelineSignature(
        const AttachmentRef* color_attachments_,
        std::uint32_t color_attachment_count_,
        const AttachmentRef* depth_attachment_ = nullptr,
        const AttachmentRef* stencil_attachment_ = nullptr) const;
    [[nodiscard]] RenderTargetRenderingInfo BuildRenderingInfo(
        VkRect2D render_area_,
        std::uint32_t layer_count_,
        const AttachmentRef* color_attachments_,
        std::uint32_t color_attachment_count_,
        const AttachmentRef* depth_attachment_ = nullptr,
        const AttachmentRef* stencil_attachment_ = nullptr) const;
    void RecordTransition(VkCommandBuffer command_buffer_,
                          RenderTargetHandle handle_,
                          RenderTargetStateKind new_state_);
    void RecordTransitionsForRendering(VkCommandBuffer command_buffer_,
                                       const AttachmentRef* color_attachments_,
                                       std::uint32_t color_attachment_count_,
                                       const AttachmentRef* depth_attachment_ = nullptr,
                                       const AttachmentRef* stencil_attachment_ = nullptr);
    [[nodiscard]] bool IsValid(RenderTargetHandle handle_) const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const RenderTargetHostStats& Stats() const noexcept;
    [[nodiscard]] static RenderTargetStateInfo DescribeState(RenderTargetStateKind state_kind_,
                                                             VkImageAspectFlags aspect_mask_) noexcept;

private:
    struct RetiredTargetPayload final {
        resource::ImageResource owned_resource{};
        VkImageView owned_view = VK_NULL_HANDLE;
    };

    [[nodiscard]] std::uint32_t AllocateSlot() noexcept;
    void RetireRecord(TargetRecord& record_,
                      std::uint64_t retire_value_);
    void CollectRetiredTargets(VulkanContext& context_,
                               std::uint64_t completed_submit_value_);
    void DestroyRetiredTargets(VulkanContext& context_) noexcept;
    void DestroyRetiredPayload(VulkanContext& context_,
                               RetiredTargetPayload& payload_) noexcept;
    void ResetRecord(TargetRecord& record_) noexcept;
    void RefreshStats() noexcept;
    [[nodiscard]] VkRenderingAttachmentInfo BuildOneRenderingAttachment(
        const AttachmentRef& attachment_,
        RenderTargetStateKind fallback_state_) const;

    [[nodiscard]] static VkExtent3D ResolveExtent(const RenderTargetDesc& desc_,
                                                  VkExtent2D reference_extent_);
    [[nodiscard]] static VkImageType ToVkImageType(RenderTargetDimension dimension_) noexcept;
    [[nodiscard]] static VkImageViewType DefaultViewType(RenderTargetDimension dimension_,
                                                         std::uint32_t array_layers_) noexcept;
    [[nodiscard]] static VkImageCreateFlags ResolveImageCreateFlags(const RenderTargetDesc& desc_) noexcept;
    [[nodiscard]] static resource::ImageCreateInfo BuildOwnedImageCreateInfo(VulkanContext& context_,
                                                                             const RenderTargetHostCreateInfo& host_create_info_,
                                                                             const RenderTargetDesc& desc_,
                                                                             VkExtent2D reference_extent_);
    [[nodiscard]] RenderTargetHandle CreateOwnedTarget(VulkanContext& context_,
                                                       const RenderTargetDesc& desc_,
                                                       VkExtent2D reference_extent_);
    static void ValidateOwnedDesc(const RenderTargetDesc& desc_);
    static void ValidateImportedDesc(const ImportedRenderTargetDesc& desc_);

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    RenderTargetHostCreateInfo create_info_cache{};
    RenderTargetMcVector<TargetRecord> targets{};
    RenderTargetMcVector<std::uint32_t> generations{};
    RenderTargetMcVector<std::uint32_t> free_indices{};
    RetireBus<RetiredTargetPayload> retired_targets{};
    RenderTargetHostStats stats{};
    bool initialized = false;
};

} // namespace vr::render
