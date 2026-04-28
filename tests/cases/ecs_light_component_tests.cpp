#include "support/test_framework.hpp"
#include "vr/ecs/component/light_component.hpp"
#include "vr/ecs/system/light_system.hpp"

#include <cstdint>
#include <type_traits>

namespace {

VR_TEST_CASE(EcsLightComponent_is_pure_pod, "unit;core;ecs;light") {
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Light<vr::ecs::Dim2>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Light<vr::ecs::Dim2>>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Light<vr::ecs::Dim3>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Light<vr::ecs::Dim3>>);
}

VR_TEST_CASE(EcsLightSystem_dim2_initialize_and_setters_mark_dirty, "unit;core;ecs;light") {
    using Light2D = vr::ecs::Light<vr::ecs::Dim2>;
    using LightSystem2D = vr::ecs::LightSystem<vr::ecs::Dim2>;

    Light2D light{};
    LightSystem2D::Initialize(light);
    VR_CHECK(light.state.dirty_flags != 0U);
    LightSystem2D::ClearDirtyFlags(light, 0xFFFFFFFFU);
    VR_CHECK(light.state.dirty_flags == 0U);

    LightSystem2D::SetIntensity(light, 2.5F);
    LightSystem2D::SetRange(light, 320.0F);
    LightSystem2D::SetConeAngles(light, 0.2F, 0.8F);
    LightSystem2D::SetCookieResource(light, 11U, 3U);
    LightSystem2D::SetOccluderResource(light, 17U, 4U);
    LightSystem2D::SetBlendMode(light, vr::ecs::Light2DBlendMode::multiply);
    LightSystem2D::SetAffectNormalsOnly(light, true);
    LightSystem2D::SetVisible(light, false);
    LightSystem2D::SetChannelMask(light, 0x00FF00FFU);

    VR_CHECK(light.style.intensity == 2.5F);
    VR_CHECK(light.style.range == 320.0F);
    VR_CHECK(light.binding.cookie.texture_id == 11U);
    VR_CHECK(light.binding.occluder.texture_id == 17U);
    VR_CHECK(light.style.blend_mode == vr::ecs::Light2DBlendMode::multiply);
    VR_CHECK(light.style.affect_normals_only == 1U);
    VR_CHECK(light.visibility.visible == 0U);
    VR_CHECK(light.visibility.light_channel_mask == 0x00FF00FFU);
    VR_CHECK(LightSystem2D::HasDirtyFlags(light,
                                          vr::ecs::light_dirty_style_flag |
                                              vr::ecs::light_dirty_binding_flag |
                                              vr::ecs::light_dirty_runtime_flag));
}

VR_TEST_CASE(EcsLightSystem_dim3_shadow_and_runtime_keys, "unit;core;ecs;light") {
    using Light3D = vr::ecs::Light<vr::ecs::Dim3>;
    using LightSystem3D = vr::ecs::LightSystem<vr::ecs::Dim3>;

    Light3D light{};
    LightSystem3D::Initialize(light);
    LightSystem3D::ClearDirtyFlags(light, 0xFFFFFFFFU);

    LightSystem3D::SetIesResource(light, 21U, 5U);
    LightSystem3D::SetShadowResource(light, 31U, 7U);
    vr::ecs::ShadowConfig shadow_config{};
    shadow_config.resolution = 2048U;
    shadow_config.cascade_count = 4U;
    shadow_config.filter_mode = vr::ecs::ShadowFilterMode::pcf5x5;
    LightSystem3D::SetShadowConfig(light, shadow_config);
    LightSystem3D::SetFalloffMode(light, vr::ecs::LightFalloffMode::smooth);
    LightSystem3D::SetRuntimeKeys(light, 101ULL, 202ULL, 303ULL);
    LightSystem3D::SetGpuRecordHandle(light, vr::ecs::LightHandle{.index = 9U, .generation = 2U});

    VR_CHECK(light.binding.ies.texture_id == 21U);
    VR_CHECK(light.binding.shadow.texture_id == 31U);
    VR_CHECK(light.binding.shadow_config.resolution == 2048U);
    VR_CHECK(light.binding.shadow_config.cascade_count == 4U);
    VR_CHECK(light.binding.shadow_config.filter_mode == vr::ecs::ShadowFilterMode::pcf5x5);
    VR_CHECK(light.style.falloff_mode == vr::ecs::LightFalloffMode::smooth);
    VR_CHECK(light.gpu.pipeline_key == 101ULL);
    VR_CHECK(light.gpu.resource_key == 202ULL);
    VR_CHECK(light.gpu.sort_key == 303ULL);
    VR_CHECK(light.gpu.gpu_record_index == 9U);
    VR_CHECK(light.gpu.handle.generation == 2U);
}

} // namespace

