#include "support/test_framework.hpp"
#include "vr/ecs/component/surface_component.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/surface_system.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;

void ApplySurface2DAppearanceLayer(vr::ecs::Surface<vr::ecs::Dim2>& surface_,
                                   std::int16_t layer_) {
    Appearance2D appearance{};
    AppearanceSystem2D::Initialize(appearance);
    AppearanceSystem2D::SetLayer(appearance, layer_);
    (void)vr::ecs::SurfaceSystem<vr::ecs::Dim2>::ApplyAppearanceRuntimeState(surface_, appearance.style);
}

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

    SurfaceSystem2D::SetSource(surface, vr::ecs::SurfaceImageSourceDesc{.surface_id = 77U, .atlas_page_id = 3U});
    SurfaceSystem2D::SetVisualResourceId(surface, 19U);
    SurfaceSystem2D::SetBatchTag(surface, 11U);
    ApplySurface2DAppearanceLayer(surface, -5);
    SurfaceSystem2D::SetRenderPassHint(surface, vr::ecs::SurfaceRenderPassHint::transparent);

    const std::uint64_t image_sort_key = SurfaceSystem2D::SortKey(surface);
    VR_CHECK(SurfaceSystem2D::ExtractPassBucket(image_sort_key) ==
             static_cast<std::uint32_t>(vr::ecs::SurfaceRenderPassHint::transparent));
    VR_CHECK(SurfaceSystem2D::ExtractVisualResourceBucket(image_sort_key) == 19U);
    VR_CHECK(SurfaceSystem2D::ExtractSurfaceBucket(image_sort_key) == 77U);
    VR_CHECK(SurfaceSystem2D::ExtractBatchBucket(image_sort_key) == 11U);
    const std::uint32_t expected_minor = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(-5) -
        static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()));
    VR_CHECK(SurfaceSystem2D::ExtractMinorBucket(image_sort_key) == expected_minor);
    VR_CHECK(surface.runtime.source.source_kind == vr::ecs::Surface2DSourceKind::image);
    VR_CHECK(surface.runtime.source.surface_id == 77U);
    VR_CHECK(surface.runtime.source.atlas_page_id == 3U);
    VR_CHECK(surface.runtime.route.surface_id == 77U);
    VR_CHECK(SurfaceSystem2D::IsVisibleForBatch(surface));
    VR_CHECK(SurfaceSystem2D::HasDirtyFlags(surface, vr::ecs::surface_dirty_runtime_flag));

    const std::uint32_t revision_after_image = surface.runtime.source_revision;
    SurfaceSystem2D::SetSource(surface, vr::ecs::SurfaceSpriteSourceDesc{.surface_id = 912U, .atlas_page_id = 6U});
    VR_CHECK(surface.runtime.source.source_kind == vr::ecs::Surface2DSourceKind::sprite);
    VR_CHECK(surface.runtime.source.surface_id == 912U);
    VR_CHECK(surface.runtime.source.atlas_page_id == 6U);
    VR_CHECK(surface.runtime.route.surface_id == 912U);
    VR_CHECK(surface.runtime.source_revision > revision_after_image);
    VR_CHECK(SurfaceSystem2D::IsVisibleForBatch(surface));

    SurfaceSystem2D::SetVisible(surface, false);
    VR_CHECK(!SurfaceSystem2D::IsVisibleForBatch(surface));
}

