#include "support/test_framework.hpp"
#include "vr/ecs/component/appearance_component.hpp"

#include <type_traits>

namespace {

VR_TEST_CASE(EcsAppearanceComponent_is_pure_pod, "unit;core;ecs;appearance") {
    VR_CHECK(std::is_standard_layout_v<vr::ecs::AppearanceHandle>);
    VR_CHECK(std::is_trivial_v<vr::ecs::AppearanceHandle>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::AppearanceRuntimeCommon>);
    VR_CHECK(std::is_trivial_v<vr::ecs::AppearanceRuntimeCommon>);

    VR_CHECK(std::is_standard_layout_v<vr::ecs::Appearance<vr::ecs::Dim2>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Appearance<vr::ecs::Dim2>>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Appearance<vr::ecs::Dim3>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Appearance<vr::ecs::Dim3>>);
}

VR_TEST_CASE(EcsAppearanceComponent_invalid_handle_defaults, "unit;core;ecs;appearance") {
    VR_CHECK(vr::ecs::invalid_appearance_handle.index == vr::ecs::invalid_appearance_index);
    VR_CHECK(vr::ecs::invalid_appearance_handle.generation == 0U);
}

} // namespace


