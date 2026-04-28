#include "support/test_framework.hpp"
#include "vr/ecs/component/surface_component.hpp"
#include "vr/ecs/system/surface_system.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

VR_TEST_CASE(EcsSurfaceComponent_is_pure_pod, "unit;core;ecs;surface") {
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Surface<vr::ecs::Dim2>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Surface<vr::ecs::Dim2>>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Surface<vr::ecs::Dim3>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Surface<vr::ecs::Dim3>>);
}

VR_TEST_CASE(EcsSurfaceSystem_dim2_image_sprite_route_and_sort_key, "unit;core;ecs;surface") {
    using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
    using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;

    Surface2D surface{};
    SurfaceSystem2D::Initialize(surface);
    SurfaceSystem2D::ClearDirtyFlags(surface, 0xFFFFFFFFU);

    SurfaceSystem2D::SetImageId(surface, 77U, 3U);
    SurfaceSystem2D::SetMaterialId(surface, 19U);
    SurfaceSystem2D::SetBatchTag(surface, 11U);
    SurfaceSystem2D::SetLayer(surface, -5);
    SurfaceSystem2D::SetRenderPassHint(surface, vr::ecs::SurfaceRenderPassHint::transparent);

    const std::uint64_t image_sort_key = SurfaceSystem2D::SortKey(surface);
    VR_CHECK(SurfaceSystem2D::ExtractPassBucket(image_sort_key) ==
             static_cast<std::uint32_t>(vr::ecs::SurfaceRenderPassHint::transparent));
    VR_CHECK(SurfaceSystem2D::ExtractMaterialBucket(image_sort_key) == 19U);
    VR_CHECK(SurfaceSystem2D::ExtractSurfaceBucket(image_sort_key) == 77U);
    VR_CHECK(SurfaceSystem2D::ExtractBatchBucket(image_sort_key) == 11U);
    const std::uint32_t expected_minor = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(-5) -
        static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()));
    VR_CHECK(SurfaceSystem2D::ExtractMinorBucket(image_sort_key) == expected_minor);
    VR_CHECK(surface.runtime.source.source_kind == vr::ecs::Surface2DSourceKind::image);
    VR_CHECK(surface.runtime.source.image_id == 77U);
    VR_CHECK(surface.runtime.source.atlas_page_id == 3U);
    VR_CHECK(surface.runtime.route.surface_id == 77U);
    VR_CHECK(SurfaceSystem2D::IsVisibleForBatch(surface));
    VR_CHECK(SurfaceSystem2D::HasDirtyFlags(surface, vr::ecs::surface_dirty_runtime_flag));

    const std::uint32_t revision_after_image = surface.runtime.source_revision;
    SurfaceSystem2D::SetSpriteId(surface, 912U, 6U);
    VR_CHECK(surface.runtime.source.source_kind == vr::ecs::Surface2DSourceKind::sprite);
    VR_CHECK(surface.runtime.source.sprite_id == 912U);
    VR_CHECK(surface.runtime.source.atlas_page_id == 6U);
    VR_CHECK(surface.runtime.route.surface_id == 912U);
    VR_CHECK(surface.runtime.source_revision > revision_after_image);
    VR_CHECK(SurfaceSystem2D::IsVisibleForBatch(surface));

    SurfaceSystem2D::SetVisible(surface, false);
    VR_CHECK(!SurfaceSystem2D::IsVisibleForBatch(surface));
}

