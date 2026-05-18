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

void CheckSharedBindlessBindings(vr::test::TestContext& test_context_,
                                 const vr::render_graph::CompiledPass& pass_) {
    test_context_.Require(pass_.descriptor_bindings.size() == 2U,
                          "pass_.descriptor_bindings.size() == 2U",
                          {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[0U].set == 0U,
                        "pass_.descriptor_bindings[0U].set == 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[0U].binding == 0U,
                        "pass_.descriptor_bindings[0U].binding == 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[0U].source ==
                            vr::render_graph::DescriptorBindingSource::bindless_table,
                        "pass_.descriptor_bindings[0U].source == DescriptorBindingSource::bindless_table",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[0U].kind ==
                            vr::render_graph::DescriptorBindingKind::sampled_image_table,
                        "pass_.descriptor_bindings[0U].kind == DescriptorBindingKind::sampled_image_table",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[1U].set == 1U,
                        "pass_.descriptor_bindings[1U].set == 1U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[1U].binding == 0U,
                        "pass_.descriptor_bindings[1U].binding == 0U",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[1U].source ==
                            vr::render_graph::DescriptorBindingSource::bindless_table,
                        "pass_.descriptor_bindings[1U].source == DescriptorBindingSource::bindless_table",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
    test_context_.Check(pass_.descriptor_bindings[1U].kind ==
                            vr::render_graph::DescriptorBindingKind::sampler_table,
                        "pass_.descriptor_bindings[1U].kind == DescriptorBindingKind::sampler_table",
                        {__FILE__, static_cast<std::uint32_t>(__LINE__)});
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
    builder.SetPassShaderContract(
        consumer,
        vr::render_graph::MakeSharedBindlessFragmentShaderContract("test_bindless_contract"));
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
    VR_REQUIRE(compiled.DescriptorPlan().pass_layouts.size() == 1U);
    VR_REQUIRE(compiled.DescriptorPlan().writes.size() == 1U);
    VR_REQUIRE(compiled.DescriptorPlan().bindless_allocations.size() == 2U);
    VR_CHECK(compiled.DescriptorPlan().pass_layouts[0U].pass.index == consumer.index);
    VR_REQUIRE(compiled.DescriptorPlan().pass_layouts[0U].bindings.size() == 2U);
    VR_CHECK(compiled.DescriptorPlan().writes[0U].pass.index == consumer.index);
    VR_REQUIRE(compiled.DescriptorPlan().writes[0U].writes.size() == 2U);
    VR_CHECK(compiled.DescriptorPlan().writes[0U].writes[0U].source ==
             vr::render_graph::DescriptorBindingSource::bindless_table);
    VR_CHECK(compiled.DescriptorPlan().writes[0U].writes[0U].source_id == 11U);
    VR_CHECK(compiled.DescriptorPlan().writes[0U].writes[1U].source_id == 12U);
    VR_CHECK(compiled.DescriptorPlan().bindless_allocations[0U].table_id == 11U);
    VR_CHECK(compiled.DescriptorPlan().bindless_allocations[1U].table_id == 12U);

    const std::string json = compiled.BuildJson();
    VR_CHECK(json.find("\"descriptorBindings\"") != std::string::npos);
    VR_CHECK(json.find("\"descriptorPlan\"") != std::string::npos);
    VR_CHECK(json.find("\"passLayouts\"") != std::string::npos);
    VR_CHECK(json.find("\"writeBatches\"") != std::string::npos);
    VR_CHECK(json.find("\"bindlessAllocations\"") != std::string::npos);
    VR_CHECK(json.find("\"source\": \"bindless_table\"") != std::string::npos);
    VR_CHECK(json.find("\"kind\": \"sampled_image_table\"") != std::string::npos);
    VR_CHECK(json.find("\"kind\": \"sampler_table\"") != std::string::npos);
}

VR_TEST_CASE(RenderGraphDescriptor_duplicate_identical_binding_is_accepted,
             "unit;core;render_graph;descriptor") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto pass = builder.AddPass("consume_color", true);
    builder.SetPassShaderContract(
        pass,
        vr::render_graph::MakeSharedBindlessFragmentShaderContract("test_bindless_contract"));
    builder.AddBindlessTableBinding(pass,
                                    0U,
                                    vr::render_graph::DescriptorBindingKind::sampled_image_table,
                                    51U,
                                    vr::render_graph::shader_stage_fragment_flag);
    builder.AddBindlessTableBinding(pass,
                                    0U,
                                    vr::render_graph::DescriptorBindingKind::sampled_image_table,
                                    51U,
                                    vr::render_graph::shader_stage_fragment_flag);
    builder.AddBindlessTableBinding(pass,
                                    1U,
                                    vr::render_graph::DescriptorBindingKind::sampler_table,
                                    52U,
                                    vr::render_graph::shader_stage_fragment_flag);

    const auto compiled = builder.Compile();
    const auto* consume_pass = compiled.FindPass(pass);
    VR_REQUIRE(consume_pass != nullptr);
    CheckSharedBindlessBindings(test_context_, *consume_pass);
    VR_CHECK(consume_pass->descriptor_bindings[0U].source_id == 51U);
    VR_CHECK(consume_pass->descriptor_bindings[1U].source_id == 52U);
}

VR_TEST_CASE(RenderGraphDescriptor_duplicate_set_binding_is_rejected,
             "unit;core;render_graph;descriptor") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto pass = builder.AddPass("consume_color", true);
    builder.SetPassShaderContract(
        pass,
        vr::render_graph::MakeSharedBindlessFragmentShaderContract("test_bindless_contract"));
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

VR_TEST_CASE(RenderGraphDescriptor_shader_contracts_merge_compatible_bindings,
             "unit;core;render_graph;descriptor") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto pass = builder.AddPass("consume_color", true);
    builder.SetPassShaderContract(
        pass,
        vr::render_graph::PassShaderContractDesc{
            .debug_name = "sampled_table_only",
            .bindings = {
                vr::render_graph::ShaderContractBindingDesc{
                    .set = 0U,
                    .binding = 0U,
                    .kind = vr::render_graph::DescriptorBindingKind::sampled_image_table,
                    .stage_flags = vr::render_graph::shader_stage_fragment_flag,
                    .descriptor_count = 3U,
                },
            },
        });
    builder.SetPassShaderContract(
        pass,
        vr::render_graph::PassShaderContractDesc{
            .debug_name = "sampler_table_only",
            .bindings = {
                vr::render_graph::ShaderContractBindingDesc{
                    .set = 1U,
                    .binding = 0U,
                    .kind = vr::render_graph::DescriptorBindingKind::sampler_table,
                    .stage_flags = vr::render_graph::shader_stage_fragment_flag,
                    .descriptor_count = 1U,
                },
            },
        });
    builder.AddBindlessTableBinding(pass,
                                    0U,
                                    vr::render_graph::DescriptorBindingKind::sampled_image_table,
                                    61U,
                                    vr::render_graph::shader_stage_fragment_flag);
    builder.AddBindlessTableBinding(pass,
                                    1U,
                                    vr::render_graph::DescriptorBindingKind::sampler_table,
                                    62U,
                                    vr::render_graph::shader_stage_fragment_flag);

    const auto compiled = builder.Compile();
    const auto* consume_pass = compiled.FindPass(pass);
    VR_REQUIRE(consume_pass != nullptr);
    CheckSharedBindlessBindings(test_context_, *consume_pass);
}

