#include "support/test_framework.hpp"
#include "vr/surface/surface_upload_host.hpp"

namespace {

VR_TEST_CASE(SurfaceUploadHostPolicy_dim3_partial_upload_requires_dirty_hint_and_partial_cache, "unit;core;surface;upload-policy") {
    vr::surface::Surface3DRuntimeUploadOptions options{};
    options.enable_partial_upload = true;
    options.require_dirty_hint_for_partial = true;
    options.min_partial_dirty_component_count = 2U;

    vr::ecs::Surface3DRuntimeBuildStats runtime_stats{};
    runtime_stats.cache_status = vr::ecs::SurfaceRuntimeCacheStatus::hit_partial_update;
    runtime_stats.transform_only_update = true;
    runtime_stats.transform_update_from_dirty_hint = true;

    const std::uint32_t dirty_indices[]{1U, 3U, 8U};
    vr::ecs::Surface3DRuntimeBuildHint build_hint{};
    build_hint.transform_dirty_component_indices = dirty_indices;
    build_hint.transform_dirty_component_count = 3U;

    VR_CHECK(vr::surface::SurfaceUploadHost::ShouldAttemptPartialUpload(runtime_stats,
                                                                        build_hint,
                                                                        options));

    runtime_stats.transform_update_from_dirty_hint = false;
    VR_CHECK(!vr::surface::SurfaceUploadHost::ShouldAttemptPartialUpload(runtime_stats,
                                                                         build_hint,
                                                                         options));

    runtime_stats.transform_update_from_dirty_hint = true;
    build_hint.transform_dirty_component_count = 1U;
    VR_CHECK(!vr::surface::SurfaceUploadHost::ShouldAttemptPartialUpload(runtime_stats,
                                                                         build_hint,
                                                                         options));

    build_hint.transform_dirty_component_count = 3U;
    build_hint.transform_dirty_component_indices = nullptr;
    VR_CHECK(!vr::surface::SurfaceUploadHost::ShouldAttemptPartialUpload(runtime_stats,
                                                                         build_hint,
                                                                         options));
}

VR_TEST_CASE(SurfaceUploadHostPolicy_dim2_partial_upload_can_disable_hint_strictness, "unit;core;surface;upload-policy") {
    vr::surface::Surface2DRuntimeUploadOptions options{};
    options.enable_partial_upload = true;
    options.require_dirty_hint_for_partial = false;
    options.min_partial_dirty_component_count = 1U;

    vr::ecs::Surface2DRuntimeBuildStats runtime_stats{};
    runtime_stats.cache_status = vr::ecs::SurfaceRuntimeCacheStatus::hit_partial_update;
    runtime_stats.transform_only_update = true;
    runtime_stats.transform_update_from_dirty_hint = false;

    const std::uint32_t dirty_indices[]{2U};
    vr::ecs::Surface2DRuntimeBuildHint build_hint{};
    build_hint.transform_dirty_component_indices = dirty_indices;
    build_hint.transform_dirty_component_count = 1U;

    VR_CHECK(vr::surface::SurfaceUploadHost::ShouldAttemptPartialUpload(runtime_stats,
                                                                        build_hint,
                                                                        options));

    options.enable_partial_upload = false;
    VR_CHECK(!vr::surface::SurfaceUploadHost::ShouldAttemptPartialUpload(runtime_stats,
                                                                         build_hint,
                                                                         options));
}

VR_TEST_CASE(SurfaceUploadHostPolicy_upload_revision_depends_on_surface_and_transform_signatures, "unit;core;surface;upload-policy") {
    const std::uint64_t rev_a = vr::surface::SurfaceUploadHost::ComposeUploadRevision(11U, 17U);
    const std::uint64_t rev_b = vr::surface::SurfaceUploadHost::ComposeUploadRevision(11U, 18U);
    const std::uint64_t rev_c = vr::surface::SurfaceUploadHost::ComposeUploadRevision(12U, 17U);
    const std::uint64_t rev_d = vr::surface::SurfaceUploadHost::ComposeUploadRevision(11U, 17U);

    VR_CHECK(rev_a != rev_b);
    VR_CHECK(rev_a != rev_c);
    VR_CHECK(rev_a == rev_d);
}

} // namespace

