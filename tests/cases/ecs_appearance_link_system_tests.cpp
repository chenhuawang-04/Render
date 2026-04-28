#include "support/test_framework.hpp"
#include "vr/ecs/system/appearance_link_system.hpp"
#include "vr/ecs/system/appearance_runtime_system.hpp"

#include <cstdint>

namespace {

template<typename T>
using AppearanceLinkTestMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

VR_TEST_CASE(EcsAppearanceLinkSystem_dim3_geometry_link_updates_runtime_route,
             "unit;core;ecs;appearance;link") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using AppearanceRuntimeSystem3D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using LinkSystem3D = vr::ecs::AppearanceLinkSystem<vr::ecs::Dim3>;

    AppearanceLinkTestMcVector<Appearance3D> appearance_components{};
    appearance_components.resize(2U);
    for (std::uint32_t i = 0U; i < 2U; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetTextureBaseColorId(appearance_components[i], 100U + i);
        AppearanceSystem3D::SetTextureNormalId(appearance_components[i], 200U + i);
        AppearanceSystem3D::SetBindingLayoutId(appearance_components[i], 7U);
        AppearanceSystem3D::SetSamplerStateId(appearance_components[i], 3U);
    }

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim3> appearance_scratch{};
    (void)AppearanceRuntimeSystem3D::Build(appearance_components.data(),
                                           static_cast<std::uint32_t>(appearance_components.size()),
                                           appearance_scratch);

    AppearanceLinkTestMcVector<Geometry3D> geometry_components{};
    geometry_components.resize(4U);
    for (std::uint32_t i = 0U; i < 4U; ++i) {
        GeometrySystem3D::Initialize(geometry_components[i]);
        GeometrySystem3D::SetGeometryId(geometry_components[i], 11U + i);
    }

    GeometrySystem3D::SetAppearanceHandle(geometry_components[0U],
                                          appearance_components[0U].runtime.gpu_record_handle);
    GeometrySystem3D::SetAppearanceHandle(geometry_components[1U],
                                          vr::ecs::AppearanceHandle{
                                              .index = appearance_components[0U].runtime.gpu_record_handle.index,
                                              .generation = static_cast<std::uint32_t>(
                                                  appearance_components[0U].runtime.gpu_record_handle.generation + 1U)
                                          });
    GeometrySystem3D::SetAppearanceHandle(geometry_components[2U],
                                          vr::ecs::AppearanceHandle{
                                              .index = 99U,
                                              .generation = 1U
                                          });

    const vr::ecs::AppearanceLinkStats stats = LinkSystem3D::ApplyToGeometryAligned(
        geometry_components.data(),
        static_cast<std::uint32_t>(geometry_components.size()),
        appearance_components.data(),
        static_cast<std::uint32_t>(appearance_components.size()));

    VR_CHECK(stats.scanned_count == 4U);
    VR_CHECK(stats.resolved_count == 1U);
    VR_CHECK(stats.updated_count == 1U);
    VR_CHECK(stats.missing_handle_count == 1U);
    VR_CHECK(stats.out_of_range_handle_count == 1U);
    VR_CHECK(stats.generation_mismatch_count == 1U);

    const Geometry3D& linked_geometry = geometry_components[0U];
    VR_CHECK(linked_geometry.runtime.route.appearance_handle.index ==
             appearance_components[0U].runtime.gpu_record_handle.index);
    VR_CHECK(linked_geometry.runtime.route.appearance_handle.generation ==
             appearance_components[0U].runtime.gpu_record_handle.generation);
    VR_CHECK(linked_geometry.runtime.route.sort_key ==
             appearance_components[0U].runtime.sort_key);
    VR_CHECK(linked_geometry.runtime.route.appearance_pipeline_bucket ==
             static_cast<std::uint32_t>(appearance_components[0U].runtime.pipeline_key));
    VR_CHECK(linked_geometry.runtime.route.appearance_resource_bucket ==
             static_cast<std::uint32_t>(appearance_components[0U].runtime.resource_key));
    VR_CHECK(linked_geometry.runtime.route.material_id ==
             static_cast<std::uint32_t>(appearance_components[0U].runtime.resource_key));
}

VR_TEST_CASE(EcsAppearanceLinkSystem_dim2_surface_link_dirty_indices_path,
             "unit;core;ecs;appearance;link") {
    using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
    using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
    using AppearanceRuntimeSystem2D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim2>;
    using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
    using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;
    using LinkSystem2D = vr::ecs::AppearanceLinkSystem<vr::ecs::Dim2>;

    AppearanceLinkTestMcVector<Appearance2D> appearance_components{};
    appearance_components.resize(3U);
    for (std::uint32_t i = 0U; i < 3U; ++i) {
        AppearanceSystem2D::Initialize(appearance_components[i]);
        AppearanceSystem2D::SetTextureBaseId(appearance_components[i], 50U + i);
        AppearanceSystem2D::SetBindingLayoutId(appearance_components[i], 5U);
        AppearanceSystem2D::SetSamplerStateId(appearance_components[i], 2U);
    }

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim2> appearance_scratch{};
    (void)AppearanceRuntimeSystem2D::Build(appearance_components.data(),
                                           static_cast<std::uint32_t>(appearance_components.size()),
                                           appearance_scratch);

    AppearanceLinkTestMcVector<Surface2D> surface_components{};
    surface_components.resize(3U);
    for (std::uint32_t i = 0U; i < 3U; ++i) {
        SurfaceSystem2D::Initialize(surface_components[i]);
        SurfaceSystem2D::SetImageId(surface_components[i], 1000U + i, 1U);
    }

    SurfaceSystem2D::SetAppearanceHandle(surface_components[0U],
                                         appearance_components[0U].runtime.gpu_record_handle);
    SurfaceSystem2D::SetAppearanceHandle(surface_components[2U],
                                         vr::ecs::AppearanceHandle{
                                             .index = appearance_components[2U].runtime.gpu_record_handle.index,
                                             .generation = static_cast<std::uint32_t>(
                                                 appearance_components[2U].runtime.gpu_record_handle.generation + 1U)
                                         });

    const std::uint32_t dirty_indices[] = {0U, 2U, 99U};
    const vr::ecs::AppearanceLinkStats stats = LinkSystem2D::ApplyToSurfaceAligned(
        surface_components.data(),
        static_cast<std::uint32_t>(surface_components.size()),
        appearance_components.data(),
        static_cast<std::uint32_t>(appearance_components.size()),
        dirty_indices,
        static_cast<std::uint32_t>(std::size(dirty_indices)));

    VR_CHECK(stats.scanned_count == 2U);
    VR_CHECK(stats.resolved_count == 1U);
    VR_CHECK(stats.updated_count == 1U);
    VR_CHECK(stats.generation_mismatch_count == 1U);
    VR_CHECK(stats.out_of_range_component_index_count == 1U);

    const Surface2D& linked_surface = surface_components[0U];
    VR_CHECK(linked_surface.runtime.route.sort_key ==
             appearance_components[0U].runtime.sort_key);
    VR_CHECK(linked_surface.runtime.route.appearance_pipeline_bucket ==
             static_cast<std::uint32_t>(appearance_components[0U].runtime.pipeline_key));
    VR_CHECK(linked_surface.runtime.route.appearance_resource_bucket ==
             static_cast<std::uint32_t>(appearance_components[0U].runtime.resource_key));
}

} // namespace

