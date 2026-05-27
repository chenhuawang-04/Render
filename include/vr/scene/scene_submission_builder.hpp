#pragma once

#include "vr/scene/scene_prepare.hpp"

namespace vr::scene {

template<ecs::DimensionTag DimT, typename BackgroundT>
class SceneSubmissionBuilder;

template<>
class SceneSubmissionBuilder<ecs::Dim2, SpriteBackground> final {
public:
    using SceneType = Scene2D;
    using PacketType = render::RenderScenePacket2D;
    using ViewType = render::RenderView2D;

    [[nodiscard]] static Background2DRenderState ResolveBackground(
        const SceneType& scene_,
        const ViewType* active_view_) noexcept {
        Background2DRenderState state = ScenePrepare<ecs::Dim2, SpriteBackground>::Resolve(scene_);
        if (active_view_ == nullptr) {
            return state;
        }

        switch (active_view_->background_override.mode) {
        case render::BackgroundOverrideMode::disabled:
            state = {};
            state.mode = Background2DMode::none;
            return state;
        case render::BackgroundOverrideMode::override_state:
            return active_view_->background_override.state;
        case render::BackgroundOverrideMode::inherit:
        default:
            return state;
        }
    }

    [[nodiscard]] static PacketType Build(const SceneType& scene_,
                                          const ViewType* views_,
                                          std::uint32_t view_count_,
                                          std::uint32_t active_view_index_ = 0U,
                                          render::SceneSubmissionId submission_id_ = {},
                                          render::RenderScenePacketKind kind_ =
                                              render::RenderScenePacketKind::world) noexcept {
        PacketType packet = render::MakeScenePacketFromViewRange(views_,
                                                                 view_count_,
                                                                 active_view_index_,
                                                                 submission_id_,
                                                                 kind_);
        render::ApplySceneSubmissionPayload(
            packet,
            render::SceneSubmissionPayload<ecs::Dim2>{
                .background = ResolveBackground(scene_, packet.ActiveView()),
            });
        render::RefreshRenderScenePacketSignature(packet);
        return packet;
    }
};

template<>
class SceneSubmissionBuilder<ecs::Dim3, SkyEnvironment> final {
public:
    using SceneType = Scene3D;
    using PacketType = render::RenderScenePacket3D;
    using ViewType = render::RenderView3D;

    struct ResolvedEnvironment final {
        SkyEnvironmentRenderState state{};
        SkyEnvironmentGpuHandle gpu{};
    };

    [[nodiscard]] static ResolvedEnvironment ResolveEnvironment(
        const SceneType& scene_,
        const ViewType* active_view_) noexcept {
        ResolvedEnvironment resolved{
            .state = ScenePrepare<ecs::Dim3, SkyEnvironment>::Resolve(scene_),
            .gpu = {},
        };
        if (active_view_ == nullptr) {
            return resolved;
        }

        switch (active_view_->background_override.mode) {
        case render::BackgroundOverrideMode::disabled:
            resolved.state = {};
            resolved.state.mode = SkyEnvironmentMode::none;
            resolved.gpu = {};
            return resolved;
        case render::BackgroundOverrideMode::override_state:
            resolved.state = active_view_->background_override.state;
            resolved.gpu = active_view_->background_override.gpu;
            return resolved;
        case render::BackgroundOverrideMode::inherit:
        default:
            return resolved;
        }
    }

    [[nodiscard]] static PacketType Build(const SceneType& scene_,
                                          const ViewType* views_,
                                          std::uint32_t view_count_,
                                          std::uint32_t active_view_index_ = 0U,
                                          render::SceneSubmissionId submission_id_ = {},
                                          render::RenderScenePacketKind kind_ =
                                              render::RenderScenePacketKind::world) noexcept {
        PacketType packet = render::MakeScenePacketFromViewRange(views_,
                                                                 view_count_,
                                                                 active_view_index_,
                                                                 submission_id_,
                                                                 kind_);
        const ResolvedEnvironment resolved = ResolveEnvironment(scene_, packet.ActiveView());
        render::ApplySceneSubmissionPayload(
            packet,
            render::SceneSubmissionPayload<ecs::Dim3>{
                .environment = resolved.state,
                .environment_gpu = resolved.gpu,
            });
        render::RefreshRenderScenePacketSignature(packet);
        return packet;
    }
};

} // namespace vr::scene

