#include "support/test_framework.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef VR_TEST_SOURCE_DIR
#error "VR_TEST_SOURCE_DIR must be defined for bindless shader contract tests"
#endif

#ifndef VR_TEST_GENERATED_DIR
#error "VR_TEST_GENERATED_DIR must be defined for bindless shader contract tests"
#endif

namespace {

struct BindlessShaderContractExpectation final {
    std::string_view source_shader;
    std::string_view contract_json;
    std::string_view reflect_json;
};

[[nodiscard]] std::filesystem::path SourceRoot() {
    return std::filesystem::path{VR_TEST_SOURCE_DIR};
}

[[nodiscard]] std::filesystem::path GeneratedRoot() {
    return std::filesystem::path{VR_TEST_GENERATED_DIR};
}

[[nodiscard]] std::string ReadUtf8TextFile(const std::filesystem::path& path_) {
    std::ifstream stream(path_, std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("Failed to open file: " + path_.string());
    }

    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

[[nodiscard]] bool Contains(std::string_view haystack_,
                            std::string_view needle_) noexcept {
    return haystack_.find(needle_) != std::string_view::npos;
}

constexpr std::array<BindlessShaderContractExpectation, 15U> kBindlessFragmentShaders{{
    {"geometry_3d.frag",
     "k_geometry_3d_frag_spv.contract.json",
     "k_geometry_3d_frag_spv.reflect.json"},
    {"particle_2d.frag",
     "k_particle_2d_frag_spv.contract.json",
     "k_particle_2d_frag_spv.reflect.json"},
    {"particle_3d.frag",
     "k_particle_3d_frag_spv.contract.json",
     "k_particle_3d_frag_spv.reflect.json"},
    {"render_target_bloom_blur.frag",
     "k_render_target_bloom_blur_frag_spv.contract.json",
     "k_render_target_bloom_blur_frag_spv.reflect.json"},
    {"render_target_bloom_combine.frag",
     "k_render_target_bloom_combine_frag_spv.contract.json",
     "k_render_target_bloom_combine_frag_spv.reflect.json"},
    {"render_target_bloom_prefilter.frag",
     "k_render_target_bloom_prefilter_frag_spv.contract.json",
     "k_render_target_bloom_prefilter_frag_spv.reflect.json"},
    {"render_target_composite.frag",
     "k_render_target_composite_frag_spv.contract.json",
     "k_render_target_composite_frag_spv.reflect.json"},
    {"render_target_temporal_motion.frag",
     "k_render_target_temporal_motion_frag_spv.contract.json",
     "k_render_target_temporal_motion_frag_spv.reflect.json"},
    {"render_target_temporal_resolve.frag",
     "k_render_target_temporal_resolve_frag_spv.contract.json",
     "k_render_target_temporal_resolve_frag_spv.reflect.json"},
    {"sky_environment_equirect.frag",
     "k_sky_environment_equirect_frag_spv.contract.json",
     "k_sky_environment_equirect_frag_spv.reflect.json"},
    {"sky_environment_image.frag",
     "k_sky_environment_image_frag_spv.contract.json",
     "k_sky_environment_image_frag_spv.reflect.json"},
    {"surface_2d.frag",
     "k_surface_2d_frag_spv.contract.json",
     "k_surface_2d_frag_spv.reflect.json"},
    {"surface_3d.frag",
     "k_surface_3d_frag_spv.contract.json",
     "k_surface_3d_frag_spv.reflect.json"},
    {"text_2d.frag",
     "k_text_2d_frag_spv.contract.json",
     "k_text_2d_frag_spv.reflect.json"},
    {"text_3d.frag",
     "k_text_3d_frag_spv.contract.json",
     "k_text_3d_frag_spv.reflect.json"},
}};

VR_TEST_CASE(BindlessShaderContract_fragment_shaders_use_shared_bindless_include,
             "unit;shader;bindless;contract") {
    const std::filesystem::path shaders_root = SourceRoot() / "shaders";

    for (const auto& shader : kBindlessFragmentShaders) {
        const std::filesystem::path shader_path = shaders_root / shader.source_shader;
        const std::string source = ReadUtf8TextFile(shader_path);

        VR_CHECK(Contains(source, "#include \"vr/render/bindless.glsl\""));
        VR_CHECK(!Contains(source, "uniform texture2D g_Textures2D[];"));
        VR_CHECK(!Contains(source, "uniform texture2DArray g_Textures2DArray[];"));
        VR_CHECK(!Contains(source, "uniform textureCube g_TexturesCube[];"));
        VR_CHECK(!Contains(source, "uniform sampler g_Samplers[];"));
    }
}

VR_TEST_CASE(BindlessShaderContract_generated_spirv_matches_bindless_descriptor_contract,
             "unit;shader;bindless;contract") {
    const std::filesystem::path generated_root = GeneratedRoot();

    for (const auto& shader : kBindlessFragmentShaders) {
        const std::string contract =
            ReadUtf8TextFile(generated_root / shader.contract_json);
        const std::string reflect =
            ReadUtf8TextFile(generated_root / shader.reflect_json);

        VR_CHECK(Contains(contract, "\"passed\": true"));
        VR_CHECK(Contains(contract, "\"expected_stage\": \"frag\""));
        VR_CHECK(Contains(contract, "\"reflect_stage\": \"frag\""));
        VR_CHECK(Contains(contract, "\"missing_in_reflect\": []"));
        VR_CHECK(Contains(contract, "\"extra_in_reflect\": []"));
        VR_CHECK(Contains(contract, "\"binding_count_mismatches\": []"));
        VR_CHECK(Contains(contract, "\"set\": 0"));
        VR_CHECK(Contains(contract, "\"binding\": 0"));
        VR_CHECK(Contains(contract, "\"count\": 3"));
        VR_CHECK(Contains(contract, "\"count\": 1"));

        VR_CHECK(Contains(reflect, "\"stage\": \"frag\""));
        VR_CHECK(Contains(reflect, "\"name\": \"g_Textures2D\""));
        VR_CHECK(Contains(reflect, "\"name\": \"g_Textures2DArray\""));
        VR_CHECK(Contains(reflect, "\"name\": \"g_TexturesCube\""));
        VR_CHECK(Contains(reflect, "\"name\": \"g_Samplers\""));
        VR_CHECK(Contains(reflect, "\"kind\": \"separate_image\""));
        VR_CHECK(Contains(reflect, "\"kind\": \"separate_sampler\""));
        VR_CHECK(Contains(reflect, "\"set\": 0"));
        VR_CHECK(Contains(reflect, "\"set\": 1"));
        VR_CHECK(Contains(reflect, "\"binding\": 0"));
    }
}

} // namespace

