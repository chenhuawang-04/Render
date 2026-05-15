#pragma once

#include "vr/render_graph/frame_snapshot.hpp"
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
    ResourceHandle present_target{};
    ResourceHandle scene_color{};
    ResourceHandle scene_depth{};
};

namespace detail {

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
        .usage = texture_usage_color_attachment_flag | texture_usage_present_flag,
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
        .usage = texture_usage_color_attachment_flag | texture_usage_sampled_flag,
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
            result.has_depth = true;
        }
    }

    if (snapshot_.HasOverlayView() &&
        !detail::IndicesMatch(snapshot_.selection.scene_view_index,
                              snapshot_.selection.overlay_view_index)) {
        if (!IsValidResourceHandle(result.scene_color)) {
            result.scene_color = builder_.CreateTexture("overlay_color",
                                                        detail::MakeSceneColorDesc(snapshot_),
                                                        ResourceLifetime::transient);
        }
        result.overlay_pass = builder_.AddPass("overlay_pass");
        if (IsValidResourceVersionHandle(color_chain)) {
            (void)builder_.Read(result.overlay_pass,
                                color_chain,
                                AccessDesc{.access = AccessKind::shader_sample_read});
        }
        color_chain = builder_.Write(result.overlay_pass,
                                     result.scene_color,
                                     AccessDesc{.access = AccessKind::color_attachment_write});
        result.has_overlay_pass = true;
    }

    result.present_pass = builder_.AddPass("present_to_swapchain", true);
    if (IsValidResourceVersionHandle(color_chain)) {
        (void)builder_.Read(result.present_pass,
                            color_chain,
                            AccessDesc{.access = AccessKind::shader_sample_read});
    }
    (void)builder_.Write(result.present_pass,
                         result.present_target,
                         AccessDesc{.access = AccessKind::present});
    result.built = true;
    return result;
}

} // namespace vr::render_graph
