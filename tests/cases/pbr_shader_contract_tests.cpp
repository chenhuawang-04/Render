#include "support/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef VR_TEST_SOURCE_DIR
#error "VR_TEST_SOURCE_DIR must be defined for PBR shader contract tests"
#endif

#ifndef VR_TEST_GENERATED_DIR
#error "VR_TEST_GENERATED_DIR must be defined for PBR shader contract tests"
#endif

namespace {

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

VR_TEST_CASE(PbrShaderContract_shared_include_defines_standard_brdf_helpers,
             "unit;shader;pbr;contract") {
    const std::string source =
        ReadUtf8TextFile(SourceRoot() / "shaders" / "include" / "vr" / "render" / "pbr.glsl");

    VR_CHECK(Contains(source, "struct AppearanceSample3D"));
    VR_CHECK(Contains(source, "float D_GGX("));
    VR_CHECK(Contains(source, "float V_SmithGGXCorrelated("));
    VR_CHECK(Contains(source, "vec3 F_Schlick("));
    VR_CHECK(Contains(source, "vec3 ResolvePbrF0("));
    VR_CHECK(Contains(source, "vec3 ResolvePbrDiffuseColor("));
    VR_CHECK(Contains(source, "vec3 EvaluatePbrIblFromTerms("));
    VR_CHECK(Contains(source, "vec3 EvaluatePbrDirect("));
}

VR_TEST_CASE(PbrShaderContract_shared_include_defines_standard_appearance_decode_3d,
             "unit;shader;pbr;contract") {
    const std::string source = ReadUtf8TextFile(
        SourceRoot() / "shaders" / "include" / "vr" / "render" / "appearance_decode_3d.glsl");

    VR_CHECK(Contains(source, "struct DecodedAppearance3D"));
    VR_CHECK(Contains(source, "bool appearance_record_has_texture("));
    VR_CHECK(Contains(source, "bool appearance_has_valid_tangent_basis("));
    VR_CHECK(Contains(source, "DecodedAppearance3D decode_appearance_record_3d("));
    VR_CHECK(Contains(source, "appearance_record_has_texture(presence_mask, k_appearance_texture_presence_normal)"));
    VR_CHECK(Contains(source, "mat3 tbn = mat3(tangent_world, bitangent_world, normalize(normal_world_));"));
    VR_CHECK(Contains(source, "decoded.appearance.normal_world = normalize(tbn * tangent_normal);"));
    VR_CHECK(Contains(source, "float occlusion_value = 1.0;"));
    VR_CHECK(Contains(source, "occlusion_value = orm_sample.r;"));
    VR_CHECK(Contains(source, "decoded.appearance.occlusion = mix(1.0, occlusion_value, occlusion_strength);"));
    VR_CHECK(Contains(source, "decoded.appearance.emissive = emissive_sample * emissive_factor.rgb * max(extras.x, 0.0);"));
    VR_CHECK(Contains(source, "decoded.alpha_cutoff = clamp(extras.y, 0.0, 1.0);"));
    VR_CHECK(Contains(source, "decoded.unlit ="));
}

VR_TEST_CASE(PbrShaderContract_geometry_3d_fragment_uses_shared_standard_pbr_path,
             "unit;shader;pbr;contract") {
    const std::string source =
        ReadUtf8TextFile(SourceRoot() / "shaders" / "geometry_3d.frag");

    VR_CHECK(Contains(source, "#include \"vr/render/pbr.glsl\""));
    VR_CHECK(Contains(source, "#include \"vr/render/appearance_decode_3d.glsl\""));
    VR_CHECK(Contains(source, "DecodedAppearance3D decode_geometry_appearance("));
    VR_CHECK(Contains(source, "layout(set = 2, binding = 7, std430) readonly buffer AppearanceRecordBuffer"));
    VR_CHECK(Contains(source, "layout(location = 3) flat in uint in_appearance_record_index;"));
    VR_CHECK(Contains(source, "layout(location = 4) in vec3 in_tangent_world;"));
    VR_CHECK(Contains(source, "layout(location = 5) in vec3 in_bitangent_world;"));
    VR_CHECK(Contains(source, "AppearanceGpuRecord appearance_record = appearance_records[in_appearance_record_index];"));
    VR_CHECK(Contains(source, "return decode_appearance_record_3d(appearance_record,"));
    VR_CHECK(Contains(source, "DecodedAppearance3D appearance_state = decode_geometry_appearance(normal_world);"));
    VR_CHECK(Contains(source, "AppearanceSample3D appearance = appearance_state.appearance;"));
    VR_CHECK(Contains(source, "vec2 appearance_uv = in_uv * pc.appearance_uv_transform.xy + pc.appearance_uv_transform.zw;"));
    VR_CHECK(Contains(source, "EvaluatePbrDirect(appearance_"));
    VR_CHECK(Contains(source, "return EvaluatePbrIblFromTerms(appearance_,"));
    VR_CHECK(Contains(source, "vec3 ibl_accum = evaluate_ibl(appearance, view_dir);"));
    VR_CHECK(Contains(source, "vec3 lit_color = direct_accum + ibl_accum * appearance.occlusion + appearance.emissive;"));

    VR_CHECK(!Contains(source, "if (!has_appearance_record()) {"));
    VR_CHECK(!Contains(source, "bool has_appearance_record()"));
    VR_CHECK(!Contains(source, "DecodedGeometryMaterial"));
    VR_CHECK(!Contains(source, "decode_appearance_geometry_material("));
    VR_CHECK(!Contains(source, "in_occlusion_strength"));
    VR_CHECK(!Contains(source, "in_instance_params"));
    VR_CHECK(!Contains(source, "pow(max(dot(normal_world_, half_vector), 0.0), 16.0)"));
    VR_CHECK(!Contains(source, "float ambient = 0.12;"));
    VR_CHECK(!Contains(source, "base_albedo.rgb * (ambient + lit_accum) + ibl_accum"));
}

VR_TEST_CASE(PbrShaderContract_geometry_3d_vertex_exports_appearance_record_and_tangent_basis,
             "unit;shader;pbr;contract") {
    const std::string source =
        ReadUtf8TextFile(SourceRoot() / "shaders" / "geometry_3d.vert");

    VR_CHECK(Contains(source, "layout(location = 7) in vec4 in_deform_param0;"));
    VR_CHECK(Contains(source, "layout(location = 8) in vec4 in_deform_param1;"));
    VR_CHECK(Contains(source, "layout(location = 9) in vec4 in_morph_weights;"));
    VR_CHECK(Contains(source, "layout(location = 10) in uint in_component_index;"));
    VR_CHECK(Contains(source, "layout(location = 11) in uint in_appearance_record_index;"));
    VR_CHECK(Contains(source, "layout(location = 3) flat out uint out_appearance_record_index;"));
    VR_CHECK(Contains(source, "out_appearance_record_index = in_appearance_record_index;"));
    VR_CHECK(Contains(source, "layout(location = 22) in vec4 in_tangent;"));
    VR_CHECK(Contains(source, "layout(location = 4) out vec3 out_tangent_world;"));
    VR_CHECK(Contains(source, "layout(location = 5) out vec3 out_bitangent_world;"));
    VR_CHECK(Contains(source, "vec4 skinned_tangent = apply_skinning_tangent(in_tangent);"));
    VR_CHECK(Contains(source, "out_tangent_world = tangent_world;"));
    VR_CHECK(Contains(source, "out_bitangent_world = (bitangent_length > 1e-6)"));

    VR_CHECK(!Contains(source, "in_occlusion_strength"));
    VR_CHECK(!Contains(source, "out_occlusion_strength"));
}

VR_TEST_CASE(PbrShaderContract_geometry_3d_generated_shader_contract_stays_valid,
             "unit;shader;pbr;contract") {
    const std::filesystem::path generated_root = GeneratedRoot();
    const std::string contract =
        ReadUtf8TextFile(generated_root / "k_geometry_3d_frag_spv.contract.json");
    const std::string reflect =
        ReadUtf8TextFile(generated_root / "k_geometry_3d_frag_spv.reflect.json");

    VR_CHECK(Contains(contract, "\"passed\": true"));
    VR_CHECK(Contains(contract, "\"expected_stage\": \"frag\""));
    VR_CHECK(Contains(contract, "\"reflect_stage\": \"frag\""));
    VR_CHECK(Contains(contract, "\"missing_in_reflect\": []"));
    VR_CHECK(Contains(contract, "\"extra_in_reflect\": []"));
    VR_CHECK(Contains(contract, "\"binding_count_mismatches\": []"));
    VR_CHECK(Contains(contract, "\"source_push_constant_count\": 1"));
    VR_CHECK(Contains(contract, "\"reflect_push_constant_count\": 1"));

    VR_CHECK(Contains(reflect, "\"stage\": \"frag\""));
    VR_CHECK(Contains(reflect, "\"name\": \"g_Textures2D\""));
    VR_CHECK(Contains(reflect, "\"name\": \"g_TexturesCube\""));
    VR_CHECK(Contains(reflect, "\"name\": \"LightingParamsBuffer\""));
    VR_CHECK(Contains(reflect, "\"name\": \"AppearanceRecordBuffer\""));
    VR_CHECK(Contains(reflect, "\"name\": \"IblParamsBuffer\""));
    VR_CHECK(Contains(reflect, "\"set\": 2"));
    VR_CHECK(Contains(reflect, "\"binding\": 7"));
    VR_CHECK(Contains(reflect, "\"set\": 3"));
    VR_CHECK(Contains(reflect, "\"binding\": 0"));
}

VR_TEST_CASE(PbrShaderContract_surface_3d_fragment_uses_shared_surface_appearance_decode,
             "unit;shader;pbr;contract") {
    const std::string source =
        ReadUtf8TextFile(SourceRoot() / "shaders" / "surface_3d.frag");

    VR_CHECK(Contains(source, "#include \"vr/render/pbr.glsl\""));
    VR_CHECK(Contains(source, "#include \"vr/render/appearance_decode_3d.glsl\""));
    VR_CHECK(Contains(source, "layout(set = 2, binding = 8, std430) readonly buffer AppearanceRecordBuffer"));
    VR_CHECK(Contains(source, "DecodedAppearance3D decode_surface_appearance(vec3 normal_world_)"));
    VR_CHECK(Contains(source, "AppearanceGpuRecord appearance_record = appearance_records[in_appearance_record_index];"));
    VR_CHECK(Contains(source, "return decode_appearance_record_3d(appearance_record,"));
    VR_CHECK(Contains(source, "DecodedAppearance3D appearance_state = decode_surface_appearance(in_normal_world);"));
    VR_CHECK(Contains(source, "AppearanceSample3D appearance = appearance_state.appearance;"));
    VR_CHECK(Contains(source, "return EvaluatePbrIblFromTerms(appearance_,"));
    VR_CHECK(Contains(source, "if (appearance_state.unlit) {"));
    VR_CHECK(Contains(source, "vec3 shaded_rgb = ibl_lighting * appearance.occlusion + appearance.emissive;"));

    VR_CHECK(!Contains(source, "in_material_params"));
    VR_CHECK(!Contains(source, "surface_sample"));
    VR_CHECK(!Contains(source, "vec3 fresnel = f0 + (vec3(1.0) - f0) * pow(1.0 - n_dot_v, 5.0);"));
    VR_CHECK(!Contains(source, "material.roughness = 0.55;"));
}

} // namespace

