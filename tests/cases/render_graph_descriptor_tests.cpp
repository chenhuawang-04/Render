#include "support/test_framework.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <string>

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

VR_TEST_CASE(RenderGraphDescriptor_pass_bindings_compile_into_compiled_graph,
             "unit;core;render_graph;descriptor") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto color = builder.CreateTexture(
        "scene_color",
        vr::render_graph::TextureDesc{
            .format = vr::render_graph::TextureFormat::r8g8b8a8_unorm,
            .extent = {.width = 256U, .height = 256U, .depth = 1U},
            .usage = vr::render_graph::texture_usage_sampled_flag,
        });
    const auto producer = builder.AddPass("produce_color");
    const auto consumer = builder.AddPass("consume_color", true);
    const auto color_v1 = builder.Write(
        producer,
        color,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::color_attachment_write,
        });
    (void)builder.Read(
        consumer,
        color_v1,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_sample_read,
        });
    builder.AddBindlessTableBinding(consumer,
                                    0U,
                                    vr::render_graph::DescriptorBindingKind::sampled_image_table,
                                    11U,
                                    vr::render_graph::shader_stage_fragment_flag);
    builder.AddBindlessTableBinding(consumer,
                                    1U,
                                    vr::render_graph::DescriptorBindingKind::sampler_table,
                                    12U,
                                    vr::render_graph::shader_stage_fragment_flag);

    const auto compiled = builder.Compile();
    VR_REQUIRE(compiled.Passes().size() == 2U);
    const auto* consume_pass = compiled.FindPass(consumer);
    VR_REQUIRE(consume_pass != nullptr);
    VR_REQUIRE(consume_pass->descriptor_bindings.size() == 2U);
    VR_CHECK(consume_pass->descriptor_bindings[0U].set == 0U);
    VR_CHECK(consume_pass->descriptor_bindings[0U].kind ==
             vr::render_graph::DescriptorBindingKind::sampled_image_table);
    VR_CHECK(consume_pass->descriptor_bindings[0U].source ==
             vr::render_graph::DescriptorBindingSource::bindless_table);
    VR_CHECK(consume_pass->descriptor_bindings[0U].source_id == 11U);
    VR_CHECK(consume_pass->descriptor_bindings[1U].set == 1U);
    VR_CHECK(consume_pass->descriptor_bindings[1U].kind ==
             vr::render_graph::DescriptorBindingKind::sampler_table);
    VR_CHECK(consume_pass->descriptor_bindings[1U].source_id == 12U);

    const std::string json = compiled.BuildJson();
    VR_CHECK(json.find("\"descriptorBindings\"") != std::string::npos);
    VR_CHECK(json.find("\"source\": \"bindless_table\"") != std::string::npos);
    VR_CHECK(json.find("\"kind\": \"sampled_image_table\"") != std::string::npos);
    VR_CHECK(json.find("\"kind\": \"sampler_table\"") != std::string::npos);
}

VR_TEST_CASE(RenderGraphDescriptor_duplicate_set_binding_is_rejected,
             "unit;core;render_graph;descriptor") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto pass = builder.AddPass("consume_color", true);
    builder.AddBindlessTableBinding(pass,
                                    0U,
                                    vr::render_graph::DescriptorBindingKind::sampled_image_table,
                                    21U,
                                    vr::render_graph::shader_stage_fragment_flag);
    VR_CHECK(ThrowsAnyException([&]() {
        builder.AddBindlessTableBinding(pass,
                                        0U,
                                        vr::render_graph::DescriptorBindingKind::sampler_table,
                                        22U,
                                        vr::render_graph::shader_stage_fragment_flag);
    }));
}

} // namespace
