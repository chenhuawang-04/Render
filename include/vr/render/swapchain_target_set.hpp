#pragma once

#include "vr/render/render_target_host.hpp"
#include "vr/render/swapchain_host.hpp"

namespace vr::render {

struct SwapchainTargetSetImportConfig final {
    const char* debug_name = "SwapchainImportedTarget";
    VkImageUsageFlags usage = 0U;
};

class SwapchainTargetSet final {
public:
    SwapchainTargetSet() = default;
    ~SwapchainTargetSet() = default;

    SwapchainTargetSet(const SwapchainTargetSet&) = delete;
    SwapchainTargetSet& operator=(const SwapchainTargetSet&) = delete;
    SwapchainTargetSet(SwapchainTargetSet&&) = delete;
    SwapchainTargetSet& operator=(SwapchainTargetSet&&) = delete;

    void Reset() noexcept;

    void Invalidate(VulkanContext& context_,
                    RenderTargetHost& render_target_host_,
                    std::uint64_t last_submitted_value_,
                    std::uint64_t completed_submit_value_);

    template<typename SwapchainT>
    void ImportOrRefresh(VulkanContext& context_,
                         const SwapchainT& swapchain_,
                         RenderTargetHost& render_target_host_,
                         const SwapchainTargetSetImportConfig& import_config_,
                         std::uint64_t last_submitted_value_,
                         std::uint64_t completed_submit_value_) {
        if (!swapchain_.IsValid()) {
            Invalidate(context_,
                       render_target_host_,
                       last_submitted_value_,
                       completed_submit_value_);
            return;
        }

        const std::uint64_t generation = swapchain_.Generation();
        const std::uint32_t image_count = swapchain_.ImageCount();
        if (generation_cache == generation && handles.size() == image_count) {
            return;
        }

        Invalidate(context_, render_target_host_, last_submitted_value_, completed_submit_value_);
        handles.reserve(image_count);

        ImportedRenderTargetDesc desc{};
        desc.debug_name = import_config_.debug_name;
        desc.ownership = RenderTargetOwnership::imported_image_imported_view;
        desc.dimension = RenderTargetDimension::image_2d;
        desc.format = swapchain_.Format();
        desc.extent = VkExtent3D{swapchain_.Extent().width, swapchain_.Extent().height, 1U};
        desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.usage = import_config_.usage;
        desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        desc.mip_levels = 1U;
        desc.array_layers = 1U;
        desc.color_encoding = (swapchain_.ColorSpace() == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            ? RenderTargetColorEncoding::srgb
            : RenderTargetColorEncoding::linear;
        desc.initial_state = RenderTargetStateKind::undefined;

        for (std::uint32_t image_index = 0U; image_index < image_count; ++image_index) {
            desc.image = swapchain_.Image(image_index);
            desc.image_view = swapchain_.ImageView(image_index);
            handles.push_back(render_target_host_.ImportTarget(context_, desc));
        }

        generation_cache = generation;
    }

    [[nodiscard]] RenderTargetHandle Get(std::uint32_t image_index_) const noexcept;
    [[nodiscard]] std::uint64_t Generation() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;

private:
    vr::McVector<RenderTargetHandle> handles{};
    std::uint64_t generation_cache = 0U;
};

} // namespace vr::render
