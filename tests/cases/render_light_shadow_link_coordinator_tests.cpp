#include "support/test_framework.hpp"
#include "vr/render/light_shadow_link_coordinator.hpp"

#include <array>
#include <cstdint>

namespace {

VR_TEST_CASE(RenderLightShadowLinkCoordinator3D_prepare_reuse_same_signature,
             "unit;core;render;light;shadow;coordinator") {
    using Coordinator = vr::render::LightShadowLinkCoordinator3D;
    using PrepareInfo = vr::render::LightShadowLinkCoordinator3DPrepareInfo;
    using LightRecord3D = vr::ecs::LightGpuRecord3D;
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowRecord3D = vr::ecs::ShadowGpuRecord3D;

    std::array<LightRecord3D, 2U> light_records{};
    std::array<Shadow3D, 1U> shadow_components{};
    std::array<ShadowRecord3D, 1U> shadow_records{};

    shadow_components[0U].visibility.enabled = 1U;
    shadow_components[0U].visibility.visible = 1U;
    shadow_components[0U].binding.light_component_index = 0U;
    shadow_components[0U].binding.atlas_namespace_id = 3U;
    shadow_records[0U].first_view_index = 8U;
    shadow_records[0U].view_count = 1U;
    shadow_records[0U].projection_kind =
        static_cast<std::uint32_t>(vr::ecs::ShadowProjectionKind::directional);

    Coordinator coordinator{};
    PrepareInfo prepare_info{};
    prepare_info.signature = 0x1234ULL;
    prepare_info.light_records = light_records.data();
    prepare_info.light_record_count = static_cast<std::uint32_t>(light_records.size());
    prepare_info.shadow_components = shadow_components.data();
    prepare_info.shadow_component_count = static_cast<std::uint32_t>(shadow_components.size());
    prepare_info.shadow_records = shadow_records.data();
    prepare_info.shadow_record_count = static_cast<std::uint32_t>(shadow_records.size());
    prepare_info.shadow_namespace_hint = 0U;

    const auto result0 = coordinator.Prepare(prepare_info);
    VR_CHECK(!result0.cache_reused);
    VR_REQUIRE(result0.link_result.linked_light_records != nullptr);
    VR_CHECK(result0.link_result.linked_light_count == 1U);

    const auto result1 = coordinator.Prepare(prepare_info);
    VR_CHECK(result1.cache_reused);
    VR_REQUIRE(result1.link_result.linked_light_records != nullptr);
    VR_CHECK(result1.link_result.linked_light_count == 1U);
    VR_CHECK(result1.link_result.linked_light_records[0U].shadow_view_begin == 8U);

    const auto& stats = coordinator.Stats();
    VR_CHECK(stats.build_count == 1U);
    VR_CHECK(stats.cache_reuse_hit_count >= 1U);
}

VR_TEST_CASE(RenderLightShadowLinkCoordinator3D_prepare_signature_change_rebuilds,
             "unit;core;render;light;shadow;coordinator") {
    using Coordinator = vr::render::LightShadowLinkCoordinator3D;
    using PrepareInfo = vr::render::LightShadowLinkCoordinator3DPrepareInfo;
    using LightRecord3D = vr::ecs::LightGpuRecord3D;
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowRecord3D = vr::ecs::ShadowGpuRecord3D;

    std::array<LightRecord3D, 1U> light_records{};
    std::array<Shadow3D, 1U> shadow_components{};
    std::array<ShadowRecord3D, 1U> shadow_records{};

    shadow_components[0U].visibility.enabled = 1U;
    shadow_components[0U].visibility.visible = 1U;
    shadow_components[0U].binding.light_component_index = 0U;
    shadow_components[0U].binding.atlas_namespace_id = 9U;
    shadow_records[0U].first_view_index = 2U;
    shadow_records[0U].view_count = 1U;
    shadow_records[0U].projection_kind =
        static_cast<std::uint32_t>(vr::ecs::ShadowProjectionKind::spot);

    Coordinator coordinator{};
    PrepareInfo prepare_info{};
    prepare_info.signature = 0xAULL;
    prepare_info.light_records = light_records.data();
    prepare_info.light_record_count = static_cast<std::uint32_t>(light_records.size());
    prepare_info.shadow_components = shadow_components.data();
    prepare_info.shadow_component_count = static_cast<std::uint32_t>(shadow_components.size());
    prepare_info.shadow_records = shadow_records.data();
    prepare_info.shadow_record_count = static_cast<std::uint32_t>(shadow_records.size());

    const auto result0 = coordinator.Prepare(prepare_info);
    VR_CHECK(!result0.cache_reused);
    VR_CHECK(result0.link_result.linked_light_count == 1U);

    prepare_info.signature = 0xBULL;
    const auto result1 = coordinator.Prepare(prepare_info);
    VR_CHECK(!result1.cache_reused);
    VR_CHECK(result1.link_result.linked_light_count == 1U);

    const auto& stats = coordinator.Stats();
    VR_CHECK(stats.build_count == 2U);
    VR_CHECK(stats.cache_reuse_hit_count == 0U);
}

VR_TEST_CASE(RenderLightShadowLinkCoordinator3D_incremental_light_patch_updates_dirty_records,
             "unit;core;render;light;shadow;coordinator") {
    using Coordinator = vr::render::LightShadowLinkCoordinator3D;
    using PrepareInfo = vr::render::LightShadowLinkCoordinator3DPrepareInfo;
    using LightRecord3D = vr::ecs::LightGpuRecord3D;
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowRecord3D = vr::ecs::ShadowGpuRecord3D;

    std::array<LightRecord3D, 4U> light_records{};
    for (std::uint32_t i = 0U; i < light_records.size(); ++i) {
        light_records[i].intensity = static_cast<float>(i + 1U);
    }

    std::array<Shadow3D, 1U> shadow_components{};
    std::array<ShadowRecord3D, 1U> shadow_records{};
    shadow_components[0U].visibility.enabled = 1U;
    shadow_components[0U].visibility.visible = 1U;
    shadow_components[0U].binding.light_component_index = 1U;
    shadow_components[0U].binding.atlas_namespace_id = 7U;
    shadow_records[0U].first_view_index = 13U;
    shadow_records[0U].view_count = 1U;
    shadow_records[0U].projection_kind =
        static_cast<std::uint32_t>(vr::ecs::ShadowProjectionKind::spot);

    Coordinator coordinator{};
    PrepareInfo prepare_info{};
    prepare_info.signature = 0x1000ULL;
    prepare_info.light_signature = 0x10ULL;
    prepare_info.shadow_signature = 0x20ULL;
    prepare_info.light_records = light_records.data();
    prepare_info.light_record_count = static_cast<std::uint32_t>(light_records.size());
    prepare_info.shadow_components = shadow_components.data();
    prepare_info.shadow_component_count = static_cast<std::uint32_t>(shadow_components.size());
    prepare_info.shadow_records = shadow_records.data();
    prepare_info.shadow_record_count = static_cast<std::uint32_t>(shadow_records.size());
    prepare_info.shadow_namespace_hint = 0U;

    const auto result0 = coordinator.Prepare(prepare_info);
    VR_CHECK(!result0.cache_reused);
    VR_REQUIRE(result0.link_result.linked_light_records != nullptr);
    VR_CHECK(result0.link_result.linked_light_records[1U].shadow_view_begin == 13U);

    light_records[1U].intensity = 77.0F;
    const std::uint32_t updated_indices[] = {1U};
    prepare_info.signature = 0x1001ULL;
    prepare_info.light_signature = 0x11ULL;
    prepare_info.shadow_signature = 0x20ULL;
    prepare_info.light_updated_component_indices = updated_indices;
    prepare_info.light_updated_component_count = 1U;
    prepare_info.allow_incremental_light_patch = 1U;

    const auto result1 = coordinator.Prepare(prepare_info);
    VR_CHECK(!result1.cache_reused);
    VR_REQUIRE(result1.link_result.linked_light_records != nullptr);
    VR_CHECK(result1.link_result.linked_light_records[1U].intensity == 77.0F);
    VR_CHECK(result1.link_result.linked_light_records[1U].shadow_view_begin == 13U);
    VR_CHECK(result1.link_result.linked_light_records[1U].shadow_meta != 0U);
    VR_CHECK(result1.link_result.linked_light_records[1U].shadow_namespace_id == 7U);

    const auto& stats = coordinator.Stats();
    VR_CHECK(stats.build_count == 1U);
    VR_CHECK(stats.incremental_patch_count == 1U);
    VR_CHECK(stats.incremental_patched_light_count == 1U);
}

} // namespace
