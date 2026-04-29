#include "support/test_framework.hpp"
#include "vr/ecs/component/shadow_component.hpp"
#include "vr/ecs/system/shadow_system.hpp"

#include <type_traits>

namespace {

VR_TEST_CASE(EcsShadowComponent_is_pure_pod, "unit;core;ecs;shadow") {
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Shadow<vr::ecs::Dim2>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Shadow<vr::ecs::Dim2>>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Shadow<vr::ecs::Dim3>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Shadow<vr::ecs::Dim3>>);
}

VR_TEST_CASE(EcsShadowSystem_dim2_initialize_and_setters, "unit;core;ecs;shadow") {
    using Shadow2D = vr::ecs::Shadow<vr::ecs::Dim2>;
    using ShadowSystem2D = vr::ecs::ShadowSystem<vr::ecs::Dim2>;

    Shadow2D shadow{};
    ShadowSystem2D::Initialize(shadow);
    VR_CHECK(shadow.state.dirty_flags != 0U);

    ShadowSystem2D::ClearDirtyFlags(shadow, 0xFFFFFFFFU);
    VR_CHECK(shadow.state.dirty_flags == 0U);

    ShadowSystem2D::SetMapResolution(shadow, 768U, 512U);
    ShadowSystem2D::SetSoftness(shadow, 2.0F);
    ShadowSystem2D::SetBindingMasks(shadow, 0x00FF00FFU, 0x0F0F0F0FU);
    ShadowSystem2D::SetLightComponentIndex(shadow, 3U);
    ShadowSystem2D::SetTransformComponentIndex(shadow, 9U);
    ShadowSystem2D::SetAtlasNamespace(shadow, 4U);
    ShadowSystem2D::SetEnabled(shadow, false);

    VR_CHECK(shadow.style.map_width == 768U);
    VR_CHECK(shadow.style.map_height == 512U);
    VR_CHECK(shadow.style.softness == 2.0F);
    VR_CHECK(shadow.binding.caster_mask == 0x00FF00FFU);
    VR_CHECK(shadow.binding.receiver_mask == 0x0F0F0F0FU);
    VR_CHECK(shadow.binding.light_component_index == 3U);
    VR_CHECK(shadow.binding.transform_component_index == 9U);
    VR_CHECK(shadow.binding.atlas_namespace_id == 4U);
    VR_CHECK(shadow.visibility.enabled == 0U);
    VR_CHECK(ShadowSystem2D::HasDirtyFlags(shadow,
                                           vr::ecs::shadow_dirty_style_flag |
                                               vr::ecs::shadow_dirty_binding_flag |
                                               vr::ecs::shadow_dirty_runtime_flag));
}

VR_TEST_CASE(EcsShadowSystem_dim3_cascade_and_binding, "unit;core;ecs;shadow") {
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowSystem3D = vr::ecs::ShadowSystem<vr::ecs::Dim3>;

    Shadow3D shadow{};
    ShadowSystem3D::Initialize(shadow);
    ShadowSystem3D::ClearDirtyFlags(shadow, 0xFFFFFFFFU);

    ShadowSystem3D::SetCascadeConfig(shadow, 3U, 0.7F);
    ShadowSystem3D::SetProjectionKind(shadow, vr::ecs::ShadowProjectionKind::directional);
    ShadowSystem3D::SetFaceCount(shadow, 6U);
    ShadowSystem3D::SetDepthSlopeBias(shadow, 2.5F);
    ShadowSystem3D::SetPlaneOffsets(shadow, 1.0F, 12.0F);
    ShadowSystem3D::SetCameraComponentIndex(shadow, 5U);
    ShadowSystem3D::SetRuntimeKeys(shadow, 111ULL, 222ULL, 333ULL);
    ShadowSystem3D::SetGpuRecordHandle(shadow, vr::ecs::ShadowHandle{.index = 12U, .generation = 4U});

    VR_CHECK(shadow.style.cascade_count == 3U);
    VR_CHECK(shadow.style.cascade_lambda == 0.7F);
    VR_CHECK(shadow.style.face_count == 6U);
    VR_CHECK(shadow.style.slope_scaled_bias == 2.5F);
    VR_CHECK(shadow.style.near_plane_offset == 1.0F);
    VR_CHECK(shadow.style.far_plane_offset == 12.0F);
    VR_CHECK(shadow.binding.camera_component_index == 5U);
    VR_CHECK(shadow.gpu.pipeline_key == 111ULL);
    VR_CHECK(shadow.gpu.resource_key == 222ULL);
    VR_CHECK(shadow.gpu.sort_key == 333ULL);
    VR_CHECK(shadow.gpu.gpu_record_index == 12U);
    VR_CHECK(shadow.gpu.handle.generation == 4U);
}

} // namespace
