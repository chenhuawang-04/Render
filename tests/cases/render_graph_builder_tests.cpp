#include "support/test_framework.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"

namespace {

template<typename FnT>
[[nodiscard]] bool ThrowsAnyException(FnT&& function_) {
    try {
        function_();
    } catch (...) {
        return true;
    }
    return false;
}

VR_TEST_CASE(RenderGraphBuilder_tracks_linear_resource_versioning,
             "unit;core;render_graph") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scene_color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r16g16b16a16_sfloat,
            .extent = {.width = 1920U, .height = 1080U, .depth = 1U},
        });

    const auto prepare = builder.AddPass("prepare_visibility");
    const auto shade = builder.AddPass("shade_scene");
    const auto present = builder.AddPass("present_scene", true);

    const auto version0 = builder.Read(prepare, scene_color);
    const auto version1 = builder.Write(shade, scene_color);
    const auto present_input = builder.Read(present, scene_color);

    const auto compiled = builder.Compile();

    VR_CHECK(version0.version == 0U);
    VR_CHECK(version1.version == 1U);
    VR_CHECK(present_input.version == 1U);
    VR_REQUIRE(compiled.ExecutionOrder().size() == 3U);
    VR_CHECK(compiled.ExecutionOrder()[0].index == prepare.index);
    VR_CHECK(compiled.ExecutionOrder()[1].index == shade.index);
    VR_CHECK(compiled.ExecutionOrder()[2].index == present.index);
    VR_REQUIRE(compiled.LivenessRanges().size() == 2U);
    VR_CHECK(compiled.LivenessRanges()[0].version.version == 0U);
    VR_CHECK(compiled.LivenessRanges()[0].first_pass_order == 0U);
    VR_CHECK(compiled.LivenessRanges()[0].last_pass_order == 1U);
    VR_CHECK(compiled.LivenessRanges()[1].version.version == 1U);
    VR_CHECK(compiled.LivenessRanges()[1].first_pass_order == 1U);
    VR_CHECK(compiled.LivenessRanges()[1].last_pass_order == 2U);
}

VR_TEST_CASE(RenderGraphBuilder_culls_unrooted_work,
             "unit;core;render_graph") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto scratch = builder.CreateBuffer(
        "scratch_buffer",
        vr::render_graph::BufferDesc{.size_bytes = 4096U});
    const auto orphan = builder.AddPass("orphan_pass");

    (void)builder.Write(orphan, scratch);

    const auto compiled = builder.Compile();
    VR_CHECK(compiled.Empty());
    VR_CHECK(compiled.Passes().empty());
    VR_CHECK(compiled.LivenessRanges().empty());
}

VR_TEST_CASE(RenderGraphBuilder_rejects_non_latest_version_writes,
             "unit;core;render_graph") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto history = builder.CreateTexture(
        "history",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
        });
    const auto writer0 = builder.AddPass("writer0");
    const auto writer1 = builder.AddPass("writer1", true);

    const auto version1 = builder.Write(writer0, history);
    VR_CHECK(version1.version == 1U);
    VR_CHECK(ThrowsAnyException([&]() {
        (void)builder.Write(writer1,
                            vr::render_graph::ResourceVersionHandle{
                                .resource_index = history.index,
                                .version = 0U,
                            });
    }));
}

VR_TEST_CASE(RenderGraphRuntimeService_is_registered_in_runtime_profile,
             "unit;core;render_graph;runtime") {
    using Runtime = vr::runtime::Runtime<>;
    using RenderGraphRuntimeService = vr::runtime::services::RenderGraphRuntimeService;

    static_assert(Runtime::RuntimeServicesType::Contains<RenderGraphRuntimeService>());

    Runtime runtime{};
    auto& services = runtime.Services();
    VR_CHECK(services.TryGet<RenderGraphRuntimeService>() != nullptr);
}

} // namespace