VR_TEST_CASE(RenderGraphDescriptor_conflicting_shader_contract_merge_is_rejected,
             "unit;core;render_graph;descriptor") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto pass = builder.AddPass("consume_color", true);
    builder.SetPassShaderContract(
        pass,
        vr::render_graph::PassShaderContractDesc{
            .debug_name = "sampled_table_only",
            .bindings = {
                vr::render_graph::ShaderContractBindingDesc{
                    .set = 0U,
                    .binding = 0U,
                    .kind = vr::render_graph::DescriptorBindingKind::sampled_image_table,
                    .stage_flags = vr::render_graph::shader_stage_fragment_flag,
                    .descriptor_count = 3U,
                },
            },
        });
    VR_CHECK(ThrowsAnyException([&]() {
        builder.SetPassShaderContract(
            pass,
            vr::render_graph::PassShaderContractDesc{
                .debug_name = "conflicting_sampled_binding",
                .bindings = {
                    vr::render_graph::ShaderContractBindingDesc{
                        .set = 0U,
                        .binding = 0U,
                        .kind = vr::render_graph::DescriptorBindingKind::sampler_table,
                        .stage_flags = vr::render_graph::shader_stage_fragment_flag,
                        .descriptor_count = 1U,
                    },
                },
            });
    }));
}

VR_TEST_CASE(RenderGraphDescriptor_missing_shader_contract_is_rejected,
             "unit;core;render_graph;descriptor") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto pass = builder.AddPass("consume_color", true);
    builder.AddBindlessTableBinding(pass,
                                    0U,
                                    vr::render_graph::DescriptorBindingKind::sampled_image_table,
                                    31U,
                                    vr::render_graph::shader_stage_fragment_flag);
    VR_CHECK(ThrowsAnyException([&]() {
        (void)builder.Compile();
    }));
}

VR_TEST_CASE(RenderGraphDescriptor_shader_contract_mismatch_is_rejected,
             "unit;core;render_graph;descriptor") {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto pass = builder.AddPass("consume_color", true);
    auto contract = vr::render_graph::MakeSharedBindlessFragmentShaderContract("test_bindless_contract");
    contract.bindings[0U].kind = vr::render_graph::DescriptorBindingKind::sampler_table;
    builder.SetPassShaderContract(pass, std::move(contract));
    builder.AddBindlessTableBinding(pass,
                                    0U,
                                    vr::render_graph::DescriptorBindingKind::sampled_image_table,
                                    41U,
                                    vr::render_graph::shader_stage_fragment_flag);
    builder.AddBindlessTableBinding(pass,
                                    1U,
                                    vr::render_graph::DescriptorBindingKind::sampler_table,
                                    42U,
                                    vr::render_graph::shader_stage_fragment_flag);
    VR_CHECK(ThrowsAnyException([&]() {
        (void)builder.Compile();
    }));
}

} // namespace
