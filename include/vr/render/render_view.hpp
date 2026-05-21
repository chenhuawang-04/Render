#pragma once

#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/concept/dimension.hpp"
#include "vr/render/render_target_types.hpp"
#include "vr/scene/background/background_traits.hpp"

#include <cstdint>
#include <cstring>
#include <type_traits>

#include <vulkan/vulkan.h>

namespace vr::render {

enum class RenderViewKind : std::uint8_t {
    world = 0U,
    ui = 1U,
    shadow = 2U,
    reflection = 3U,
    custom = 255U,
};

enum RenderViewFlags : std::uint32_t {
    render_view_none_flag = 0U,
    render_view_lighting_enabled_flag = 1U << 0U,
    render_view_shadow_enabled_flag = 1U << 1U,
    render_view_postprocess_enabled_flag = 1U << 2U,
    render_view_overlay_enabled_flag = 1U << 3U,
};

enum RenderViewDebugFlags : std::uint32_t {
    render_view_debug_none_flag = 0U,
    render_view_debug_bounds_flag = 1U << 0U,
    render_view_debug_culling_flag = 1U << 1U,
    render_view_debug_light_clusters_flag = 1U << 2U,
    render_view_debug_shadow_atlas_flag = 1U << 3U,
    render_view_debug_wireframe_flag = 1U << 4U,
};

enum class RenderPostProcessPolicy : std::uint8_t {
    inherit = 0U,
    enabled = 1U,
    disabled = 2U,
};

enum class BackgroundOverrideMode : std::uint8_t {
    inherit = 0U,
    disabled = 1U,
    override_state = 2U,
};

struct RenderViewViewport final {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float min_depth = 0.0F;
    float max_depth = 1.0F;
};

struct RenderViewScissor final {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

struct RenderViewTargetRefs final {
    RenderTargetHandle color_target = invalid_render_target_handle;
    RenderTargetHandle depth_target = invalid_render_target_handle;
};

template<ecs::DimensionTag DimensionT>
struct RenderViewBackgroundOverride;

template<>
struct RenderViewBackgroundOverride<ecs::Dim2> final {
    BackgroundOverrideMode mode = BackgroundOverrideMode::inherit;
    scene::Background2DRenderState state{};
};

template<>
struct RenderViewBackgroundOverride<ecs::Dim3> final {
    BackgroundOverrideMode mode = BackgroundOverrideMode::inherit;
    scene::SkyEnvironmentRenderState state{};
    scene::SkyEnvironmentGpuHandle gpu{};
};

template<ecs::DimensionTag DimensionT>
struct RenderView final {
    using CameraType = ecs::Camera<DimensionT>;
    using TransformType = ecs::Transform<DimensionT>;

    RenderViewKind kind = RenderViewKind::world;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
    std::uint32_t view_index = 0U;
    std::uint32_t flags = render_view_lighting_enabled_flag |
                          render_view_shadow_enabled_flag |
                          render_view_postprocess_enabled_flag;
    std::uint32_t culling_mask = 0xFFFF'FFFFU;
    std::uint32_t layer_mask = 0xFFFF'FFFFU;
    std::uint32_t debug_flags = render_view_debug_none_flag;
    RenderPostProcessPolicy postprocess_policy = RenderPostProcessPolicy::inherit;
    std::uint8_t reserved2 = 0U;
    std::uint16_t reserved3 = 0U;
    RenderViewViewport viewport{};
    RenderViewScissor scissor{};
    RenderViewTargetRefs targets{};
    RenderViewBackgroundOverride<DimensionT> background_override{};
    const CameraType* camera = nullptr;
    const TransformType* camera_transform = nullptr;
    std::uint64_t signature = 0U;
};

using RenderView2D = RenderView<ecs::Dim2>;
using RenderView3D = RenderView<ecs::Dim3>;

namespace detail {

[[nodiscard]] inline std::uint32_t RenderViewFloatBits(float value_) noexcept {
    std::uint32_t out = 0U;
    std::memcpy(&out, &value_, sizeof(out));
    return out;
}

inline void RenderViewHashCombine(std::uint64_t& hash_,
                                  std::uint64_t value_) noexcept {
    hash_ ^= value_;
    hash_ *= 1099511628211ULL;
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] inline std::uint64_t ComposeRenderViewSignature(
    const RenderView<DimensionT>& view_) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.kind));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.view_index));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.flags));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.culling_mask));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.layer_mask));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.debug_flags));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.postprocess_policy));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(RenderViewFloatBits(view_.viewport.x)));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(RenderViewFloatBits(view_.viewport.y)));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(RenderViewFloatBits(view_.viewport.width)));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(RenderViewFloatBits(view_.viewport.height)));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(RenderViewFloatBits(view_.viewport.min_depth)));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(RenderViewFloatBits(view_.viewport.max_depth)));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(view_.scissor.x)));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(view_.scissor.y)));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.scissor.width));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.scissor.height));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.targets.color_target.index));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.targets.color_target.generation));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.targets.depth_target.index));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.targets.depth_target.generation));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.background_override.mode));
    if constexpr (std::is_same_v<DimensionT, ecs::Dim2>) {
        RenderViewHashCombine(hash,
                              static_cast<std::uint64_t>(view_.background_override.state.revision));
    } else {
        RenderViewHashCombine(hash,
                              static_cast<std::uint64_t>(view_.background_override.state.revision));
        RenderViewHashCombine(hash,
                              static_cast<std::uint64_t>(view_.background_override.gpu.index));
        RenderViewHashCombine(hash,
                              static_cast<std::uint64_t>(view_.background_override.gpu.generation));
    }
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(view_.camera)));
    RenderViewHashCombine(hash,
                          static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(view_.camera_transform)));
    if (view_.camera != nullptr) {
        RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.camera->runtime.revision));
        RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.camera->runtime.culling_mask));
    }
    if (view_.camera_transform != nullptr) {
        RenderViewHashCombine(hash, static_cast<std::uint64_t>(view_.camera_transform->runtime.world_revision));
    }
    return hash;
}

} // namespace detail

