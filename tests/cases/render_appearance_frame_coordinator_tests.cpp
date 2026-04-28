#include "support/test_framework.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/render/appearance_frame_coordinator.hpp"
#include "vr/render/appearance_prepare_bridge.hpp"

#include <cstdint>

namespace {

template<typename T>
using RenderAppearanceCoordinatorTestMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

VR_TEST_CASE(RenderAppearanceFrameCoordinator_dim3_prepare_once_reuse_in_same_frame,
             "unit;core;render;appearance;coordinator") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::AppearanceFrameCoordinator<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 16U;
    RenderAppearanceCoordinatorTestMcVector<Appearance3D> appearance_components{};
    appearance_components.resize(component_count);
    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetTextureBaseColorId(appearance_components[i], 100U + i);
        AppearanceSystem3D::SetTextureNormalId(appearance_components[i], 200U + i);
        AppearanceSystem3D::SetBindingLayoutId(appearance_components[i], 5U);
        AppearanceSystem3D::SetSamplerStateId(appearance_components[i], 2U);
    }

    Coordinator3D coordinator{};
    coordinator.SetAppearanceData(appearance_components.data(), component_count);
    coordinator.Reserve(component_count);

    const auto first_prepare = coordinator.PrepareFrame(7U);
    VR_CHECK(first_prepare.has_appearance_data);
    VR_CHECK(first_prepare.build_invoked);
    VR_CHECK(first_prepare.runtime_stats.component_count == component_count);

    const auto second_prepare_same_frame = coordinator.PrepareFrame(7U);
    VR_CHECK(second_prepare_same_frame.has_appearance_data);
    VR_CHECK(!second_prepare_same_frame.build_invoked);

    const auto& stats = coordinator.Stats();
    VR_CHECK(stats.prepare_call_count == 2U);
    VR_CHECK(stats.runtime_build_call_count == 1U);
    VR_CHECK(stats.frame_reuse_hit_count == 1U);
}

VR_TEST_CASE(RenderAppearanceFrameCoordinator_dim3_link_geometry_and_surface,
             "unit;core;render;appearance;coordinator") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::AppearanceFrameCoordinator<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 8U;
    RenderAppearanceCoordinatorTestMcVector<Appearance3D> appearance_components{};
    RenderAppearanceCoordinatorTestMcVector<Geometry3D> geometry_components{};
    RenderAppearanceCoordinatorTestMcVector<Surface3D> surface_components{};
    appearance_components.resize(component_count);
    geometry_components.resize(component_count);
    surface_components.resize(component_count);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetTextureBaseColorId(appearance_components[i], 300U + i);
        AppearanceSystem3D::SetTextureNormalId(appearance_components[i], 400U + i);
        AppearanceSystem3D::SetBindingLayoutId(appearance_components[i], 7U);
        AppearanceSystem3D::SetSamplerStateId(appearance_components[i], 3U);

        GeometrySystem3D::Initialize(geometry_components[i]);
        GeometrySystem3D::SetGeometryId(geometry_components[i], 500U + i);
        GeometrySystem3D::SetAppearanceHandle(geometry_components[i], vr::ecs::invalid_appearance_handle);

        SurfaceSystem3D::Initialize(surface_components[i]);
        SurfaceSystem3D::SetTextureId(surface_components[i], 600U + i);
        SurfaceSystem3D::SetAppearanceHandle(surface_components[i], vr::ecs::invalid_appearance_handle);
    }

    Coordinator3D coordinator{};
    coordinator.SetAppearanceData(appearance_components.data(), component_count);
    coordinator.Reserve(component_count);

    const auto prepare_result = coordinator.PrepareFrame(11U);
    VR_CHECK(prepare_result.has_appearance_data);
    VR_CHECK(prepare_result.build_invoked);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        const auto handle = appearance_components[i].runtime.gpu_record_handle;
        GeometrySystem3D::SetAppearanceHandle(geometry_components[i], handle);
        SurfaceSystem3D::SetAppearanceHandle(surface_components[i], handle);
    }

    const vr::ecs::AppearanceLinkStats geometry_link_stats = coordinator.LinkGeometry(
        geometry_components.data(),
        component_count,
        11U);
    const vr::ecs::AppearanceLinkStats surface_link_stats = coordinator.LinkSurface(
        surface_components.data(),
        component_count,
        11U);

    VR_CHECK(geometry_link_stats.resolved_count == component_count);
    VR_CHECK(surface_link_stats.resolved_count == component_count);
    VR_CHECK(geometry_link_stats.updated_count == component_count);
    VR_CHECK(surface_link_stats.updated_count == component_count);

    const auto& coord_stats = coordinator.Stats();
    VR_CHECK(coord_stats.runtime_build_call_count == 1U);
    VR_CHECK(coord_stats.geometry_link_call_count == 1U);
    VR_CHECK(coord_stats.surface_link_call_count == 1U);
    VR_CHECK(coord_stats.total_link_scanned_count >= static_cast<std::uint64_t>(component_count * 2U));
}