VR_TEST_CASE(EcsSurfaceSystem_dim3_runtime_bridge_apply_updates_transparency_sort,
             "unit;core;ecs;surface") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;

    Surface3D surface{};
    SurfaceSystem3D::Initialize(surface);
    SurfaceSystem3D::SetSource(surface, vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 400U, .sampler_id = 5U, .uv_set = 0U, .flags = 0U});
    SurfaceSystem3D::SetVisualResourceId(surface, 27U);
    SurfaceSystem3D::SetDepthBin(surface, 12U);
    SurfaceSystem3D::ClearDirtyFlags(surface, 0xFFFFFFFFU);

    auto bridge = vr::ecs::ReadAppearanceRuntimeBridge3D(surface.runtime);
    bridge.base_color = vr::ecs::Rgba8{200U, 180U, 160U, 192U};
    bridge.opacity = 0.5F;
    bridge.blend_mode = vr::ecs::AppearanceBlendMode::alpha;
    bridge.alpha_mode = vr::ecs::AppearanceAlphaMode::blend;

    VR_REQUIRE(SurfaceSystem3D::ApplyAppearanceRuntimeBridgeState(surface, bridge));
    const auto stored_bridge = vr::ecs::ReadAppearanceRuntimeBridge3D(surface.runtime);
    VR_CHECK(stored_bridge.base_color.r == 200U);
    VR_CHECK(stored_bridge.base_color.g == 180U);
    VR_CHECK(stored_bridge.base_color.b == 160U);
    VR_CHECK(stored_bridge.opacity == 0.5F);
    VR_CHECK(SurfaceSystem3D::ExtractPassBucket(SurfaceSystem3D::SortKey(surface)) ==
             static_cast<std::uint32_t>(vr::ecs::SurfaceRenderPassHint::transparent));
    VR_CHECK(SurfaceSystem3D::HasDirtyFlags(surface, vr::ecs::surface_dirty_runtime_flag));
    VR_CHECK(!SurfaceSystem3D::ApplyAppearanceRuntimeBridgeState(surface, bridge));
}

VR_TEST_CASE(EcsSurfaceSystem_dim3_texture_route_style_and_visibility, "unit;core;ecs;surface") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;

    Surface3D surface{};
    SurfaceSystem3D::Initialize(surface);
    SurfaceSystem3D::ClearDirtyFlags(surface, 0xFFFFFFFFU);
    Appearance3D appearance{};
    AppearanceSystem3D::Initialize(appearance);

    SurfaceSystem3D::SetSource(surface, vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 301U, .sampler_id = 7U, .uv_set = 2U, .flags = 9U});
    SurfaceSystem3D::SetVisualResourceId(surface, 88U);
    SurfaceSystem3D::SetDepthBin(surface, 17U);
    SurfaceSystem3D::SetUvTransform(surface, 2.0F, 0.5F, 0.1F, -0.2F);
    AppearanceSystem3D::SetDepthTest(appearance, false);
    AppearanceSystem3D::SetDepthWrite(appearance, true);
    AppearanceSystem3D::SetDoubleSided(appearance, true);
    AppearanceSystem3D::SetBaseColor(appearance, vr::ecs::Rgba8{210U, 220U, 255U, 192U});
    AppearanceSystem3D::SetOpacity(appearance, 0.8F);
    (void)SurfaceSystem3D::ApplyAppearanceRuntimeState(surface, appearance.style);
    SurfaceSystem3D::SetFilterMode(surface, vr::ecs::Surface3DFilterMode::anisotropic);
    SurfaceSystem3D::SetAddressMode(surface,
                                    vr::ecs::Surface3DAddressMode::clamp,
                                    vr::ecs::Surface3DAddressMode::mirror,
                                    vr::ecs::Surface3DAddressMode::wrap);

    const std::uint64_t sort_key = SurfaceSystem3D::SortKey(surface);
    VR_CHECK(SurfaceSystem3D::ExtractSurfaceBucket(sort_key) == 301U);
    VR_CHECK(SurfaceSystem3D::ExtractVisualResourceBucket(sort_key) == 88U);
    VR_CHECK(SurfaceSystem3D::ExtractMinorBucket(sort_key) == 17U);
    VR_CHECK(surface.runtime.source.surface_id == 301U);
    VR_CHECK(surface.runtime.source.sampler_id == 7U);
    VR_CHECK(surface.runtime.source.uv_set == 2U);
    VR_CHECK(surface.runtime.source.flags == 9U);
    VR_CHECK(surface.style.uv_scale_u == 2.0F);
    VR_CHECK(surface.style.uv_scale_v == 0.5F);
    VR_CHECK(surface.style.uv_bias_u == 0.1F);
    VR_CHECK(surface.style.uv_bias_v == -0.2F);
    const auto appearance_bridge = vr::ecs::ReadAppearanceRuntimeBridge3D(surface.runtime);
    VR_CHECK(!vr::ecs::IsAppearanceRuntimeBridge3DDepthTestEnabled(appearance_bridge));
    VR_CHECK(vr::ecs::IsAppearanceRuntimeBridge3DDepthWriteEnabled(appearance_bridge));
    VR_CHECK(vr::ecs::IsAppearanceRuntimeBridge3DDoubleSided(appearance_bridge));
    VR_CHECK(appearance_bridge.base_color.r == 210U);
    VR_CHECK(appearance_bridge.base_color.a == 192U);
    VR_CHECK(surface.style.filter_mode == vr::ecs::Surface3DFilterMode::anisotropic);
    VR_CHECK(surface.style.address_u == vr::ecs::Surface3DAddressMode::clamp);
    VR_CHECK(surface.style.address_v == vr::ecs::Surface3DAddressMode::mirror);
    VR_CHECK(surface.style.address_w == vr::ecs::Surface3DAddressMode::wrap);
    VR_CHECK(appearance_bridge.opacity == 0.8F);
    VR_CHECK(SurfaceSystem3D::IsVisibleForBatch(surface));

    SurfaceSystem3D::SetSource(surface, vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 0U});
    VR_CHECK(!SurfaceSystem3D::IsVisibleForBatch(surface));
}