[[nodiscard]] inline RenderViewViewport MakeRenderViewViewport(
    const ecs::CameraViewport& viewport_) noexcept {
    return RenderViewViewport{
        .x = viewport_.origin_x,
        .y = viewport_.origin_y,
        .width = viewport_.width,
        .height = viewport_.height,
        .min_depth = 0.0F,
        .max_depth = 1.0F,
    };
}

[[nodiscard]] inline RenderViewScissor MakeRenderViewScissor(
    const RenderViewViewport& viewport_) noexcept {
    const float width = (viewport_.width > 0.0F) ? viewport_.width : 0.0F;
    const float height = (viewport_.height > 0.0F) ? viewport_.height : 0.0F;
    return RenderViewScissor{
        .x = static_cast<std::int32_t>(viewport_.x),
        .y = static_cast<std::int32_t>(viewport_.y),
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
    };
}

template<ecs::DimensionTag DimensionT>
[[nodiscard]] RenderView<DimensionT> MakeRenderViewFromCamera(
    const ecs::Camera<DimensionT>& camera_,
    const ecs::Transform<DimensionT>* camera_transform_ = nullptr,
    RenderViewKind kind_ = RenderViewKind::world,
    std::uint32_t view_index_ = 0U) noexcept {
    RenderView<DimensionT> view{};
    view.kind = kind_;
    view.view_index = view_index_;
    view.camera = &camera_;
    view.camera_transform = camera_transform_;
    view.culling_mask = camera_.runtime.culling_mask;
    view.viewport = MakeRenderViewViewport(camera_.style.viewport);
    view.scissor = MakeRenderViewScissor(view.viewport);
    view.signature = detail::ComposeRenderViewSignature(view);
    return view;
}

template<ecs::DimensionTag DimensionT>
void RefreshRenderViewSignature(RenderView<DimensionT>& view_) noexcept {
    view_.signature = detail::ComposeRenderViewSignature(view_);
}

[[nodiscard]] constexpr bool HasRenderViewFlag(std::uint32_t flags_,
                                               RenderViewFlags flag_) noexcept {
    return (flags_ & static_cast<std::uint32_t>(flag_)) != 0U;
}

[[nodiscard]] inline VkViewport ToVkViewport(const RenderViewViewport& viewport_) noexcept {
    return VkViewport{
        .x = viewport_.x,
        .y = viewport_.y,
        .width = viewport_.width,
        .height = viewport_.height,
        .minDepth = viewport_.min_depth,
        .maxDepth = viewport_.max_depth,
    };
}

[[nodiscard]] inline VkRect2D ToVkRect2D(const RenderViewScissor& scissor_) noexcept {
    return VkRect2D{
        .offset = VkOffset2D{.x = scissor_.x, .y = scissor_.y},
        .extent = VkExtent2D{.width = scissor_.width, .height = scissor_.height},
    };
}

static_assert(std::is_standard_layout_v<RenderViewViewport>);
static_assert(std::is_standard_layout_v<RenderViewScissor>);
static_assert(std::is_standard_layout_v<RenderViewTargetRefs>);
static_assert(std::is_standard_layout_v<RenderViewBackgroundOverride<ecs::Dim2>>);
static_assert(std::is_standard_layout_v<RenderViewBackgroundOverride<ecs::Dim3>>);
static_assert(std::is_standard_layout_v<RenderView2D>);
static_assert(std::is_standard_layout_v<RenderView3D>);

} // namespace vr::render

