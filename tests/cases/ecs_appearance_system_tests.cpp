#include "support/test_framework.hpp"
#include "vr/ecs/system/appearance_system.hpp"

#include <cstdint>

namespace {

VR_TEST_CASE(EcsAppearanceSystem_dim2_compare_write_and_dirty, "unit;core;ecs;appearance") {
    using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
    using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;

    Appearance2D component{};
    AppearanceSystem2D::Initialize(component);
    const std::uint32_t style_revision_before_clear = AppearanceSystem2D::StyleRevision(component);
    const std::uint32_t binding_revision_before_clear = AppearanceSystem2D::BindingRevision(component);
    AppearanceSystem2D::ClearDirtyFlags(component, 0xFFFFFFFFU);

    AppearanceSystem2D::SetFillColor(component, vr::ecs::Rgba8{255U, 255U, 255U, 255U});
    VR_CHECK(AppearanceSystem2D::StyleRevision(component) == style_revision_before_clear);
    VR_CHECK(AppearanceSystem2D::DirtyFlags(component) == 0U);

    AppearanceSystem2D::SetFillColor(component, vr::ecs::Rgba8{12U, 34U, 56U, 255U});
    VR_CHECK(AppearanceSystem2D::StyleRevision(component) > style_revision_before_clear);
    VR_CHECK(AppearanceSystem2D::HasDirtyFlags(component, vr::ecs::appearance_dirty_style_flag));

    AppearanceSystem2D::SetTextureBaseId(component, 77U);
    VR_CHECK(AppearanceSystem2D::BindingRevision(component) > binding_revision_before_clear);
    VR_CHECK(AppearanceSystem2D::HasDirtyFlags(component, vr::ecs::appearance_dirty_binding_flag));

    AppearanceSystem2D::SetPaintMode(component, vr::ecs::AppearancePaintMode::pattern);
    VR_CHECK(AppearanceSystem2D::IsVisibleForBatch(component));
    AppearanceSystem2D::SetTextureBaseId(component, 0U);
    VR_CHECK(!AppearanceSystem2D::IsVisibleForBatch(component));
}

VR_TEST_CASE(EcsAppearanceSystem_dim3_style_binding_and_visibility, "unit;core;ecs;appearance") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;

    Appearance3D component{};
    AppearanceSystem3D::Initialize(component);
    AppearanceSystem3D::ClearDirtyFlags(component, 0xFFFFFFFFU);

    AppearanceSystem3D::SetBaseColor(component, vr::ecs::Rgba8{10U, 20U, 30U, 200U});
    AppearanceSystem3D::SetMetallic(component, 0.4F);
    AppearanceSystem3D::SetRoughness(component, 0.6F);
    AppearanceSystem3D::SetShadingModel(component, vr::ecs::AppearanceShadingModel3D::lit_blinn);
    AppearanceSystem3D::SetTextureBaseColorId(component, 101U);
    AppearanceSystem3D::SetTextureNormalId(component, 202U);
    AppearanceSystem3D::SetBindingLayoutId(component, 3U);
    AppearanceSystem3D::SetSamplerStateId(component, 9U);

    VR_CHECK(component.style.base_color.r == 10U);
    VR_CHECK(component.style.base_color.g == 20U);
    VR_CHECK(component.style.base_color.b == 30U);
    VR_CHECK(component.style.base_color.a == 200U);
    VR_CHECK(component.style.metallic == 0.4F);
    VR_CHECK(component.style.roughness == 0.6F);
    VR_CHECK(component.style.shading_model == vr::ecs::AppearanceShadingModel3D::lit_blinn);
    VR_CHECK(component.binding.texture_base_color_id == 101U);
    VR_CHECK(component.binding.texture_normal_id == 202U);
    VR_CHECK(component.binding.binding_layout_id == 3U);
    VR_CHECK(component.binding.sampler_state_id == 9U);
    VR_CHECK(AppearanceSystem3D::HasDirtyFlags(component, vr::ecs::appearance_dirty_style_flag));
    VR_CHECK(AppearanceSystem3D::HasDirtyFlags(component, vr::ecs::appearance_dirty_binding_flag));

    AppearanceSystem3D::SetVisible(component, false);
    VR_CHECK(!AppearanceSystem3D::IsVisible(component));
}

} // namespace

