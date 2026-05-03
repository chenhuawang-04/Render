#pragma once

#include "vr/ecs/concept/dimension.hpp"
#include "vr/render/render_view.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::render {

enum class RenderScenePacketKind : std::uint8_t {
    world = 0U,
    ui = 1U,
    mixed = 2U,
    custom = 255U,
};

enum RenderScenePacketFlags : std::uint32_t {
    render_scene_packet_none_flag = 0U,
    render_scene_packet_allow_postprocess_flag = 1U << 0U,
    render_scene_packet_allow_shadow_flag = 1U << 1U,
    render_scene_packet_allow_overlay_flag = 1U << 2U,
};

template<ecs::DimensionTag DimensionT>
struct RenderScenePacket final {
    using ViewType = RenderView<DimensionT>;

    RenderScenePacketKind kind = RenderScenePacketKind::world;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
    std::uint32_t active_view_index = 0U;
    std::uint32_t flags = render_scene_packet_allow_postprocess_flag |
                          render_scene_packet_allow_shadow_flag |
                          render_scene_packet_allow_overlay_flag;
    const ViewType* views = nullptr;
    std::uint32_t view_count = 0U;
    std::uint32_t render_layer_mask = 0xFFFF'FFFFU;
    std::uint64_t submission_id = 0U;
    std::uint64_t signature = 0U;

    [[nodiscard]] const ViewType* ActiveView() const noexcept {
        if (views == nullptr || view_count == 0U || active_view_index >= view_count) {
            return nullptr;
        }
        return views + active_view_index;
    }
};

using RenderScenePacket2D = RenderScenePacket<ecs::Dim2>;
using RenderScenePacket3D = RenderScenePacket<ecs::Dim3>;

namespace detail {

template<ecs::DimensionTag DimensionT>
[[nodiscard]] inline std::uint64_t ComposeRenderScenePacketSignature(
    const RenderScenePacket<DimensionT>& packet_) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.kind));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.active_view_index));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.flags));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.view_count));
    RenderViewHashCombine(hash, static_cast<std::uint64_t>(packet_.render_layer_mask));
    RenderViewHashCombine(hash, packet_.submission_id);
    for (std::uint32_t view_index = 0U; view_index < packet_.view_count; ++view_index) {
        const RenderView<DimensionT>& view = packet_.views[view_index];
        RenderViewHashCombine(hash, view.signature);
    }
    return hash;
}

} // namespace detail

template<ecs::DimensionTag DimensionT>
[[nodiscard]] RenderScenePacket<DimensionT> MakeSingleViewScenePacket(
    const RenderView<DimensionT>& view_,
    std::uint64_t submission_id_ = 0U,
    RenderScenePacketKind kind_ = RenderScenePacketKind::world) noexcept {
    RenderScenePacket<DimensionT> packet{};
    packet.kind = kind_;
    packet.views = &view_;
    packet.view_count = 1U;
    packet.active_view_index = 0U;
    packet.submission_id = submission_id_;
    packet.render_layer_mask = view_.layer_mask;
    packet.signature = detail::ComposeRenderScenePacketSignature(packet);
    return packet;
}

template<ecs::DimensionTag DimensionT>
void RefreshRenderScenePacketSignature(RenderScenePacket<DimensionT>& packet_) noexcept {
    packet_.signature = detail::ComposeRenderScenePacketSignature(packet_);
}

static_assert(std::is_standard_layout_v<RenderScenePacket2D>);
static_assert(std::is_standard_layout_v<RenderScenePacket3D>);

} // namespace vr::render
