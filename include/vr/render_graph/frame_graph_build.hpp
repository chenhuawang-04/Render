#pragma once

#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <type_traits>

namespace vr::render_graph {

template<ecs::DimensionTag DimensionT>
struct MinimalFrameGraphBuildResult final {
    bool built = false;
    bool has_scene_pass = false;
    bool has_overlay_pass = false;
    bool has_depth = false;
    PassHandle scene_pass{};
    PassHandle overlay_pass{};
    PassHandle present_pass{};
    PassHandle present_transition_pass{};
    ResourceHandle present_target{};
    ResourceHandle scene_color{};
    ResourceHandle scene_depth{};
};

namespace detail {

inline void RecordMinimalPresentCopyPass(GraphCommandContext& context_,
                                         const ResourceHandle source_color_,
                                         const ResourceHandle present_target_) {
    const auto source = context_.ResolveTextureView(source_color_);
    const auto target = context_.ResolveTextureView(present_target_);
    if (source.image == VK_NULL_HANDLE || target.image == VK_NULL_HANDLE) {
        return;
    }

    if (source.format == target.format &&
        source.extent.width == target.extent.width &&
        source.extent.height == target.extent.height) {
        VkImageCopy copy_region{};
        copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.srcSubresource.mipLevel = 0U;
        copy_region.srcSubresource.baseArrayLayer = 0U;
        copy_region.srcSubresource.layerCount = 1U;
        copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.dstSubresource.mipLevel = 0U;
        copy_region.dstSubresource.baseArrayLayer = 0U;
        copy_region.dstSubresource.layerCount = 1U;
        copy_region.extent = VkExtent3D{source.extent.width, source.extent.height, 1U};
        vkCmdCopyImage(context_.CommandBuffer(),
                       source.image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       target.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1U,
                       &copy_region);
        return;
    }

    VkImageBlit blit_region{};
    blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_region.srcSubresource.mipLevel = 0U;
    blit_region.srcSubresource.baseArrayLayer = 0U;
    blit_region.srcSubresource.layerCount = 1U;
    blit_region.srcOffsets[0] = VkOffset3D{0, 0, 0};
    blit_region.srcOffsets[1] = VkOffset3D{
        static_cast<std::int32_t>(source.extent.width),
        static_cast<std::int32_t>(source.extent.height),
        1,
    };
    blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_region.dstSubresource.mipLevel = 0U;
    blit_region.dstSubresource.baseArrayLayer = 0U;
    blit_region.dstSubresource.layerCount = 1U;
    blit_region.dstOffsets[0] = VkOffset3D{0, 0, 0};
    blit_region.dstOffsets[1] = VkOffset3D{
        static_cast<std::int32_t>(target.extent.width),
        static_cast<std::int32_t>(target.extent.height),
        1,
    };
    vkCmdBlitImage(context_.CommandBuffer(),
                   source.image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   target.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1U,
                   &blit_region,
                   VK_FILTER_NEAREST);
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] TextureFormat ResolveSceneColorFormat() noexcept {
    if constexpr (std::is_same_v<DimensionT, ecs::Dim3>) {
        return TextureFormat::r16g16b16a16_sfloat;
    } else {
        return TextureFormat::r8g8b8a8_unorm;
    }
}

[[nodiscard]] inline Extent3D ResolveGraphExtent(const Extent3D& reference_extent_) noexcept {
    if (reference_extent_.width != 0U && reference_extent_.height != 0U &&
        reference_extent_.depth != 0U) {
        return reference_extent_;
    }
    return Extent3D{};
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] TextureDesc MakePresentTargetDesc(const FrameSnapshot<DimensionT>& snapshot_) noexcept {
    return TextureDesc{
        .dimension = TextureDimension::image_2d,
        .format = TextureFormat::unknown,
        .extent = ResolveGraphExtent(snapshot_.reference_extent),
        .usage = texture_usage_color_attachment_flag |
                 texture_usage_transfer_dst_flag |
                 texture_usage_present_flag,
        .mip_level_count = 1U,
        .array_layer_count = 1U,
        .sample_count = SampleCount::x1,
    };
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] TextureDesc MakeSceneColorDesc(const FrameSnapshot<DimensionT>& snapshot_) noexcept {
    return TextureDesc{
        .dimension = TextureDimension::image_2d,
        .format = ResolveSceneColorFormat<DimensionT>(),
        .extent = ResolveGraphExtent(snapshot_.reference_extent),
        .usage = texture_usage_color_attachment_flag |
                 texture_usage_sampled_flag |
                 texture_usage_transfer_src_flag,
        .mip_level_count = 1U,
        .array_layer_count = 1U,
        .sample_count = SampleCount::x1,
    };
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] TextureDesc MakeSceneDepthDesc(const FrameSnapshot<DimensionT>& snapshot_) noexcept {
    return TextureDesc{
        .dimension = TextureDimension::image_2d,
        .format = TextureFormat::d32_sfloat,
        .extent = ResolveGraphExtent(snapshot_.reference_extent),
        .usage = texture_usage_depth_stencil_attachment_flag,
        .mip_level_count = 1U,
        .array_layer_count = 1U,
        .sample_count = SampleCount::x1,
    };
}

[[nodiscard]] constexpr bool IndicesMatch(const std::uint32_t lhs_,
                                          const std::uint32_t rhs_) noexcept {
    return lhs_ != render::invalid_scene_view_index && lhs_ == rhs_;
}

} // namespace detail

template<ecs::DimensionTag DimensionT>
[[nodiscard]] MinimalFrameGraphBuildResult<DimensionT> BuildMinimalFrameGraph(
    RenderGraphBuilder& builder_,
    const FrameSnapshot<DimensionT>& snapshot_) {
    return BuildMinimalFrameGraph(builder_,
                                  snapshot_,
                                  [](RenderGraphBuilder&,
                                     const FrameSnapshot<DimensionT>&,
                                     MinimalFrameGraphBuildResult<DimensionT>&,
                                     ResourceVersionHandle&) {});
}

template<ecs::DimensionTag DimensionT, typename ExtendFnT>
[[nodiscard]] MinimalFrameGraphBuildResult<DimensionT> BuildMinimalFrameGraph(
    RenderGraphBuilder& builder_,
    const FrameSnapshot<DimensionT>& snapshot_,
    ExtendFnT&& extend_) {
    MinimalFrameGraphBuildResult<DimensionT> result{};
    if (snapshot_.ViewCount() == 0U || snapshot_.ActiveView() == nullptr) {
        return result;
    }

    result.present_target = builder_.CreateTexture("present_target",
                                                   detail::MakePresentTargetDesc(snapshot_),
                                                   ResourceLifetime::imported);

    ResourceVersionHandle color_chain = invalid_resource_version;

    if (snapshot_.HasSceneView()) {
        result.scene_color = builder_.CreateTexture("scene_color",
                                                    detail::MakeSceneColorDesc(snapshot_),
                                                    ResourceLifetime::transient);
        result.scene_pass = builder_.AddPass("main_scene_pass");
        color_chain = builder_.Write(result.scene_pass,
                                     result.scene_color,
                                     AccessDesc{.access = AccessKind::color_attachment_write});
        result.has_scene_pass = true;

        if constexpr (std::is_same_v<DimensionT, ecs::Dim3>) {
            result.scene_depth = builder_.CreateTexture("scene_depth",
                                                        detail::MakeSceneDepthDesc(snapshot_),
                                                        ResourceLifetime::transient);
            (void)builder_.Write(result.scene_pass,
                                 result.scene_depth,
                                 AccessDesc{.access = AccessKind::depth_stencil_write});
            builder_.SetRasterPassDesc(result.scene_pass,
                                       RasterPassDesc{
                                           .color_attachments = {
                                               RasterColorAttachmentDesc{
                                                   .target = result.scene_color,
                                                   .load_op = AttachmentLoadOp::clear,
                                                   .store_op = AttachmentStoreOp::store,
                                                   .clear_value = {.red = 0.08F, .green = 0.10F, .blue = 0.14F, .alpha = 1.0F},
                                               },
                                           },
                                           .has_depth_attachment = true,
                                           .depth_attachment = RasterDepthAttachmentDesc{
                                               .target = result.scene_depth,
                                               .load_op = AttachmentLoadOp::clear,
                                               .store_op = AttachmentStoreOp::store,
                                               .stencil_load_op = AttachmentLoadOp::dont_care,
                                               .stencil_store_op = AttachmentStoreOp::dont_care,
                                               .clear_value = {.depth = 1.0F, .stencil = 0U},
                                           },
                                       });
            result.has_depth = true;
        } else {
            builder_.SetRasterPassDesc(result.scene_pass,
                                       RasterPassDesc{
                                           .color_attachments = {
                                               RasterColorAttachmentDesc{
                                                   .target = result.scene_color,
                                                   .load_op = AttachmentLoadOp::clear,
                                                   .store_op = AttachmentStoreOp::store,
                                                   .clear_value = {.red = 0.08F, .green = 0.10F, .blue = 0.14F, .alpha = 1.0F},
                                               },
                                           },
                                       });
        }
    }

    const bool build_overlay_pass =
        snapshot_.HasOverlayView() &&
        !detail::IndicesMatch(snapshot_.selection.scene_view_index,
                              snapshot_.selection.overlay_view_index);
    if (build_overlay_pass) {
        if (!IsValidResourceHandle(result.scene_color)) {
            result.scene_color = builder_.CreateTexture("overlay_color",
                                                        detail::MakeSceneColorDesc(snapshot_),
                                                        ResourceLifetime::transient);
        }
        result.overlay_pass = builder_.AddPass("overlay_pass");
        builder_.SetRasterPassDesc(result.overlay_pass,
                                   RasterPassDesc{
                                       .color_attachments = {
                                           RasterColorAttachmentDesc{
                                               .target = result.scene_color,
                                               .load_op = AttachmentLoadOp::load,
                                               .store_op = AttachmentStoreOp::store,
                                           },
                                       },
                                   });
        result.has_overlay_pass = true;
    }

    extend_(builder_, snapshot_, result, color_chain);

    if (IsValidPassHandle(result.overlay_pass)) {
        if (IsValidResourceVersionHandle(color_chain)) {
            (void)builder_.Read(result.overlay_pass,
                                color_chain,
                                AccessDesc{.access = AccessKind::shader_sample_read});
        }
        color_chain = builder_.Write(result.overlay_pass,
                                     result.scene_color,
                                     AccessDesc{.access = AccessKind::color_attachment_write});
    }

    ResourceVersionHandle present_ready_version = invalid_resource_version;
    if (IsValidResourceVersionHandle(color_chain) &&
        color_chain.resource_index == result.present_target.index) {
        present_ready_version = color_chain;
    } else {
        result.present_pass = builder_.AddPass("present_to_swapchain");
        if (IsValidResourceVersionHandle(color_chain)) {
            (void)builder_.Read(result.present_pass,
                                color_chain,
                                AccessDesc{.access = AccessKind::transfer_read});
        }
        present_ready_version = builder_.Write(result.present_pass,
                                              result.present_target,
                                              AccessDesc{.access = AccessKind::transfer_write});
        if (IsValidResourceHandle(result.scene_color)) {
            builder_.SetExecuteCallback(result.present_pass,
                                        [source = result.scene_color,
                                         present = result.present_target](GraphCommandContext& context_) {
                                            detail::RecordMinimalPresentCopyPass(context_, source, present);
                                        });
        }
    }

    result.present_transition_pass = builder_.AddPass("present_transition", true);
    (void)builder_.Read(result.present_transition_pass,
                        present_ready_version,
                        AccessDesc{.access = AccessKind::present});
    result.built = true;
    return result;
}

} // namespace vr::render_graph