VR_TEST_CASE(EcsSurfaceSystem_dim3_texture_route_style_and_visibility, "unit;core;ecs;surface") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;

    Surface3D surface{};
    SurfaceSystem3D::Initialize(surface);
    SurfaceSystem3D::ClearDirtyFlags(surface, 0xFFFFFFFFU);

    SurfaceSystem3D::SetTextureRoute(surface, 301U, 7U, 2U, 9U);
    SurfaceSystem3D::SetMaterialId(surface, 88U);
    SurfaceSystem3D::SetDepthBin(surface, 17U);
    SurfaceSystem3D::SetUvTransform(surface, 2.0F, 0.5F, 0.1F, -0.2F);
    SurfaceSystem3D::SetDepthTest(surface, false);
    SurfaceSystem3D::SetDepthWrite(surface, true);
    SurfaceSystem3D::SetDoubleSided(surface, true);
    SurfaceSystem3D::SetFilterMode(surface, vr::ecs::Surface3DFilterMode::anisotropic);
    SurfaceSystem3D::SetAddressMode(surface,
                                    vr::ecs::Surface3DAddressMode::clamp,
                                    vr::ecs::Surface3DAddressMode::mirror,
                                    vr::ecs::Surface3DAddressMode::wrap);
    SurfaceSystem3D::SetOpacity(surface, 0.8F);

    const std::uint64_t sort_key = SurfaceSystem3D::SortKey(surface);
    VR_CHECK(SurfaceSystem3D::ExtractSurfaceBucket(sort_key) == 301U);
    VR_CHECK(SurfaceSystem3D::ExtractMaterialBucket(sort_key) == 88U);
    VR_CHECK(SurfaceSystem3D::ExtractMinorBucket(sort_key) == 17U);
    VR_CHECK(surface.runtime.texture.texture_id == 301U);
    VR_CHECK(surface.runtime.texture.sampler_id == 7U);
    VR_CHECK(surface.runtime.texture.uv_set == 2U);
    VR_CHECK(surface.runtime.texture.flags == 9U);
    VR_CHECK(surface.style.uv_scale_u == 2.0F);
    VR_CHECK(surface.style.uv_scale_v == 0.5F);
    VR_CHECK(surface.style.uv_bias_u == 0.1F);
    VR_CHECK(surface.style.uv_bias_v == -0.2F);
    VR_CHECK(surface.style.depth_test == 0U);
    VR_CHECK(surface.style.depth_write == 1U);
    VR_CHECK(surface.style.double_sided == 1U);
    VR_CHECK(surface.style.filter_mode == vr::ecs::Surface3DFilterMode::anisotropic);
    VR_CHECK(surface.style.address_u == vr::ecs::Surface3DAddressMode::clamp);
    VR_CHECK(surface.style.address_v == vr::ecs::Surface3DAddressMode::mirror);
    VR_CHECK(surface.style.address_w == vr::ecs::Surface3DAddressMode::wrap);
    VR_CHECK(surface.style.opacity == 0.8F);
    VR_CHECK(SurfaceSystem3D::IsVisibleForBatch(surface));

    SurfaceSystem3D::SetTextureId(surface, 0U);
    VR_CHECK(!SurfaceSystem3D::IsVisibleForBatch(surface));
}

VR_TEST_CASE(EcsSurfaceSystem_default_visibility_requires_valid_source, "unit;core;ecs;surface") {
    using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;

    Surface2D surface_2d{};
    SurfaceSystem2D::Initialize(surface_2d);
    VR_CHECK(surface_2d.runtime.route.visible == 1U);
    VR_CHECK(!SurfaceSystem2D::IsVisibleForBatch(surface_2d));

    Surface3D surface_3d{};
    SurfaceSystem3D::Initialize(surface_3d);
    VR_CHECK(surface_3d.runtime.route.visible == 1U);
    VR_CHECK(!SurfaceSystem3D::IsVisibleForBatch(surface_3d));
}

VR_TEST_CASE(EcsSurfaceSystem_appearance_handle_mutation_serial_monotonic,
             "unit;core;ecs;surface") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;

    Surface3D surface{};
    SurfaceSystem3D::Initialize(surface);

    const std::uint64_t serial_before = SurfaceSystem3D::AppearanceHandleMutationSerial();
    const vr::ecs::AppearanceHandle handle_a{.index = 7U, .generation = 1U};
    SurfaceSystem3D::SetAppearanceHandle(surface, handle_a);
    const std::uint64_t serial_after_set = SurfaceSystem3D::AppearanceHandleMutationSerial();
    VR_CHECK(serial_after_set > serial_before);

    SurfaceSystem3D::SetAppearanceHandle(surface, handle_a);
    const std::uint64_t serial_after_same_set = SurfaceSystem3D::AppearanceHandleMutationSerial();
    VR_CHECK(serial_after_same_set == serial_after_set);

    const bool runtime_changed = SurfaceSystem3D::SetAppearanceRuntimeLink(surface,
                                                                            handle_a,
                                                                            100ULL,
                                                                            2ULL,
                                                                            3ULL);
    VR_CHECK(runtime_changed);
    const std::uint64_t serial_after_runtime_link_same_handle =
        SurfaceSystem3D::AppearanceHandleMutationSerial();
    VR_CHECK(serial_after_runtime_link_same_handle == serial_after_same_set);

    const vr::ecs::AppearanceHandle handle_b{.index = 9U, .generation = 1U};
    const bool runtime_handle_switched = SurfaceSystem3D::SetAppearanceRuntimeLink(surface,
                                                                                    handle_b,
                                                                                    200ULL,
                                                                                    4ULL,
                                                                                    5ULL);
    VR_CHECK(runtime_handle_switched);
    const std::uint64_t serial_after_runtime_link_changed_handle =
        SurfaceSystem3D::AppearanceHandleMutationSerial();
    VR_CHECK(serial_after_runtime_link_changed_handle > serial_after_runtime_link_same_handle);
}

} // namespace
