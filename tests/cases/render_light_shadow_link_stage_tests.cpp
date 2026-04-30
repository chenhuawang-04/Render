#include "support/test_framework.hpp"
#include "vr/render/light_shadow_link_stage.hpp"

#include <array>
#include <cstdint>

namespace {

VR_TEST_CASE(RenderLightShadowLinkStage_dim3_links_shadow_views_to_lights,
             "unit;core;render;light;shadow;link") {
    using LightRecord3D = vr::ecs::LightGpuRecord3D;
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowRecord3D = vr::ecs::ShadowGpuRecord3D;
    using LinkStage = vr::render::LightShadowLinkStage;
    using ScratchVector = vr::render::LightShadowLinkStageMcVector<LightRecord3D>;

    std::array<LightRecord3D, 2U> light_records{};
    light_records[0U].flags = 0U;
    light_records[1U].flags = 1U;

    std::array<Shadow3D, 1U> shadow_components{};
    shadow_components[0U].visibility.enabled = 1U;
    shadow_components[0U].visibility.visible = 1U;
    shadow_components[0U].binding.light_component_index = 1U;
    shadow_components[0U].binding.atlas_namespace_id = 7U;

    std::array<ShadowRecord3D, 1U> shadow_records{};
    shadow_records[0U].first_view_index = 5U;
    shadow_records[0U].view_count = 2U;
    shadow_records[0U].projection_kind =
        static_cast<std::uint32_t>(vr::ecs::ShadowProjectionKind::spot);

    ScratchVector scratch{};
    const auto result = LinkStage::BuildLinkedLightRecords3D(light_records.data(),
                                                             static_cast<std::uint32_t>(light_records.size()),
                                                             shadow_components.data(),
                                                             static_cast<std::uint32_t>(shadow_components.size()),
                                                             shadow_records.data(),
                                                             static_cast<std::uint32_t>(shadow_records.size()),
                                                             0U,
                                                             scratch);

    VR_REQUIRE(result.linked_light_records != nullptr);
    VR_CHECK(result.linked_light_record_count == static_cast<std::uint32_t>(light_records.size()));
    VR_CHECK(result.shadow_namespace_id == 7U);
    VR_CHECK(result.linked_light_count == 1U);
    VR_CHECK(result.namespace_drop_count == 0U);
    VR_CHECK(result.unmapped_light_count == 1U);

    VR_CHECK(result.linked_light_records[0U].shadow_view_begin == LinkStage::invalid_shadow_view_begin);
    VR_CHECK(result.linked_light_records[1U].shadow_view_begin == 5U);
    VR_CHECK((result.linked_light_records[1U].shadow_meta & 0xFFFFU) == 2U);
    VR_CHECK(((result.linked_light_records[1U].shadow_meta >> 16U) & 0xFFU) ==
             static_cast<std::uint32_t>(vr::ecs::ShadowProjectionKind::spot));
    VR_CHECK(result.linked_light_records[1U].shadow_namespace_id == 7U);
}

VR_TEST_CASE(RenderLightShadowLinkStage_dim3_drops_namespace_mismatch,
             "unit;core;render;light;shadow;link") {
    using LightRecord3D = vr::ecs::LightGpuRecord3D;
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowRecord3D = vr::ecs::ShadowGpuRecord3D;
    using LinkStage = vr::render::LightShadowLinkStage;
    using ScratchVector = vr::render::LightShadowLinkStageMcVector<LightRecord3D>;

    std::array<LightRecord3D, 2U> light_records{};
    std::array<Shadow3D, 2U> shadow_components{};
    std::array<ShadowRecord3D, 2U> shadow_records{};

    shadow_components[0U].visibility.enabled = 1U;
    shadow_components[0U].visibility.visible = 1U;
    shadow_components[0U].binding.light_component_index = 0U;
    shadow_components[0U].binding.atlas_namespace_id = 11U;
    shadow_records[0U].first_view_index = 1U;
    shadow_records[0U].view_count = 1U;
    shadow_records[0U].projection_kind =
        static_cast<std::uint32_t>(vr::ecs::ShadowProjectionKind::directional);

    shadow_components[1U].visibility.enabled = 1U;
    shadow_components[1U].visibility.visible = 1U;
    shadow_components[1U].binding.light_component_index = 1U;
    shadow_components[1U].binding.atlas_namespace_id = 99U;
    shadow_records[1U].first_view_index = 2U;
    shadow_records[1U].view_count = 1U;
    shadow_records[1U].projection_kind =
        static_cast<std::uint32_t>(vr::ecs::ShadowProjectionKind::spot);

    ScratchVector scratch{};
    const auto result = LinkStage::BuildLinkedLightRecords3D(light_records.data(),
                                                             static_cast<std::uint32_t>(light_records.size()),
                                                             shadow_components.data(),
                                                             static_cast<std::uint32_t>(shadow_components.size()),
                                                             shadow_records.data(),
                                                             static_cast<std::uint32_t>(shadow_records.size()),
                                                             0U,
                                                             scratch);

    VR_CHECK(result.shadow_namespace_id == 11U);
    VR_CHECK(result.linked_light_count == 1U);
    VR_CHECK(result.namespace_drop_count == 1U);
    VR_CHECK(result.unmapped_light_count == 1U);
    VR_REQUIRE(result.linked_light_records != nullptr);
    VR_CHECK(result.linked_light_records[0U].shadow_view_begin == 1U);
    VR_CHECK(result.linked_light_records[1U].shadow_view_begin == LinkStage::invalid_shadow_view_begin);
}

} // namespace
