#include "support/test_framework.hpp"
#include "vr/render/shadow_atlas_binding_coordinator.hpp"

namespace {

VR_TEST_CASE(RenderShadowAtlasBindingCoordinator_fallback_resolve_and_reuse,
             "unit;core;render;shadow;atlas;coordinator") {
    vr::render::ShadowAtlasBindingCoordinator coordinator{};
    vr::render::ShadowAtlasBindingResolveInput resolve_input{};
    resolve_input.atlas_host = nullptr;
    resolve_input.namespace_id = 77U;
    resolve_input.fallback_namespace_id = 1U;
    resolve_input.allow_namespace_fallback = 1U;
    resolve_input.primary_sampler = VK_NULL_HANDLE;
    resolve_input.fallback_view = reinterpret_cast<VkImageView>(0x1000U);
    resolve_input.fallback_sampler = reinterpret_cast<VkSampler>(0x2000U);
    resolve_input.fallback_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const vr::render::ShadowAtlasBindingResolveResult result0 = coordinator.Resolve(resolve_input);
    VR_CHECK(result0.valid);
    VR_CHECK(!result0.cache_reused);
    VR_CHECK(result0.image_view == resolve_input.fallback_view);
    VR_CHECK(result0.sampler == resolve_input.fallback_sampler);
    VR_CHECK(result0.image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const vr::render::ShadowAtlasBindingResolveResult result1 = coordinator.Resolve(resolve_input);
    VR_CHECK(result1.valid);
    VR_CHECK(result1.cache_reused);
    VR_CHECK(result1.image_view == resolve_input.fallback_view);
    VR_CHECK(result1.sampler == resolve_input.fallback_sampler);

    const auto& stats = coordinator.Stats();
    VR_CHECK(stats.resolve_call_count == 2U);
    VR_CHECK(stats.cache_build_count == 1U);
    VR_CHECK(stats.cache_reuse_hit_count == 1U);
}

VR_TEST_CASE(RenderShadowAtlasBindingCoordinator_key_change_triggers_rebuild,
             "unit;core;render;shadow;atlas;coordinator") {
    vr::render::ShadowAtlasBindingCoordinator coordinator{};
    vr::render::ShadowAtlasBindingResolveInput resolve_input{};
    resolve_input.atlas_host = nullptr;
    resolve_input.namespace_id = 3U;
    resolve_input.primary_sampler = VK_NULL_HANDLE;
    resolve_input.fallback_view = reinterpret_cast<VkImageView>(0x1000U);
    resolve_input.fallback_sampler = reinterpret_cast<VkSampler>(0x2000U);
    resolve_input.fallback_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    (void)coordinator.Resolve(resolve_input);
    resolve_input.namespace_id = 4U;
    const vr::render::ShadowAtlasBindingResolveResult result1 = coordinator.Resolve(resolve_input);
    VR_CHECK(result1.valid);
    VR_CHECK(!result1.cache_reused);

    const auto& stats = coordinator.Stats();
    VR_CHECK(stats.cache_build_count == 2U);
    VR_CHECK(stats.cache_reuse_hit_count == 0U);
}

VR_TEST_CASE(RenderShadowAtlasBindingCoordinator_invalid_when_no_sources,
             "unit;core;render;shadow;atlas;coordinator") {
    vr::render::ShadowAtlasBindingCoordinator coordinator{};
    vr::render::ShadowAtlasBindingResolveInput resolve_input{};
    resolve_input.atlas_host = nullptr;
    resolve_input.namespace_id = 1U;
    resolve_input.primary_sampler = VK_NULL_HANDLE;
    resolve_input.fallback_view = VK_NULL_HANDLE;
    resolve_input.fallback_sampler = VK_NULL_HANDLE;
    resolve_input.fallback_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    const vr::render::ShadowAtlasBindingResolveResult result = coordinator.Resolve(resolve_input);
    VR_CHECK(!result.valid);
    VR_CHECK(result.image_view == VK_NULL_HANDLE);
    VR_CHECK(result.sampler == VK_NULL_HANDLE);
    VR_CHECK(result.image_layout == VK_IMAGE_LAYOUT_UNDEFINED);
}

} // namespace