VR_TEST_CASE(EcsSurfaceSystem_dim3_transparent_sort_reverses_depth_minor_bucket,
             "unit;core;ecs;surface") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;

    Surface3D surface{};
    SurfaceSystem3D::Initialize(surface);
    SurfaceSystem3D::SetSource(surface, vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 301U, .sampler_id = 7U, .uv_set = 2U, .flags = 9U});
    SurfaceSystem3D::SetVisualResourceId(surface, 88U);
    SurfaceSystem3D::SetDepthBin(surface, 17U);
    SurfaceSystem3D::SetRenderPassHint(surface, vr::ecs::SurfaceRenderPassHint::transparent);

    const std::uint64_t sort_key = SurfaceSystem3D::SortKey(surface);
    VR_CHECK(SurfaceSystem3D::ExtractPassBucket(sort_key) ==
             static_cast<std::uint32_t>(vr::ecs::SurfaceRenderPassHint::transparent));
    VR_CHECK(SurfaceSystem3D::ExtractMinorBucket(sort_key) ==
             static_cast<std::uint32_t>((std::numeric_limits<std::uint16_t>::max)() - 17U));
}

VR_TEST_CASE(EcsSurfaceSystem_dim3_unlinked_appearance_bridge_promotes_transparent_sort,
             "unit;core;ecs;surface") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;

    Surface3D surface{};
    SurfaceSystem3D::Initialize(surface);
    SurfaceSystem3D::SetSource(surface, vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 302U, .sampler_id = 8U, .uv_set = 0U, .flags = 0U});
    SurfaceSystem3D::SetVisualResourceId(surface, 89U);
    SurfaceSystem3D::SetDepthBin(surface, 19U);

    Appearance3D appearance{};
    AppearanceSystem3D::Initialize(appearance);
    AppearanceSystem3D::SetBlendMode(appearance, vr::ecs::AppearanceBlendMode::alpha);
    AppearanceSystem3D::SetAlphaMode(appearance, vr::ecs::AppearanceAlphaMode::blend);
    AppearanceSystem3D::SetOpacity(appearance, 0.55F);
    (void)SurfaceSystem3D::ApplyAppearanceRuntimeState(surface, appearance.style);

    const std::uint64_t sort_key = SurfaceSystem3D::SortKey(surface);
    VR_CHECK(SurfaceSystem3D::ExtractPassBucket(sort_key) ==
             static_cast<std::uint32_t>(vr::ecs::SurfaceRenderPassHint::transparent));
    VR_CHECK(SurfaceSystem3D::ExtractMinorBucket(sort_key) ==
             static_cast<std::uint32_t>((std::numeric_limits<std::uint16_t>::max)() - 19U));
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