VR_TEST_CASE(RenderAppearanceFrameCoordinator_dim3_accumulate_dirty_hints_before_prepare,
             "unit;core;render;appearance;coordinator") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::AppearanceFrameCoordinator<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 16U;
    RenderAppearanceCoordinatorTestMcVector<Appearance3D> appearance_components{};
    appearance_components.resize(component_count);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetTextureBaseColorId(appearance_components[i], 32U + i);
        AppearanceSystem3D::SetTextureNormalId(appearance_components[i], 64U + i);
        AppearanceSystem3D::SetBindingLayoutId(appearance_components[i], 4U);
        AppearanceSystem3D::SetSamplerStateId(appearance_components[i], 2U);
    }

    Coordinator3D coordinator{};
    coordinator.SetAppearanceData(appearance_components.data(), component_count);
    coordinator.Reserve(component_count);
    (void)coordinator.PrepareFrame(1U);

    const std::uint32_t dirty_a = 3U;
    const std::uint32_t dirty_b = 9U;
    AppearanceSystem3D::SetRoughness(appearance_components[dirty_a], 0.21F);
    AppearanceSystem3D::SetMetallic(appearance_components[dirty_b], 0.75F);
    coordinator.SetDirtyHint(&dirty_a, 1U);
    coordinator.SetDirtyHint(&dirty_b, 1U);

    const auto dirty_prepare = coordinator.PrepareFrame(2U);
    VR_CHECK(dirty_prepare.has_appearance_data);
    VR_CHECK(dirty_prepare.build_invoked);
    VR_CHECK(dirty_prepare.runtime_stats.used_dirty_indices == 1U);
    VR_CHECK(dirty_prepare.runtime_stats.updated_record_count >= 2U);
}

VR_TEST_CASE(RenderAppearanceFrameCoordinator_dim3_dirty_hint_after_prepare_same_frame_rebuilds,
             "unit;core;render;appearance;coordinator") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::AppearanceFrameCoordinator<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 8U;
    RenderAppearanceCoordinatorTestMcVector<Appearance3D> appearance_components{};
    appearance_components.resize(component_count);
    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetTextureBaseColorId(appearance_components[i], 100U + i);
        AppearanceSystem3D::SetBindingLayoutId(appearance_components[i], 5U);
    }

    Coordinator3D coordinator{};
    coordinator.SetAppearanceData(appearance_components.data(), component_count);
    coordinator.Reserve(component_count);
    (void)coordinator.PrepareFrame(3U);

    const std::uint32_t dirty_first = 1U;
    AppearanceSystem3D::SetRoughness(appearance_components[dirty_first], 0.38F);
    coordinator.SetDirtyHint(&dirty_first, 1U);
    const auto first_prepare = coordinator.PrepareFrame(4U);
    VR_CHECK(first_prepare.build_invoked);

    const std::uint32_t dirty_second = 6U;
    AppearanceSystem3D::SetMetallic(appearance_components[dirty_second], 0.62F);
    coordinator.SetDirtyHint(&dirty_second, 1U);
    const auto second_prepare_same_frame = coordinator.PrepareFrame(4U);
    VR_CHECK(second_prepare_same_frame.build_invoked);
    VR_CHECK(second_prepare_same_frame.runtime_stats.used_dirty_indices == 1U);

    const auto& stats = coordinator.Stats();
    VR_CHECK(stats.runtime_build_call_count >= 3U);
}

VR_TEST_CASE(RenderAppearancePrepareBridge_dim3_dual_renderer_duplicate_dirty_hint_reuses_cached_build,
             "unit;core;render;appearance;coordinator;bridge") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::AppearanceFrameCoordinator<vr::ecs::Dim3>;
    using Bridge3D = vr::render::AppearancePrepareBridge<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 32U;
    RenderAppearanceCoordinatorTestMcVector<Appearance3D> appearance_components{};
    RenderAppearanceCoordinatorTestMcVector<Geometry3D> geometry_components{};
    RenderAppearanceCoordinatorTestMcVector<Surface3D> surface_components{};
    appearance_components.resize(component_count);
    geometry_components.resize(component_count);
    surface_components.resize(component_count);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetTextureBaseColorId(appearance_components[i], 1000U + i);
        AppearanceSystem3D::SetTextureNormalId(appearance_components[i], 2000U + i);
        AppearanceSystem3D::SetBindingLayoutId(appearance_components[i], 9U);
        AppearanceSystem3D::SetSamplerStateId(appearance_components[i], 1U);

        GeometrySystem3D::Initialize(geometry_components[i]);
        GeometrySystem3D::SetGeometryId(geometry_components[i], 3000U + i);

        SurfaceSystem3D::Initialize(surface_components[i]);
        SurfaceSystem3D::SetTextureId(surface_components[i], 4000U + i);
    }

    Coordinator3D coordinator{};
    coordinator.SetAppearanceData(appearance_components.data(), component_count);
    coordinator.Reserve(component_count);
    (void)coordinator.PrepareFrame(1U);
    for (std::uint32_t i = 0U; i < component_count; ++i) {
        const auto handle = appearance_components[i].runtime.gpu_record_handle;
        GeometrySystem3D::SetAppearanceHandle(geometry_components[i], handle);
        SurfaceSystem3D::SetAppearanceHandle(surface_components[i], handle);
    }

    Bridge3D geometry_bridge{};
    Bridge3D surface_bridge{};
    geometry_bridge.SetCoordinator(&coordinator);
    surface_bridge.SetCoordinator(&coordinator);
    geometry_bridge.SetAppearanceData(appearance_components.data(), component_count);
    surface_bridge.SetAppearanceData(appearance_components.data(), component_count);

    const std::uint32_t dirty_index = 5U;
    AppearanceSystem3D::SetRoughness(appearance_components[dirty_index], 0.41F);
    geometry_bridge.SetDirtyHint(&dirty_index, 1U);
    surface_bridge.SetDirtyHint(&dirty_index, 1U);

    const auto geometry_prepare = geometry_bridge.PrepareGeometry(
        geometry_components.data(),
        component_count,
        2U);
    const auto surface_prepare = surface_bridge.PrepareSurface(
        surface_components.data(),
        component_count,
        2U);

    VR_CHECK(geometry_prepare.build_invoked);
    VR_CHECK(!surface_prepare.build_invoked);
    VR_CHECK(surface_prepare.has_appearance_data);
}

} // namespace