VR_TEST_CASE(EcsSurfaceSystem_appearance_link_preserves_base_visual_resource_and_unlink_restores_effective_route,
             "unit;core;ecs;surface") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;

    Surface3D surface{};
    SurfaceSystem3D::Initialize(surface);
    SurfaceSystem3D::SetSource(surface, vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 301U, .sampler_id = 4U, .uv_set = 0U, .flags = 0U});
    SurfaceSystem3D::SetVisualResourceId(surface, 55U);
    SurfaceSystem3D::SetDepthBin(surface, 9U);

    const vr::ecs::AppearanceHandle handle{.index = 6U, .generation = 1U};
    const bool linked = SurfaceSystem3D::SetAppearanceRuntimeLink(surface,
                                                                  handle,
                                                                  0ULL,
                                                                  0x02000000ULL,
                                                                  900ULL);
    VR_CHECK(linked);
    VR_CHECK(surface.runtime.route.visual_resource_id == 55U);
    VR_CHECK(surface.runtime.route.appearance_visual_resource_id == 900U);
    VR_CHECK(vr::ecs::ResolveEffectiveVisualResourceId(surface.runtime.route) == 900U);
    VR_CHECK(SurfaceSystem3D::ExtractVisualResourceBucket(surface.runtime.route.sort_key) == 900U);
    VR_CHECK(SurfaceSystem3D::ExtractSurfaceBucket(surface.runtime.route.sort_key) == 0U);

    SurfaceSystem3D::SetVisualResourceId(surface, 61U);
    VR_CHECK(surface.runtime.route.visual_resource_id == 61U);
    VR_CHECK(vr::ecs::ResolveEffectiveVisualResourceId(surface.runtime.route) == 900U);
    VR_CHECK(SurfaceSystem3D::ExtractVisualResourceBucket(surface.runtime.route.sort_key) == 900U);
    VR_CHECK(SurfaceSystem3D::ExtractSurfaceBucket(surface.runtime.route.sort_key) == 0U);

    SurfaceSystem3D::ClearAppearanceHandle(surface);
    VR_CHECK(surface.runtime.route.visual_resource_id == 61U);
    VR_CHECK(surface.runtime.route.appearance_visual_resource_id == 0U);
    VR_CHECK(vr::ecs::ResolveEffectiveVisualResourceId(surface.runtime.route) == 61U);
    VR_CHECK(SurfaceSystem3D::ExtractVisualResourceBucket(surface.runtime.route.sort_key) == 61U);
    VR_CHECK(SurfaceSystem3D::ExtractSurfaceBucket(surface.runtime.route.sort_key) == 301U);
}

VR_TEST_CASE(EcsSurfaceSystem_dim3_linked_appearance_is_visible_without_legacy_texture_route,
             "unit;core;ecs;surface") {
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;

    Surface3D surface{};
    SurfaceSystem3D::Initialize(surface);
    SurfaceSystem3D::SetVisualResourceId(surface, 44U);

    const bool linked = SurfaceSystem3D::SetAppearanceRuntimeLink(surface,
                                                                  vr::ecs::AppearanceHandle{.index = 7U, .generation = 1U},
                                                                  0ULL,
                                                                  0ULL,
                                                                  901ULL);
    VR_CHECK(linked);
    VR_CHECK(surface.runtime.source.surface_id == 0U);
    VR_CHECK(SurfaceSystem3D::IsVisibleForBatch(surface));
}

} // namespace

